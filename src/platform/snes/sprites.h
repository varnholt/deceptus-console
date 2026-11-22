/* logical source frames mapped to bounded SNES OBJ tiles and palette banks. */
#include "deceptus/appearance.h"
#include "deceptus/enemies.h"
#include "snes_headtorch_resources.h"
#include "snes_sword_resources.h"
#include <string.h>

#define SNES_OBJ_REQUESTS 96
#define SNES_OBJ_PIECES 128

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

typedef struct SnesObjSlot
{
   DeceptusU16 resource;
   DeceptusU8 piece, valid, used, held, pending, next;
} SnesObjSlot;

static SnesObjRequest snes_obj_requests[SNES_OBJ_REQUESTS];
static DeceptusU16 snes_obj_active_resources[SNES_OBJ_REQUESTS];
SnesObjSlot snes_obj_slots[SNES_OBJ_PIECES];
typedef char SnesObjSlotSize[(sizeof(SnesObjSlot) == 8) ? 1 : -1];
/* the resource lists are walked by snesObjSlotFor in sprites_resource.asm. */
DeceptusU8 snes_obj_heads[SNES_SPRITE_RESOURCE_COUNT];
static DeceptusU8 snes_obj_epochs[SNES_SPRITE_RESOURCE_COUNT];
static DeceptusU8 snes_obj_epoch;
static DeceptusU16 snes_obj_unused, snes_obj_free_cursor;
DeceptusU8 snes_obj_oam[544];
DeceptusU8 snes_obj_lines[224], snes_obj_slivers[224];
static const DeceptusU16 *snes_obj_palettes[8];
static DeceptusU16 snes_obj_request_count, snes_obj_palette_count;
DeceptusU16 snes_obj_count;
static DeceptusU8 snes_sprite_room_load, snes_sprite_blank;
DeceptusU8 snes_sprite_overflow;
static DeceptusU8 snes_sprite_initialized;
static DeceptusU16 snes_sprite_upload_bytes;

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

#ifdef SNES_SPRITES_ASM
/* sprites_resource.asm finds resident pieces itself and calls the c function
 * only to allocate a missing one; the lookup repeats inside it unchanged. */
typedef char SnesObjSlotSize[(sizeof(SnesObjSlot) == 8) ? 1 : -1];
DeceptusU16 snesObjSlotFor(DeceptusU16 resource, DeceptusU16 piece);
#define SNES_OBJ_SLOT_ALLOCATE snesObjSlotAllocate
#else
#define SNES_OBJ_SLOT_ALLOCATE snesObjSlotFor
#endif

DeceptusU16 SNES_OBJ_SLOT_ALLOCATE(DeceptusU16 resource, DeceptusU16 piece)
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
   DeceptusU16 object, high, shift;
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
   for (line = top; line < bottom; ++line)
   {
      if (snes_obj_lines[line] >= 32)
      {
         snes_sprite_overflow = 1;
         return;
      }
      if (snes_obj_slivers[line] + size / 8 > 34)
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
      ++snes_obj_lines[line];
      snes_obj_slivers[line] += size / 8;
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

#endif
#ifdef SNES_SPRITES_ASM
void snesObjPiece(DeceptusI16 x, DeceptusI16 y, DeceptusU16 tile, DeceptusU8 palette, DeceptusU8 flip, DeceptusU8 priority,
                  DeceptusU8 size);
#else
#define snesObjPiece snesObjPieceReference
#endif

#if !defined(SNES_SPRITES_ASM) || defined(SNES_SPRITES_REFERENCE)
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
#ifdef SNES_SPRITES_ASM
void snesObjEmitQuads(DeceptusI16 x, DeceptusI16 y, DeceptusU16 slot, DeceptusU8 palette, DeceptusU8 flip, DeceptusU8 priority,
                      DeceptusU8 quads);
#else
#define snesObjEmitQuads snesObjEmitQuadsReference
#endif

#if !defined(SNES_SPRITES_ASM) || defined(SNES_SPRITES_REFERENCE)
static void snesObjBeginReference(void)
{
   DeceptusU16 index;
   memset(snes_obj_oam, 0, sizeof(snes_obj_oam));
   memset(snes_obj_lines, 0, sizeof(snes_obj_lines));
   memset(snes_obj_slivers, 0, sizeof(snes_obj_slivers));
   for (index = 0; index < SNES_OBJ_PIECES; ++index)
   {
      snes_obj_oam[index * 4 + 1] = 240;
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
typedef char SnesObjResourceSize[(sizeof(SnesSpriteResource) == 16) ? 1 : -1];
typedef char SnesObjRequestSize[(sizeof(SnesObjRequest) == 8) ? 1 : -1];
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

static DeceptusU8 snesObjAppearance(void *context, const DeceptusSpriteAppearance *sprite)
{
   static const DeceptusU16 first[DECEPTUS_SPRITE_FAMILY_COUNT] = {
      SNES_ACTOR_FIRST,     SNES_LEVER_FIRST,         SNES_SPIKES_FIRST,    SNES_BLOCK_FIRST,          SNES_COLLAPSE_FIRST,
      SNES_RAT_FIRST,       SNES_TRAP_FIRST,          SNES_MUZZLE_FIRST,    SNES_ARROW_FIRST,          SNES_SWIM_FIRST,
      SNES_BLOB_FIRST,      SNES_SPLASH_FIRST,        SNES_SWORD_FIRST,     SNES_CANNON_FIRST,         SNES_CANNONBALL_FIRST,
      SNES_IRON_DOOR_FIRST, SNES_WOODEN_PLANKS_FIRST, SNES_HEADTORCH_FIRST, SNES_HEADTORCH_BEAM_FIRST, SNES_ORB_FIRST,
      SNES_FROG_BLUE_FIRST, SNES_BLUE_FIREBALL_FIRST, SNES_GRAB_ROPE_FIRST, SNES_PLAYER_ROPE_FIRST};
   (void)context;
   if (sprite->family >= DECEPTUS_SPRITE_FAMILY_COUNT)
   {
      return 0;
   }
   snesObjAdd(first[sprite->family] + sprite->frame, sprite->x, sprite->y, sprite->flip_x,
              sprite->family == DECEPTUS_SPRITE_GRAB_ROPE ? 3 : 2);
   return 1;
}

static void snesSpritesInit(void)
{
   DeceptusU16 index;
   if (!snes_sprite_initialized)
   {
      snesObjResetCache();
      snes_sprite_initialized = 1;
   }
   for (index = 0; index < SNES_OBJ_PIECES; ++index)
   {
      snes_obj_slots[index].used = snes_obj_slots[index].held = 0;
   }
   oamInitGfxAttr(0x6000, OBJ_SIZE8_L16);
   oamClear(0, 0);
   snes_sprite_room_load = 1;
}

static void snesSpritesPrepare(const DeceptusAdventure *game, const DeceptusFrame *frame)
{
   DeceptusU16 index, other, resource, active_count;
   DeceptusU8 palette, link;
   static DeceptusPlayerAnimation animation = DECEPTUS_PLAYER_ANIMATION_INITIALIZER;
   static const DeceptusAppearanceDef appearance = {
      {snes_sword_clips, snes_sword_durations, sizeof(snes_sword_durations), sizeof(snes_sword_clips) / sizeof(snes_sword_clips[0])},
      snes_headtorch_normal,
      snes_headtorch_swim,
      snes_headtorch_rope,
      snes_headtorch_sword,
      snes_headtorch_lamp_offsets};
   const SnesSpriteResource *entry;
   const SnesObjRequest *request;
   SNES_OBJ_PROFILE(1);
   snes_obj_request_count = snes_obj_count = snes_obj_palette_count = 0;
   snes_sprite_overflow = snes_sprite_blank = 0;
   snes_obj_free_cursor = 0;
   snesObjBegin();
   SNES_OBJ_PROFILE(2);
   if (!deceptusAppearanceVisit(game, frame, &appearance, &animation, snesObjAppearance, 0))
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
   SNES_OBJ_PROFILE(8);
}

static void snesSpritesPreload(void)
{
   DeceptusU16 index, tile, budget;
   const DeceptusU8 *graphics;
   SnesObjSlot *slot;
   budget = 0;
   snes_sprite_upload_bytes = 0;
   if (snes_sprite_blank)
   {
      setScreenOff();
   }
   for (index = 0; index < SNES_OBJ_PIECES; ++index)
   {
      slot = &snes_obj_slots[index];
      if (!slot->pending)
      {
         continue;
      }
      if (!snes_sprite_room_load && !snes_sprite_blank && (!budget || budget >= 2048))
      {
         WaitForVBlank();
         budget = 0;
      }
      tile = 0x6000 + snesObjTile(index) * 16;
      graphics = snes_sprite_resources[slot->resource].graphics + slot->piece * 128;
      dmaCopyVram((u8 *)graphics, tile, 64);
      dmaCopyVram((u8 *)(graphics + 64), tile + 256, 64);
      slot->pending = 0;
      budget += 128;
      snes_sprite_upload_bytes += 128;
   }
   snes_sprite_room_load = 0;
}

#ifdef SNES_SPRITES_ASM
void snesObjPublish(void);
#endif

static void snesSpritesCommit(void)
{
   DeceptusU16 index;
   for (index = 0; index < snes_obj_palette_count; ++index)
   {
      dmaCopyCGram((u8 *)snes_obj_palettes[index], 128 + index * 16, 32);
   }
   dmaCopyOAram(snes_obj_oam, 0, sizeof(snes_obj_oam));
   /* the next SDK VBlank publishes this only after every new tile is ready. */
#ifdef SNES_SPRITES_ASM
   snesObjPublish();
#else
   memcpy(oamMemory, snes_obj_oam, sizeof(snes_obj_oam));
   for (index = 0; index < SNES_OBJ_PIECES; ++index)
   {
      snes_obj_slots[index].held = snes_obj_slots[index].used;
   }
#endif
}
