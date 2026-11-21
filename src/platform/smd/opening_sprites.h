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
static DeceptusU16 smd_obj_unused, smd_obj_free_cursor;
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

static DeceptusU16 smdObjSlotFor(DeceptusU16 resource, DeceptusU8 piece)
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
}

static void smdObjPiece(DeceptusI16 x, DeceptusI16 y, DeceptusU16 tile, DeceptusU8 palette, DeceptusU8 flip, DeceptusU8 width,
                        DeceptusU8 height)
{
   DeceptusI16 top, bottom, line;
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
   if (smd_obj_fast_budget)
   {
      ++smd_obj_budget.ranges.objects[top];
      --smd_obj_budget.ranges.objects[bottom];
      smd_obj_budget.ranges.slivers[top] += width / 8;
      smd_obj_budget.ranges.slivers[bottom] -= width / 8;
   }
   else
   {
      for (line = top; line < bottom; ++line)
      {
         if (smd_obj_lines[line] >= 20 || smd_obj_slivers[line] + width / 8 > 40)
         {
            smd_sprite_overflow = 1;
            return;
         }
      }
      for (line = top; line < bottom; ++line)
      {
         ++smd_obj_lines[line];
         smd_obj_slivers[line] += width / 8;
      }
   }
   VDP_setSpriteFull(smd_obj_count, x, y, SPRITE_SIZE(width / 8, height / 8), TILE_ATTR_FULL(palette, 0, 0, flip, tile), smd_obj_count + 1);
   ++smd_obj_count;
}

static void smdObjQuads(DeceptusI16 x, DeceptusI16 y, DeceptusU16 tile, DeceptusU8 palette, DeceptusU8 flip, DeceptusU8 quads)
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
   DeceptusU16 index, other, resource, row, column, piece, slot, active_count;
   DeceptusI16 x, y;
   DeceptusU8 palette, link, quads;
   static DeceptusPlayerAnimation animation = DECEPTUS_PLAYER_ANIMATION_INITIALIZER;
   static const DeceptusAppearanceDef appearance = {
      {smd_sword_clips, smd_sword_durations, sizeof(smd_sword_durations), sizeof(smd_sword_clips) / sizeof(smd_sword_clips[0])},
      smd_headtorch_normal,
      smd_headtorch_swim,
      smd_headtorch_rope,
      smd_headtorch_sword,
      smd_headtorch_lamp_offsets};
   const SmdSpriteResource *entry;
   const SmdObjRequest *request;
   smd_obj_request_count = smd_obj_count = 0;
   smd_sprite_overflow = smd_sprite_blank = 0;
   smd_obj_free_cursor = 0;
   memset(&smd_obj_budget, 0, sizeof(smd_obj_budget));
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
      request = &smd_obj_requests[index];
      entry = &smd_sprite_resources[request->resource];
      palette = entry->palette_bank;
      for (row = 0; row < entry->rows; ++row)
      {
         for (column = 0; column < entry->columns; ++column)
         {
            x = request->x + (request->flip ? entry->columns - column - 1 : column) * 16;
            y = request->y + row * 16;
            if (x <= -16 || x >= 320 || y <= -16 || y >= 224)
            {
               continue;
            }
            piece = row * entry->columns + column;
#ifdef SMD_SPRITE_OPAQUE_MASK
            if (!(smd_sprite_opaque[request->resource][piece >> 3] & (1U << (piece & 7))))
            {
               continue;
            }
#endif
            slot = smdObjSlotFor(request->resource, (DeceptusU8)piece);
            if (slot != 65535)
            {
               quads = 15;
#ifdef SMD_SPRITE_QUADS
               quads = smd_sprite_quads[smd_sprite_quad_offsets[request->resource] + piece];
#endif
               smdObjQuads(x, y, smdObjTile(slot), palette, request->flip, quads);
            }
         }
      }
   }
}

static DeceptusU8 smdObjBudgetFits(void)
{
   DeceptusI16 objects, slivers;
   DeceptusU16 line;
   for (line = 0, objects = slivers = 0; line < 224; ++line)
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
   DeceptusU16 index;
   smd_sprite_upload_bytes = 0;
   for (index = 0; index < SMD_OBJ_PIECES; ++index)
   {
      if (smd_obj_slots[index].pending)
      {
         smd_sprite_upload_bytes += 128;
      }
   }
}

static void smdSpritesPreload(void)
{
   DeceptusU16 index;
   SmdObjSlot *slot;
   for (index = 0; index < SMD_OBJ_PIECES; ++index)
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
   for (index = 0; index < SMD_OBJ_PIECES; ++index)
   {
      smd_obj_slots[index].held = smd_obj_slots[index].used;
   }
}
