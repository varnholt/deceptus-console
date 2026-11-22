/* logical source frames mapped to bounded Mega Drive sprites and palette banks. */
#include "deceptus/appearance.h"
#include "deceptus/enemies.h"
#include "opening_video.h"
#include "smd_headtorch_resources.h"
#include "smd_sword_resources.h"
#include <genesis.h>

#define SMD_OBJ_REQUESTS 96
#define SMD_OBJ_PIECES 64

typedef struct SmdObjRequest
{
   DeceptusU16 resource;
   DeceptusI16 x, y;
   DeceptusU8 flip, priority;
} SmdObjRequest;

typedef struct SmdObjSlot
{
   DeceptusU16 resource;
   DeceptusU8 piece, valid, used, held, pending, next;
} SmdObjSlot;

static SmdObjRequest smd_obj_requests[SMD_OBJ_REQUESTS];
static DeceptusU16 smd_obj_active_resources[SMD_OBJ_REQUESTS];
static SmdObjSlot smd_obj_slots[SMD_OBJ_PIECES];
static DeceptusU8 smd_obj_heads[SMD_SPRITE_RESOURCE_COUNT], smd_obj_epochs[SMD_SPRITE_RESOURCE_COUNT];
static DeceptusU8 smd_obj_epoch;
static DeceptusU8 smd_obj_piece_links[64];
static DeceptusU16 smd_obj_unused, smd_obj_free_cursor;
/* slots whose tiles still have to be sent. kept as a count so that measuring the upload and the
   usual case of nothing to preload do not scan all 64 slots every presentation. */
static DeceptusU16 smd_obj_pending_count;
static union
{
   struct
   {
      DeceptusI8 objects[225];
      DeceptusI16 slivers[225];
   } ranges;
   struct
   {
      DeceptusU8 lines[224], slivers[224];
   } reference;
} smd_obj_budget;
#define smd_obj_lines smd_obj_budget.reference.lines
#define smd_obj_slivers smd_obj_budget.reference.slivers
static DeceptusU8 smd_obj_fast_budget;
/* lines touched by the fast budget, so the fit check skips the empty part of the screen. */
static DeceptusI16 smd_obj_budget_top, smd_obj_budget_bottom;
static DeceptusU16 smd_obj_request_count, smd_obj_count;
static DeceptusU8 smd_sprite_overflow, smd_sprite_room_load, smd_sprite_blank;
static DeceptusU8 smd_sprite_initialized;
static DeceptusU16 smd_sprite_upload_bytes;
static DeceptusU32 smd_sprite_buffer[32];

static void smdObjAdd(DeceptusU16 resource, DeceptusI16 x, DeceptusI16 y, DeceptusU8 flip, DeceptusU8 priority)
{
   const SmdSpriteResource *entry;
   SmdObjRequest *request;
   if (resource >= SMD_SPRITE_RESOURCE_COUNT)
   {
      smd_sprite_overflow = 1;
      return;
   }
   entry = &smd_sprite_resources[resource];
   x += entry->trim_x;
   y += entry->trim_y;
   if (x >= 320 || y >= 224 || x + entry->columns * 16 <= 0 || y + entry->rows * 16 <= 0)
   {
      return;
   }
   if (smd_obj_request_count >= SMD_OBJ_REQUESTS || entry->columns * entry->rows > 64)
   {
      smd_sprite_overflow = 1;
      return;
   }
   request = &smd_obj_requests[smd_obj_request_count++];
   request->resource = resource;
   request->x = x;
   request->y = y;
   request->flip = flip;
   request->priority = priority;
}

static DeceptusU16 smdObjTile(DeceptusU16 slot)
{
   return SMD_OBJECT_FIRST + slot * 4;
}

/* out of line: a miss is rare, and inlined it took registers from the per-piece loop. */
static DeceptusU16 __attribute__((noinline)) smdObjSlotFor(DeceptusU16 resource, DeceptusU8 piece)
{
   DeceptusU16 index;
   DeceptusU8 link, previous;
   SmdObjSlot *slot;
   for (link = smd_obj_heads[resource]; link; link = slot->next)
   {
      slot = &smd_obj_slots[link - 1];
      if (slot->piece == piece)
      {
         slot->used = 1;
         return link - 1;
      }
   }
   if (smd_obj_unused < SMD_OBJ_PIECES)
   {
      index = smd_obj_unused++;
   }
   else
   {
      for (index = smd_obj_free_cursor; index < SMD_OBJ_PIECES; ++index)
      {
         slot = &smd_obj_slots[index];
         if (!slot->used && !slot->held)
         {
            break;
         }
      }
      smd_obj_free_cursor = index < SMD_OBJ_PIECES ? index + 1 : SMD_OBJ_PIECES;
   }
   if (index >= SMD_OBJ_PIECES)
   {
      /* explicit pressure fallback: never overwrite displayed tiles unblanked. */
      smd_sprite_blank = 1;
      for (index = 0; index < SMD_OBJ_PIECES && smd_obj_slots[index].used; ++index)
      {
      }
   }
   if (index >= SMD_OBJ_PIECES)
   {
      smd_sprite_overflow = 1;
      return 65535;
   }
   slot = &smd_obj_slots[index];
   if (slot->valid)
   {
      previous = 0;
      link = smd_obj_heads[slot->resource];
      while (link && link != index + 1)
      {
         previous = link;
         link = smd_obj_slots[link - 1].next;
      }
      if (previous)
      {
         smd_obj_slots[previous - 1].next = slot->next;
      }
      else
      {
         smd_obj_heads[slot->resource] = slot->next;
      }
   }
   slot->resource = resource;
   slot->piece = piece;
   if (!slot->pending)
   {
      ++smd_obj_pending_count;
   }
   slot->valid = slot->used = slot->pending = 1;
   slot->next = smd_obj_heads[resource];
   smd_obj_heads[resource] = (DeceptusU8)(index + 1);
   return index;
}

static void smdObjResetCache(void)
{
   memset(smd_obj_slots, 0, sizeof(smd_obj_slots));
   memset(smd_obj_heads, 0, sizeof(smd_obj_heads));
   smd_obj_unused = smd_obj_free_cursor = 0;
   smd_obj_pending_count = 0;
}

/* the same fields VDP_setSpriteFull writes, without an out-of-line library call per object. */
static void smdObjHardware(DeceptusU16 index, DeceptusI16 x, DeceptusI16 y, DeceptusU8 size, DeceptusU16 attributes)
{
   VDPSprite *sprite;
   sprite = &vdpSpriteCache[index];
   sprite->y = y + 0x80;
   sprite->size = size;
   sprite->link = (DeceptusU8)(index + 1);
   sprite->attribut = attributes;
   sprite->x = x + 0x80;
}

static void smdObjPiece(DeceptusI16 x, DeceptusI16 y, DeceptusU16 tile, DeceptusU8 palette, DeceptusU8 flip, DeceptusU8 width,
                        DeceptusU8 height)
{
   DeceptusI16 top, bottom, line;
   DeceptusU8 slivers;
   if (x <= -(DeceptusI16)width || x >= 320 || y <= -(DeceptusI16)height || y >= 224)
   {
      return;
   }
   top = y < 0 ? 0 : y;
   bottom = y + height > 224 ? 224 : y + height;
   if (smd_obj_count >= 80)
   {
      smd_sprite_overflow = 1;
      return;
   }
   slivers = width >> 3;
   if (smd_obj_fast_budget)
   {
      if (top < smd_obj_budget_top)
      {
         smd_obj_budget_top = top;
      }
      if (bottom > smd_obj_budget_bottom)
      {
         smd_obj_budget_bottom = bottom;
      }
      ++smd_obj_budget.ranges.objects[top];
      --smd_obj_budget.ranges.objects[bottom];
      smd_obj_budget.ranges.slivers[top] += slivers;
      smd_obj_budget.ranges.slivers[bottom] -= slivers;
   }
   else
   {
      for (line = top; line < bottom; ++line)
      {
         if (smd_obj_lines[line] >= 20 || smd_obj_slivers[line] + slivers > 40)
         {
            smd_sprite_overflow = 1;
            return;
         }
      }
      for (line = top; line < bottom; ++line)
      {
         ++smd_obj_lines[line];
         smd_obj_slivers[line] += slivers;
      }
   }
   smdObjHardware(smd_obj_count, x, y, SPRITE_SIZE(slivers, height >> 3), TILE_ATTR_FULL(palette, 0, 0, flip, tile));
   ++smd_obj_count;
}

/* out of line: only the exact-budget pass uses it per piece, and inlined into the request
   emitter it took registers from the fast pass. */
static void __attribute__((noinline)) smdObjQuads(DeceptusI16 x, DeceptusI16 y, DeceptusU16 tile, DeceptusU8 palette, DeceptusU8 flip,
                                                  DeceptusU8 quads)
{
   DeceptusU8 column, bits, fits;
   DeceptusI16 px, line, top, bottom;
   fits = 0;
   if ((quads & 5) && (quads & 10))
   {
      fits = 1;
      top = y < 0 ? 0 : y;
      bottom = y + 16 > 224 ? 224 : y + 16;
      for (line = top; !smd_obj_fast_budget && line < bottom; ++line)
      {
         if (smd_obj_lines[line] >= 20 || smd_obj_slivers[line] > 38)
         {
            fits = 0;
            break;
         }
      }
   }
   /* transparent quadrants may share one larger obj when its fetch budget fits. */
   if (quads == 15 || fits)
   {
      smdObjPiece(x, y, tile, palette, flip, 16, 16);
      return;
   }
   for (column = 0; column < 2; ++column)
   {
      bits = (quads >> column) & 5;
      px = x + (flip ? 1 - column : column) * 8;
      if (bits == 5)
      {
         smdObjPiece(px, y, tile + column * 2, palette, flip, 8, 16);
      }
      else
      {
         if (bits & 1)
         {
            smdObjPiece(px, y, tile + column * 2, palette, flip, 8, 8);
         }
         if (bits & 4)
         {
            smdObjPiece(px, y + 8, tile + column * 2 + 1, palette, flip, 8, 8);
         }
      }
   }
}

/* the fast budget's line counts for objects of one height that start on the same line. the
   difference arrays add up in any order, so a row of whole pieces is entered once. */
static void smdObjBudgetAdd(DeceptusI16 y, DeceptusI16 height, DeceptusU16 objects, DeceptusU16 slivers)
{
   DeceptusI16 top, bottom;
   top = y < 0 ? 0 : y;
   bottom = y + height > 224 ? 224 : y + height;
   if (top < smd_obj_budget_top)
   {
      smd_obj_budget_top = top;
   }
   if (bottom > smd_obj_budget_bottom)
   {
      smd_obj_budget_bottom = bottom;
   }
   smd_obj_budget.ranges.objects[top] += objects;
   smd_obj_budget.ranges.objects[bottom] -= objects;
   smd_obj_budget.ranges.slivers[top] += slivers;
   smd_obj_budget.ranges.slivers[bottom] -= slivers;
}

/* smdObjQuads under the fast budget for a piece that cannot be one 16x16 object: each 8-pixel
   column becomes an 8x16 or 8x8 object, as smdObjPiece would place it, without its seven
   stacked arguments and per-call checks. returns the new object count. */
static DeceptusU16 smdObjSplit(DeceptusU16 count, DeceptusI16 x, DeceptusI16 y, DeceptusU16 attributes, DeceptusU16 quads)
{
   DeceptusU16 column, bits, flip;
   DeceptusI16 px, py, height;
   flip = attributes & 0x800;
   for (column = 0; column < 2; ++column)
   {
      bits = (quads >> column) & 5;
      if (!bits)
      {
         continue;
      }
      px = x + (flip ? 1 - column : column) * 8;
      py = bits == 4 ? y + 8 : y;
      height = bits == 5 ? 16 : 8;
      if (px <= -8 || px >= 320 || py <= -height || py >= 224)
      {
         continue;
      }
      if (count >= 80)
      {
         smd_sprite_overflow = 1;
         continue;
      }
      smdObjBudgetAdd(py, height, 1, 1);
      smdObjHardware(count, px, py, SPRITE_SIZE(1, height >> 3), attributes + column * 2 + (bits == 4));
      ++count;
   }
   return count;
}

static DeceptusU8 smdObjAppearance(void *context, const DeceptusSpriteAppearance *sprite)
{
   static const DeceptusU16 first[DECEPTUS_SPRITE_FAMILY_COUNT] = {
      SMD_ACTOR_FIRST,     SMD_LEVER_FIRST,         SMD_SPIKES_FIRST,    SMD_BLOCK_FIRST,          SMD_COLLAPSE_FIRST,
      SMD_RAT_FIRST,       SMD_TRAP_FIRST,          SMD_MUZZLE_FIRST,    SMD_ARROW_FIRST,          SMD_SWIM_FIRST,
      SMD_BLOB_FIRST,      SMD_SPLASH_FIRST,        SMD_SWORD_FIRST,     SMD_CANNON_FIRST,         SMD_CANNONBALL_FIRST,
      SMD_IRON_DOOR_FIRST, SMD_WOODEN_PLANKS_FIRST, SMD_HEADTORCH_FIRST, SMD_HEADTORCH_BEAM_FIRST, SMD_ORB_FIRST,
      SMD_FROG_BLUE_FIRST, SMD_BLUE_FIREBALL_FIRST, SMD_GRAB_ROPE_FIRST, SMD_PLAYER_ROPE_FIRST};
   (void)context;
   if (sprite->family >= DECEPTUS_SPRITE_FAMILY_COUNT)
   {
      return 0;
   }
   smdObjAdd(first[sprite->family] + sprite->frame, sprite->x, sprite->y, sprite->flip_x,
             sprite->family == DECEPTUS_SPRITE_GRAB_ROPE ? 3 : 2);
   return 1;
}

/* index a resource's resident pieces once per request; walking the slot list for every piece
   made large frames quadratic. slots of active resources are protected by the caller, so the
   table stays valid while misses allocate. */
static void smdObjLinkPieces(DeceptusU16 resource, DeceptusU8 enable)
{
   DeceptusU16 link;
   for (link = smd_obj_heads[resource]; link; link = smd_obj_slots[link - 1].next)
   {
      smd_obj_piece_links[smd_obj_slots[link - 1].piece] = enable ? (DeceptusU8)link : 0;
   }
}

/* the slot holding a visible piece's tiles, allocating it on a miss; 65535 when none is free. */
static DeceptusU16 smdObjPieceSlot(DeceptusU16 resource, DeceptusU16 piece)
{
   DeceptusU16 link;
   link = smd_obj_piece_links[piece];
   if (link)
   {
      smd_obj_slots[link - 1].used = 1;
      return link - 1;
   }
   return smdObjSlotFor(resource, (DeceptusU8)piece);
}

/* one request's visible pieces under the fast budget, the common pass. timing sections with the
   V counter showed that a piece cost the 68000 well over a thousand cycles in bookkeeping rather
   than in one expensive step: byte counters re-extended for every index, globals re-read per
   piece, values spilled to the stack because the exact-budget path shared the loop, and every
   partial piece going through smdObjQuads and smdObjPiece with seven stacked arguments each.
   this loop walks word counters, keeps the object count local, enters the budget once per row
   of whole pieces and splits partial pieces inline. */
static void __attribute__((noinline)) smdObjEmitRequest(const SmdObjRequest *request)
{
   const DeceptusU8 *quad_row;
   DeceptusU16 resource, slot, attributes, count, quads, row_pieces;
   DeceptusU16 row, column, rows, columns, piece;
   DeceptusI16 x, y, first_x, step;
   resource = request->resource;
   rows = smd_sprite_resources[resource].rows;
   columns = smd_sprite_resources[resource].columns;
   quad_row = smd_sprite_quads + smd_sprite_quad_offsets[resource];
   count = smd_obj_count;
   attributes = TILE_ATTR_FULL(smd_sprite_resources[resource].palette_bank, 0, 0, request->flip, 0);
   smdObjLinkPieces(resource, 1);
   step = request->flip ? -16 : 16;
   first_x = request->flip ? request->x + (columns - 1) * 16 : request->x;
   for (row = 0, y = request->y; row < rows; ++row, y += 16, quad_row += columns)
   {
      if (y <= -16 || y >= 224)
      {
         continue;
      }
      row_pieces = 0;
      piece = row * columns;
      for (column = 0, x = first_x; column < columns; ++column, x += step)
      {
         /* the converter sets a piece's opaque bit exactly when it has an occupied quadrant. */
         quads = quad_row[column];
         if (!quads || x <= -16 || x >= 320)
         {
            continue;
         }
         slot = smdObjPieceSlot(resource, piece + column);
         if (slot == 65535)
         {
            continue;
         }
         if (quads == 15 || ((quads & 5) && (quads & 10)))
         {
            /* transparent quadrants may share one larger obj under the fast budget. */
            if (count >= 80)
            {
               smd_sprite_overflow = 1;
               continue;
            }
            smdObjHardware(count, x, y, SPRITE_SIZE(2, 2), attributes | smdObjTile(slot));
            ++count;
            ++row_pieces;
         }
         else
         {
            count = smdObjSplit(count, x, y, attributes | smdObjTile(slot), quads);
         }
      }
      if (row_pieces)
      {
         smdObjBudgetAdd(y, 16, row_pieces, row_pieces * 2);
      }
   }
   smd_obj_count = count;
   smdObjLinkPieces(resource, 0);
}

/* the exact-budget pass, for scenes whose fast packing does not fit: every piece is checked
   line by line through smdObjQuads. kept apart so it does not cost the fast loop registers. */
static void __attribute__((noinline)) smdObjEmitRequestExact(const SmdObjRequest *request)
{
   const DeceptusU8 *quad_row;
   DeceptusU16 resource, slot, row, column, rows, columns;
   DeceptusI16 x, y, first_x, step;
   DeceptusU8 palette;
   resource = request->resource;
   rows = smd_sprite_resources[resource].rows;
   columns = smd_sprite_resources[resource].columns;
   palette = smd_sprite_resources[resource].palette_bank;
   quad_row = smd_sprite_quads + smd_sprite_quad_offsets[resource];
   smdObjLinkPieces(resource, 1);
   step = request->flip ? -16 : 16;
   first_x = request->flip ? request->x + (columns - 1) * 16 : request->x;
   for (row = 0, y = request->y; row < rows; ++row, y += 16, quad_row += columns)
   {
      for (column = 0, x = first_x; column < columns; ++column, x += step)
      {
         if (y <= -16 || y >= 224 || !quad_row[column] || x <= -16 || x >= 320)
         {
            continue;
         }
         slot = smdObjPieceSlot(resource, row * columns + column);
         if (slot != 65535)
         {
            smdObjQuads(x, y, smdObjTile(slot), palette, request->flip, quad_row[column]);
         }
      }
   }
   smdObjLinkPieces(resource, 0);
}

static void smdSpritesInit(void)
{
   DeceptusU16 index;
   if (!smd_sprite_initialized)
   {
      smdObjResetCache();
      smd_sprite_initialized = 1;
   }
   for (index = 0; index < SMD_OBJ_PIECES; ++index)
   {
      smd_obj_slots[index].used = smd_obj_slots[index].held = 0;
   }
   VDP_resetSprites();
   smd_sprite_room_load = 1;
}

static void smdSpritesPreparePass(const DeceptusAdventure *game, const DeceptusFrame *frame)
{
   DeceptusU16 index, other, resource, active_count;
   DeceptusU8 link;
   static DeceptusPlayerAnimation animation = DECEPTUS_PLAYER_ANIMATION_INITIALIZER;
   static const DeceptusAppearanceDef appearance = {
      {smd_sword_clips, smd_sword_durations, sizeof(smd_sword_durations), sizeof(smd_sword_clips) / sizeof(smd_sword_clips[0])},
      smd_headtorch_normal,
      smd_headtorch_swim,
      smd_headtorch_rope,
      smd_headtorch_sword,
      smd_headtorch_lamp_offsets};
   smd_obj_request_count = smd_obj_count = 0;
   smd_sprite_overflow = smd_sprite_blank = 0;
   smd_obj_free_cursor = 0;
   memset(&smd_obj_budget, 0, sizeof(smd_obj_budget));
   smd_obj_budget_top = 224;
   smd_obj_budget_bottom = 0;
   for (index = 0; index < SMD_OBJ_PIECES; ++index)
   {
      smd_obj_slots[index].used = 0;
   }
   if (!deceptusAppearanceVisit(game, frame, &appearance, &animation, smdObjAppearance, 0))
   {
      smd_sprite_overflow = 1;
   }
   /* protect all existing frames needed later in this presentation. */
   active_count = 0;
   if (!++smd_obj_epoch)
   {
      memset(smd_obj_epochs, 0, sizeof(smd_obj_epochs));
      ++smd_obj_epoch;
   }
   for (index = 0; index < smd_obj_request_count; ++index)
   {
      resource = smd_obj_requests[index].resource;
      if (smd_obj_epochs[resource] != smd_obj_epoch)
      {
         smd_obj_epochs[resource] = smd_obj_epoch;
         smd_obj_active_resources[active_count++] = resource;
      }
   }
   for (other = 0; other < active_count; ++other)
   {
      for (link = smd_obj_heads[smd_obj_active_resources[other]]; link; link = smd_obj_slots[link - 1].next)
      {
         smd_obj_slots[link - 1].used = 1;
      }
   }
   for (index = 0; index < smd_obj_request_count; ++index)
   {
      if (smd_obj_fast_budget)
      {
         smdObjEmitRequest(&smd_obj_requests[index]);
      }
      else
      {
         smdObjEmitRequestExact(&smd_obj_requests[index]);
      }
   }
}

static DeceptusU8 smdObjBudgetFits(void)
{
   DeceptusI16 objects, slivers;
   DeceptusI16 line;
   for (line = smd_obj_budget_top, objects = slivers = 0; line < smd_obj_budget_bottom; ++line)
   {
      objects += smd_obj_budget.ranges.objects[line];
      slivers += smd_obj_budget.ranges.slivers[line];
      if (objects > 20 || slivers > 40)
      {
         return 0;
      }
   }
   return 1;
}

static void smdSpritesPrepare(const DeceptusAdventure *game, const DeceptusFrame *frame)
{
   DeceptusU8 blank, overflow;
   smd_obj_fast_budget = 1;
   smdSpritesPreparePass(game, frame);
   if (!smdObjBudgetFits())
   {
      /* keep the exact quadrant path for scenes that need finer packing. */
      blank = smd_sprite_blank;
      overflow = smd_sprite_overflow;
      smd_obj_fast_budget = 0;
      smdSpritesPreparePass(game, frame);
      smd_sprite_blank |= blank;
      smd_sprite_overflow |= overflow;
   }
}

static void smdSpritesMeasure(void)
{
   smd_sprite_upload_bytes = smd_obj_pending_count * 128;
}

static void smdSpritesPreload(void)
{
   DeceptusU16 index;
   SmdObjSlot *slot;
   for (index = 0; smd_obj_pending_count && index < SMD_OBJ_PIECES; ++index)
   {
      slot = &smd_obj_slots[index];
      if (!slot->pending)
      {
         continue;
      }
      if (!smdDecodeBlock(&smd_sprite_resources[slot->resource].graphics, slot->piece, (DeceptusU8 *)smd_sprite_buffer))
      {
         return;
      }
      VDP_loadTileData(smd_sprite_buffer, smdObjTile(index), 4, DMA);
      slot->pending = 0;
      --smd_obj_pending_count;
   }
   smd_sprite_room_load = 0;
}

static void smdSpritesCommit(void)
{
   DeceptusU16 index;
   if (smd_obj_count)
   {
      VDP_setSpriteLink(smd_obj_count - 1, 0);
      VDP_updateSprites(smd_obj_count, DMA);
   }
   else
   {
      VDP_setSpriteFull(0, 0, 240, SPRITE_SIZE(1, 1), 0, 0);
      VDP_updateSprites(1, DMA);
   }
   /* four slots per pass: the copy itself is one byte move, so loop overhead was most of its cost. */
   for (index = 0; index < SMD_OBJ_PIECES; index += 4)
   {
      smd_obj_slots[index].held = smd_obj_slots[index].used;
      smd_obj_slots[index + 1].held = smd_obj_slots[index + 1].used;
      smd_obj_slots[index + 2].held = smd_obj_slots[index + 2].used;
      smd_obj_slots[index + 3].held = smd_obj_slots[index + 3].used;
   }
}
