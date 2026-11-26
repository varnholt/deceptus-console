/* logical source frames mapped to bounded SNES OBJ tiles and palette banks. preparation only:
 * no video access, so on the SA-1 cartridge it runs on the SA-1 (sprites_commit.c transfers). */
#include "sprites.h"
#include "snes_assets.h"
#include "deceptus/appearance.h"
#include "deceptus/enemies.h"
#include "deceptus/physics.h"
#include "arith.h"
#include "snes_headtorch_resources.h"
#include "snes_sword_resources.h"
#include <snes.h>
#include <stddef.h>
#include <string.h>

#define SNES_OBJ_REQUESTS 96

#ifdef SNES_SPRITES_PROFILE
volatile DeceptusU16 snes_obj_profile_stage;
#define SNES_OBJ_PROFILE(stage) snes_obj_profile_stage = (stage)
#else
#define SNES_OBJ_PROFILE(stage)
#endif

typedef struct SnesObjRequest
{
   DeceptusU16 resource;
   DeceptusI16 x, y;
   DeceptusU8 flip, priority;
} SnesObjRequest;

static SnesObjRequest snes_obj_requests[SNES_OBJ_REQUESTS];
static DeceptusU16 snes_obj_active_resources[SNES_OBJ_REQUESTS];
SnesObjSlot snes_obj_slots[SNES_OBJ_PIECES];
typedef char SnesObjSlotSize[(sizeof(SnesObjSlot) == 8) ? 1 : -1];
/* the resource lists are walked by snesObjDrawResource in sprites_draw.asm. */
DeceptusU8 snes_obj_heads[SNES_SPRITE_RESOURCE_COUNT];
static DeceptusU8 snes_obj_epochs[SNES_SPRITE_RESOURCE_COUNT];
static DeceptusU8 snes_obj_epoch;
static DeceptusU16 snes_obj_unused, snes_obj_free_cursor;
DeceptusU8 snes_obj_oam[SNES_OBJ_OAM_BYTES];
/* per scanline one load word: the objects on the line plus 95 in the low byte, its 8-pixel slivers
 * plus 93 in the high byte. adding a piece sets bit 7 or 15 exactly when the line would hold more
 * than 32 objects or 34 slivers, the hardware limits, so one add both checks and counts
 * (sprites_draw.asm, which defines the same values). */
#define SNES_OBJ_LINE_EMPTY 0x5D5FU
#define SNES_OBJ_LINE_FULL 0x8080U
DeceptusU16 snes_obj_line_load[224];
static const DeceptusU16 *snes_obj_palettes[8];
static DeceptusU16 snes_obj_palette_count;
/* tile pressure: a needed piece found no slot that is neither used nor on screen. */
static DeceptusU8 snes_sprite_blank;
static DeceptusU16 snes_obj_request_count;
DeceptusU16 snes_obj_count;
/* the oam entries the previous frame wrote, which snesObjBegin hides (sprites_begin.asm); every
 * entry above them is hidden already. all of them after a room load. */
DeceptusU16 snes_obj_shown;
static DeceptusU8 snes_sprite_room_load;
DeceptusU8 snes_sprite_overflow;
static DeceptusU8 snes_sprite_initialized;
/* the player's pose history across presentations. */
static DeceptusPlayerAnimation snes_sprite_animation;
SnesSpriteStaging snes_sprite_stagings[SNES_SPRITE_STAGINGS];

static void snesObjAdd(DeceptusU16 resource, DeceptusI16 x, DeceptusI16 y, DeceptusU8 flip, DeceptusU8 priority)
{
   const SnesSpriteResource *entry;
   SnesObjRequest *request;
   if (resource >= SNES_SPRITE_RESOURCE_COUNT)
   {
      snes_sprite_overflow = 1;
      return;
   }
   entry = &snes_sprite_resources[resource];
   x += entry->trim_x;
   y += entry->trim_y;
   if (x >= 256 || y >= 224 || x + entry->columns * 16 <= 0 || y + entry->rows * 16 <= 0)
   {
      return;
   }
   if (snes_obj_request_count >= SNES_OBJ_REQUESTS || entry->columns * entry->rows > 64)
   {
      snes_sprite_overflow = 1;
      return;
   }
   request = &snes_obj_requests[snes_obj_request_count++];
   request->resource = resource;
   request->x = x;
   request->y = y;
   request->flip = flip;
   request->priority = priority;
}

static DeceptusU16 snesObjTile(DeceptusU16 slot)
{
   return (slot / 64) * 256 + ((slot & 63) / 8) * 32 + (slot & 7) * 2;
}

/* sprites_draw.asm finds resident pieces itself and calls this only for a missing one; the
 * lookup repeats here unchanged. */
DeceptusU16 snesObjSlotFor(DeceptusU16 resource, DeceptusU16 piece)
{
   DeceptusU16 index;
   DeceptusU8 link, previous;
   SnesObjSlot *slot;
   for (link = snes_obj_heads[resource]; link; link = slot->next)
   {
      slot = &snes_obj_slots[link - 1];
      if (slot->piece == piece)
      {
         slot->used = 1;
         return link - 1;
      }
   }
   if (snes_obj_unused < SNES_OBJ_PIECES)
   {
      index = snes_obj_unused++;
   }
   else
   {
      for (index = snes_obj_free_cursor; index < SNES_OBJ_PIECES; ++index)
      {
         slot = &snes_obj_slots[index];
         if (!slot->used && !slot->held)
         {
            break;
         }
      }
      snes_obj_free_cursor = index < SNES_OBJ_PIECES ? index + 1 : SNES_OBJ_PIECES;
   }
   if (index >= SNES_OBJ_PIECES)
   {
      /* explicit pressure fallback: never overwrite displayed tiles unblanked. */
      snes_sprite_blank = 1;
      for (index = 0; index < SNES_OBJ_PIECES && snes_obj_slots[index].used; ++index)
      {
      }
   }
   if (index >= SNES_OBJ_PIECES)
   {
      snes_sprite_overflow = 1;
      return 65535;
   }
   slot = &snes_obj_slots[index];
   if (slot->valid)
   {
      previous = 0;
      link = snes_obj_heads[slot->resource];
      while (link && link != index + 1)
      {
         previous = link;
         link = snes_obj_slots[link - 1].next;
      }
      if (previous)
      {
         snes_obj_slots[previous - 1].next = slot->next;
      }
      else
      {
         snes_obj_heads[slot->resource] = slot->next;
      }
   }
   slot->resource = resource;
   slot->piece = piece;
   slot->valid = slot->used = slot->pending = 1;
   slot->next = snes_obj_heads[resource];
   snes_obj_heads[resource] = (DeceptusU8)(index + 1);
   return index;
}

static void snesObjResetCache(void)
{
   memset(snes_obj_slots, 0, sizeof(snes_obj_slots));
   memset(snes_obj_heads, 0, sizeof(snes_obj_heads));
   snes_obj_unused = snes_obj_free_cursor = 0;
}

static DeceptusU8 snesObjPalette(const DeceptusU16 *palette)
{
   DeceptusU16 index, colour;
   for (index = 0; index < snes_obj_palette_count; ++index)
   {
      if (snes_obj_palettes[index] == palette)
      {
         return (DeceptusU8)index;
      }
      for (colour = 0; colour < 16 && snes_obj_palettes[index][colour] == palette[colour]; ++colour)
      {
      }
      if (colour == 16)
      {
         return (DeceptusU8)index;
      }
   }
   if (snes_obj_palette_count == 8)
   {
      snes_sprite_overflow = 1;
      return 0;
   }
   snes_obj_palettes[snes_obj_palette_count++] = palette;
   return (DeceptusU8)(snes_obj_palette_count - 1);
}

#if !defined(SNES_SPRITES_ASM) || defined(SNES_SPRITES_REFERENCE)
static void snesObjPieceReference(DeceptusI16 x, DeceptusI16 y, DeceptusU16 tile, DeceptusU8 palette, DeceptusU8 flip, DeceptusU8 priority,
                                  DeceptusU8 size)
{
   DeceptusI16 line, top, bottom;
   DeceptusU16 object, high, shift, load;
   if (x <= -(DeceptusI16)size)
   {
      return;
   }
   if (x >= 256)
   {
      return;
   }
   if (y <= -(DeceptusI16)size)
   {
      return;
   }
   if (y >= 224)
   {
      return;
   }
   top = y < 0 ? 0 : y;
   bottom = y + size > 224 ? 224 : y + size;
   load = 1 | ((DeceptusU16)(size / 8) << 8);
   for (line = top; line < bottom; ++line)
   {
      if (((snes_obj_line_load[line] + load) & SNES_OBJ_LINE_FULL) != 0)
      {
         snes_sprite_overflow = 1;
         return;
      }
   }
   if (snes_obj_count == 128)
   {
      snes_sprite_overflow = 1;
      return;
   }
   for (line = top; line < bottom; ++line)
   {
      snes_obj_line_load[line] += load;
   }
   object = snes_obj_count * 4;
   snes_obj_oam[object] = (DeceptusU8)x;
   snes_obj_oam[object + 1] = (DeceptusU8)y;
   snes_obj_oam[object + 2] = (DeceptusU8)tile;
   snes_obj_oam[object + 3] = (DeceptusU8)((tile >> 8) | (palette << 1) | (priority << 4) | (flip ? 0x40 : 0));
   high = 512 + snes_obj_count / 4;
   shift = (snes_obj_count & 3) * 2;
   snes_obj_oam[high] |= (DeceptusU8)(((((DeceptusU16)x >> 8) & 1) | (size == 16 ? 2 : 0)) << shift);
   ++snes_obj_count;
}

static void snesObjEmitQuadsReference(DeceptusI16 x, DeceptusI16 y, DeceptusU16 slot, DeceptusU8 palette, DeceptusU8 flip,
                                      DeceptusU8 priority, DeceptusU8 quads)
{
   DeceptusU8 quadrant;
   DeceptusU16 tile;
   tile = snesObjTile(slot);
   if (quads == 15)
   {
      snesObjPieceReference(x, y, tile, palette, flip, priority, 16);
   }
   else
   {
      for (quadrant = 0; quadrant < 4; ++quadrant)
      {
         if (quads & (1U << quadrant))
         {
            snesObjPieceReference(x + (flip ? 1 - (quadrant & 1) : quadrant & 1) * 8, y + (quadrant >> 1) * 8,
                                  tile + (quadrant & 1) + (quadrant >> 1) * 16, palette, flip, priority, 8);
         }
      }
   }
}
#endif

#if !defined(SNES_SPRITES_ASM) || defined(SNES_SPRITES_REFERENCE)
static void snesObjBeginReference(void)
{
   DeceptusU16 index;
   memset(snes_obj_oam, 0, sizeof(snes_obj_oam));
   for (index = 0; index < 224; ++index)
   {
      snes_obj_line_load[index] = SNES_OBJ_LINE_EMPTY;
   }
   for (index = 0; index < SNES_OBJ_PIECES; ++index)
   {
      snes_obj_oam[index * 4 + 1] = 240;
      snes_obj_slots[index].held = snes_obj_slots[index].used;
      snes_obj_slots[index].used = 0;
   }
}
#endif
#ifdef SNES_SPRITES_ASM
void snesObjBegin(void);
#else
#define snesObjBegin snesObjBeginReference
#endif

#ifdef SNES_SPRITES_ASM
/* sprites_draw.asm reads requests, resources and slots by these layouts. */
typedef char SnesObjResourceSize[(sizeof(SnesSpriteResource) == 16) ? 1 : -1];
typedef char SnesObjResourceShape[(offsetof(SnesSpriteResource, columns) == 10) && (offsetof(SnesSpriteResource, rows) == 11) ? 1 : -1];
typedef char SnesObjRequestSize[(sizeof(SnesObjRequest) == 8) ? 1 : -1];
typedef char SnesObjRequestFlags[(offsetof(SnesObjRequest, flip) == 6) && (offsetof(SnesObjRequest, priority) == 7) ? 1 : -1];
typedef char SnesObjSlotFields[(offsetof(SnesObjSlot, piece) == 2) && (offsetof(SnesObjSlot, used) == 4) && (offsetof(SnesObjSlot, next) == 7) ? 1 : -1];
void snesObjDrawResource(const SnesObjRequest *request, DeceptusU16 palette);
#endif

#if !defined(SNES_SPRITES_ASM) || defined(SNES_SPRITES_REFERENCE)
static void snesObjDrawResourceReference(const SnesObjRequest *request, DeceptusU16 palette)
{
   const SnesSpriteResource *entry;
   DeceptusU16 row, column, piece, slot;
   DeceptusI16 x, y;
   DeceptusU8 quads;
   entry = &snes_sprite_resources[request->resource];
   for (row = 0; row < entry->rows; ++row)
   {
      for (column = 0; column < entry->columns; ++column)
      {
         x = request->x + (request->flip ? entry->columns - column - 1 : column) * 16;
         y = request->y + row * 16;
         if (x <= -16 || x >= 256 || y <= -16 || y >= 224)
         {
            continue;
         }
         piece = row * entry->columns + column;
         if (!(snes_sprite_opaque[request->resource][piece >> 3] & (1U << (piece & 7))))
         {
            continue;
         }
         slot = snesObjSlotFor(request->resource, piece);
         if (slot != 65535)
         {
            quads = snes_sprite_quads[snes_sprite_quad_offsets[request->resource] + piece];
            snesObjEmitQuadsReference(x, y, slot, (DeceptusU8)palette, request->flip, request->priority, quads);
         }
      }
   }
}
#endif

/* one coordinate of the points along a rope link: start + delta * remaining / length, truncated
 * toward zero as the shared deceptusGrabRopePointFrom computes it, kept as the quotient and
 * remainder of |delta| * remaining so that each further piece only adds its step. */
typedef struct SnesRopeAxis
{
   DeceptusFixed start;
   DeceptusU16 magnitude, quotient, remainder, step_quotient, step_remainder;
   DeceptusU8 negative;
} SnesRopeAxis;

static void snesRopeAxisStart(SnesRopeAxis *axis, DeceptusFixed start, DeceptusI32 delta, DeceptusU16 remaining, DeceptusU16 length)
{
   DeceptusI32 product;
   axis->start = start;
   axis->negative = delta < 0;
   axis->magnitude = (DeceptusU16)(axis->negative ? -delta : delta);
   product = arithMulS16((DeceptusI16)axis->magnitude, (DeceptusI16)remaining);
   axis->quotient = (DeceptusU16)arithDivS32U16(product, length);
   axis->remainder = (DeceptusU16)(product - arithMulS16((DeceptusI16)axis->quotient, (DeceptusI16)length));
}

static void snesRopeAxisSetStep(SnesRopeAxis *axis, DeceptusU16 step, DeceptusU16 length)
{
   DeceptusI32 product;
   product = arithMulS16((DeceptusI16)axis->magnitude, (DeceptusI16)step);
   axis->step_quotient = (DeceptusU16)arithDivS32U16(product, length);
   axis->step_remainder = (DeceptusU16)(product - arithMulS16((DeceptusI16)axis->step_quotient, (DeceptusI16)length));
}

static void snesRopeAxisAdvance(SnesRopeAxis *axis, DeceptusU16 length)
{
   axis->quotient += axis->step_quotient;
   axis->remainder += axis->step_remainder;
   if (axis->remainder >= length)
   {
      axis->remainder -= length;
      ++axis->quotient;
   }
}

static DeceptusI16 snesRopeAxisPixel(const SnesRopeAxis *axis)
{
   if (axis->negative)
   {
      return deceptusFloor((DeceptusFixed)(axis->start - (DeceptusI16)axis->quotient));
   }
   return deceptusFloor((DeceptusFixed)(axis->start + (DeceptusI16)axis->quotient));
}

/* whether every link's coordinate deltas and length fit the 16-bit products the stepping uses. */
static DeceptusU8 snesRopeSteppable(const DeceptusGrabRope *chain)
{
   DeceptusU8 node;
   DeceptusI32 dx, dy;
   for (node = 1; node < chain->node_count; ++node)
   {
      dx = (DeceptusI32)chain->x[node] - chain->x[node - 1];
      dy = (DeceptusI32)chain->y[node] - chain->y[node - 1];
      if ((dx > 32767L) || (dx < -32767L) || (dy > 32767L) || (dy < -32767L) ||
          (chain->cumulative_length[node] - chain->cumulative_length[node - 1] > 32767U))
      {
         return 0;
      }
   }
   return 1;
}

/* the grab ropes' pieces exactly as the shared appearance emits them. the shared drawing finds each
 * piece's point with a 32-bit multiply and divide; here a link costs one division per step size and
 * each piece some additions. */
static DeceptusU8 snesRopeSprites(const DeceptusEngine *engine, DeceptusSpriteEmitter emit, void *context)
{
   const DeceptusGrabRope *chain;
   DeceptusSpriteAppearance sprite;
   SnesRopeAxis axis_x, axis_y;
   DeceptusU16 distance, length, height, total, previous_total, remaining, link, step;
   DeceptusU8 rope, node, started, steppable;
   DeceptusI16 x, y;
   sprite.family = DECEPTUS_SPRITE_GRAB_ROPE;
   sprite.flip_x = 0;
   remaining = 0;
   for (rope = 0; rope < engine->grab_ropes.count; ++rope)
   {
      length = deceptusGrabRopeLength(engine, rope);
      chain = &engine->grab_ropes.ropes[rope];
      steppable = snesRopeSteppable(chain);
      node = 1;
      started = 0;
      step = 0;
      previous_total = 0;
      link = 0;
      for (distance = 0; distance < length; distance += height)
      {
         height = distance ? 8 : 16;
         if (height > length - distance)
         {
            height = length - distance;
         }
         total = (distance > 2047) ? 32752 : distance * 16;
         if (steppable == 0)
         {
            node = 1;
            deceptusGrabRopePointFrom(engine, rope, distance, &node, &x, &y);
         }
         else
         {
            /* the link holding the point: the first whose end is not passed, as the shared walk. */
            while (node < chain->node_count)
            {
               link = chain->cumulative_length[node] - chain->cumulative_length[node - 1];
               remaining = total - chain->cumulative_length[node - 1];
               if ((remaining <= link) && (link != 0))
               {
                  break;
               }
               ++node;
               started = 0;
            }
            if (node >= chain->node_count)
            {
               x = deceptusFloor(chain->x[chain->node_count - 1]);
               y = deceptusFloor(chain->y[chain->node_count - 1]);
            }
            else
            {
               if (started == 0)
               {
                  snesRopeAxisStart(&axis_x, chain->x[node - 1], (DeceptusI32)chain->x[node] - chain->x[node - 1], remaining, link);
                  snesRopeAxisStart(&axis_y, chain->y[node - 1], (DeceptusI32)chain->y[node] - chain->y[node - 1], remaining, link);
                  started = 1;
                  step = 0;
               }
               else
               {
                  if (total - previous_total != step)
                  {
                     step = total - previous_total;
                     snesRopeAxisSetStep(&axis_x, step, link);
                     snesRopeAxisSetStep(&axis_y, step, link);
                  }
                  snesRopeAxisAdvance(&axis_x, link);
                  snesRopeAxisAdvance(&axis_y, link);
               }
               x = snesRopeAxisPixel(&axis_x);
               y = snesRopeAxisPixel(&axis_y);
            }
            previous_total = total;
         }
         sprite.frame = distance ? height : 0;
         sprite.x = x - engine->camera_x - 7;
         sprite.y = y - engine->camera_y - (distance ? 4 : 0);
         if (!emit(context, &sprite))
         {
            return 0;
         }
      }
   }
   return 1;
}

static DeceptusU8 snesObjAppearance(void *context, const DeceptusSpriteAppearance *sprite)
{
   static const DeceptusU16 first[DECEPTUS_SPRITE_FAMILY_COUNT] = {
      SNES_ACTOR_FIRST,     SNES_LEVER_FIRST,         SNES_SPIKES_FIRST,    SNES_BLOCK_FIRST,          SNES_COLLAPSE_FIRST,
      SNES_RAT_FIRST,       SNES_TRAP_FIRST,          SNES_MUZZLE_FIRST,    SNES_ARROW_FIRST,          SNES_SWIM_FIRST,
      SNES_BLOB_FIRST,      SNES_SPLASH_FIRST,        SNES_SWORD_FIRST,     SNES_CANNON_FIRST,         SNES_CANNONBALL_FIRST,
      SNES_IRON_DOOR_FIRST, SNES_WOODEN_PLANKS_FIRST, SNES_HEADTORCH_FIRST, SNES_HEADTORCH_BEAM_FIRST, SNES_ORB_FIRST,
      SNES_FROG_BLUE_FIRST, SNES_BLUE_FIREBALL_FIRST, SNES_GRAB_ROPE_FIRST, SNES_PLAYER_ROPE_FIRST, SNES_MOVEABLE_BOX_FIRST,
      SNES_CRUSHER_FIRST, SNES_FLOOR_SPIKES_FIRST};
   (void)context;
   if (sprite->family >= DECEPTUS_SPRITE_FAMILY_COUNT)
   {
      return 0;
   }
   snesObjAdd(first[sprite->family] + sprite->frame, sprite->x, sprite->y, sprite->flip_x,
              sprite->family == DECEPTUS_SPRITE_GRAB_ROPE ? 3 : 2);
   return 1;
}

void snesSpritesReset(void)
{
   DeceptusU16 index;
   static const DeceptusPlayerAnimation initial_animation = DECEPTUS_PLAYER_ANIMATION_INITIALIZER;
   if (!snes_sprite_initialized)
   {
      snesObjResetCache();
      snes_sprite_animation = initial_animation;
      snes_sprite_initialized = 1;
   }
   for (index = 0; index < SNES_OBJ_PIECES; ++index)
   {
      snes_obj_slots[index].used = snes_obj_slots[index].held = 0;
   }
   snes_sprite_room_load = 1;
   snes_obj_shown = SNES_OBJ_PIECES;
}

void snesSpritesPrepare(const DeceptusAdventure *game, const DeceptusFrame *frame)
{
   DeceptusU16 index, other, resource, active_count;
   DeceptusU8 palette, link;
   static const DeceptusAppearanceDef appearance = {
      {snes_sword_clips, snes_sword_durations, sizeof(snes_sword_durations), sizeof(snes_sword_clips) / sizeof(snes_sword_clips[0])},
      snes_headtorch_normal,
      snes_headtorch_swim,
      snes_headtorch_rope,
      snes_headtorch_sword,
      snes_headtorch_lamp_offsets,
      snesRopeSprites};
   const SnesSpriteResource *entry;
   const SnesObjRequest *request;
   SNES_OBJ_PROFILE(1);
   snes_obj_request_count = snes_obj_count = snes_obj_palette_count = 0;
   snes_sprite_overflow = snes_sprite_blank = 0;
   snes_obj_free_cursor = 0;
   snesObjBegin();
   SNES_OBJ_PROFILE(2);
   if (!deceptusAppearanceVisit(game, frame, &appearance, &snes_sprite_animation, snesObjAppearance, 0))
   {
      snes_sprite_overflow = 1;
   }
   /* protect all existing frames needed later in this presentation. */
   SNES_OBJ_PROFILE(3);
   active_count = 0;
   if (!++snes_obj_epoch)
   {
      memset(snes_obj_epochs, 0, sizeof(snes_obj_epochs));
      ++snes_obj_epoch;
   }
   for (index = 0; index < snes_obj_request_count; ++index)
   {
      resource = snes_obj_requests[index].resource;
      if (snes_obj_epochs[resource] != snes_obj_epoch)
      {
         snes_obj_epochs[resource] = snes_obj_epoch;
         snes_obj_active_resources[active_count++] = resource;
      }
   }
   for (other = 0; other < active_count; ++other)
   {
      for (link = snes_obj_heads[snes_obj_active_resources[other]]; link; link = snes_obj_slots[link - 1].next)
      {
         snes_obj_slots[link - 1].used = 1;
      }
   }
   for (index = 0; index < snes_obj_request_count; ++index)
   {
      SNES_OBJ_PROFILE(4);
      request = &snes_obj_requests[index];
      resource = request->resource;
      entry = &snes_sprite_resources[resource];
      palette = snesObjPalette(entry->palette);
#ifdef SNES_SPRITES_ASM
      snesObjDrawResource(request, palette);
#else
      snesObjDrawResourceReference(request, palette);
#endif
   }
   snes_obj_shown = snes_obj_count;
   SNES_OBJ_PROFILE(8);
}

static void snesObjStageTileDma(SnesSpriteStaging *staging, const DeceptusU8 *source, DeceptusU16 target)
{
   dmaMemory memory;
   SnesVramDmaCommand *command;
   memory.mem.p = (void *)source;
   command = &staging->tile_dma[staging->tile_dma_count++];
   command->source = memory.mem.c.addr;
   command->bank = memory.mem.c.bank;
   command->increment = 0x80;
   command->target = target;
   command->bytes = 64;
}

static void snesSpritesStagePalettes(SnesSpriteStaging *staging)
{
   DeceptusU16 index;
   staging->palette_count = snes_obj_palette_count;
   for (index = 0; index < snes_obj_palette_count; ++index)
   {
      memcpy(staging->palette_buffer[index], snes_obj_palettes[index], 32);
   }
}

static void snesObjStagePiece(SnesSpriteStaging *staging, DeceptusU16 index, SnesObjSlot *slot)
{
   DeceptusU16 tile;
   const DeceptusU8 *graphics;
   tile = 0x6000 + snesObjTile(index) * 16;
   graphics = snes_sprite_resources[slot->resource].graphics + slot->piece * 128;
   snesObjStageTileDma(staging, graphics, tile);
   snesObjStageTileDma(staging, graphics + 64, tile + 256);
   slot->pending = 0;
}

void snesSpritesStage(DeceptusU8 staging_index)
{
   DeceptusU16 index, pieces;
   SnesSpriteStaging *staging;
   staging = &snes_sprite_stagings[staging_index];
   staging->tile_dma_count = 0;
   snesSpritesStagePalettes(staging);
#ifdef SNES_SA1
   memcpy(staging->oam_copy, snes_obj_oam, SNES_OBJ_OAM_BYTES);
   staging->oam = staging->oam_copy;
#else
   staging->oam = snes_obj_oam;
#endif
   /* flags through a computed pointer are set by if and else: 816-tcc stores the value of an && or
    * || chain through an address it spills on one branch only, so the other branch writes
    * through stale stack contents. */
   staging->blank = snes_sprite_blank;
   staging->forced_blank = 0;
   if ((snes_sprite_room_load != 0) || (snes_sprite_blank != 0))
   {
      staging->forced_blank = 1;
   }
   snes_sprite_room_load = 0;
   pieces = 0;
   for (index = 0; index < SNES_OBJ_PIECES; ++index)
   {
      if (snes_obj_slots[index].pending)
      {
         snesObjStagePiece(staging, index, &snes_obj_slots[index]);
         ++pieces;
      }
   }
   /* a vblank of its own costs up to a display frame; the commit vblank takes a few tiles. */
   staging->deferred = 0;
   if ((staging->forced_blank == 0) && (pieces != 0) && (pieces <= SNES_DEFERRED_PIECES))
   {
      staging->deferred = 1;
   }
   staging->upload_bytes = pieces * 128;
}
