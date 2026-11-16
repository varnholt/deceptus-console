/* logical source frames mapped to bounded SNES OBJ tiles and palette banks. */
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
static DeceptusU8 snes_obj_heads[SNES_SPRITE_RESOURCE_COUNT], snes_obj_epochs[SNES_SPRITE_RESOURCE_COUNT];
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

static DeceptusU16 snesObjSword(const DeceptusAdventure *game, DeceptusI16 *x, DeceptusI16 *y)
{
   static DeceptusU8 previous_pose = 255;
   static DeceptusU16 previous_tick, pose_ticks;
   const DeceptusEntity *player;
   const DeceptusU8 *clip;
   DeceptusU16 step, duration, elapsed, delta;
   DeceptusU8 pose, left;
   player = &game->campaign.engine.entities[0];
   left = player->facing_left;
   if (game->sword.ticks)
   {
      pose = game->sword.airborne ? 10 : game->sword.crouched ? 9 : 8;
      left = game->sword.facing_left;
   }
   else if (game->campaign.engine.player_rope.attached)
   {
      pose = 3;
   }
   else if (player->in_water)
   {
      pose = player->velocity_x ? 7 : 6;
   }
   else if (player->crouched)
   {
      pose = 5;
   }
   else if (!player->grounded)
   {
      pose = player->velocity_y < -16 ? 2 : player->velocity_y > 16 ? 4 : 3;
   }
   else
   {
      pose = player->velocity_x ? 1 : 0;
   }
   pose += left ? 11 : 0;
   delta = game->campaign.engine.ticks - previous_tick;
   pose_ticks = pose != previous_pose || delta > 4 ? 0 : pose_ticks + delta;
   previous_pose = pose;
   previous_tick = game->campaign.engine.ticks;
   clip = snes_sword_clips[pose];
   duration = 0;
   for (step = 0; step < clip[1]; ++step)
   {
      duration += snes_sword_durations[clip[0] + step];
   }
   while (pose_ticks >= duration)
   {
      pose_ticks -= duration;
   }
   elapsed = game->sword.ticks ? game->sword.ticks - 1 : pose_ticks;
   for (step = 0; step + 1 < clip[1] && elapsed >= snes_sword_durations[clip[0] + step]; ++step)
   {
      elapsed -= snes_sword_durations[clip[0] + step];
   }
   *x = deceptusFloor(player->x) - game->campaign.engine.camera_x - clip[2];
   *y = deceptusFloor(player->y) - game->campaign.engine.camera_y - clip[3];
   return clip[0] + step;
}

static void snesObjPlayer(const DeceptusAdventure *game, const DeceptusDraw *draw)
{
   const SnesHeadtorchPose *pose;
   const DeceptusEngine *engine;
   DeceptusU16 resource, image;
   DeceptusI16 x, y, helmet_x, helmet_y;
   DeceptusU8 flip;
   engine = &game->campaign.engine;
   image = draw->frame;
   x = draw->x;
   y = draw->y;
   flip = draw->flip_x;
   if (game->has_sword)
   {
      image = snesObjSword(game, &x, &y);
      resource = SNES_SWORD_FIRST + image;
      pose = &snes_headtorch_sword[image];
      flip = 0;
   }
   else if (draw->kind == DECEPTUS_PLAYER_SWIM)
   {
      resource = SNES_SWIM_FIRST + image;
      pose = &snes_headtorch_swim[image];
   }
   else if (draw->kind == DECEPTUS_PLAYER_ROPE)
   {
      resource = SNES_PLAYER_ROPE_FIRST + image;
      pose = &snes_headtorch_rope[image];
   }
   else
   {
      resource = SNES_ACTOR_FIRST + image;
      pose = &snes_headtorch_normal[engine->entities[0].facing_left][image];
   }
   if (game->inventory.counts[5] && pose->valid)
   {
      helmet_x = deceptusFloor(engine->entities[0].x) - engine->camera_x + pose->x;
      helmet_y = deceptusFloor(engine->entities[0].y) - engine->camera_y + pose->y;
      snesObjAdd(SNES_HEADTORCH_FIRST + pose->side, helmet_x, helmet_y, 0, 2);
      snesObjAdd(SNES_HEADTORCH_BEAM_FIRST, helmet_x + snes_headtorch_lamp_offsets[pose->side][0] - (pose->side ? 64 : 0),
                 helmet_y + snes_headtorch_lamp_offsets[pose->side][1] - 16, pose->side, 2);
   }
   snesObjAdd(resource, x, y, flip, 2);
}

static void snesObjMechanisms(const DeceptusEngine *engine)
{
   const DeceptusMechanismDef *definition;
   const DeceptusMechanismState *state;
   DeceptusU16 index, resource, segment, segments;
   DeceptusI16 x, y;
   for (index = 0; index < engine->mechanisms.count; ++index)
   {
      definition = &engine->mechanisms.definitions[index];
      state = &engine->mechanisms.states[index];
      if (!state->visible)
      {
         continue;
      }
      segments = 1;
      x = definition->bounds.x - engine->camera_x;
      y = definition->bounds.y - engine->camera_y;
      if (definition->kind == DECEPTUS_MECH_LEVER)
      {
         resource = SNES_LEVER_FIRST + (state->handle_available ? state->frame : 32);
      }
      else if (definition->kind == DECEPTUS_MECH_BLOCK && engine->mechanisms.timings &&
               (engine->mechanisms.timings[index].flags & DECEPTUS_TIMED_ANIMATION))
      {
         resource = SNES_BLOCK_FIRST + state->frame;
      }
      else if (definition->kind == DECEPTUS_MECH_SPIKES)
      {
         resource = SNES_SPIKES_FIRST + state->frame;
      }
      else if (definition->kind == DECEPTUS_MECH_COLLAPSING)
      {
         if (state->frame >= 38 || state->phase == 2)
         {
            continue;
         }
         resource = SNES_COLLAPSE_FIRST + state->frame;
         segments = definition->bounds.width / 24;
         y -= 24;
         if (state->phase == 1)
         {
            y += state->timer / 10;
         }
         else if (state->timer)
         {
            y += (DeceptusI16)((state->timer / 2) & 3) - 1;
         }
      }
      else if (definition->kind == DECEPTUS_MECH_DOOR)
      {
         resource = SNES_IRON_DOOR_FIRST + state->frame;
         x -= 24;
         y -= 24;
      }
      else if (definition->kind == DECEPTUS_MECH_DESTRUCTIBLE)
      {
         resource = SNES_WOODEN_PLANKS_FIRST + state->frame;
         if (definition->flags & DECEPTUS_MECH_RIGHT_ALIGNED)
         {
            resource += 50;
            x -= 102;
         }
      }
      else
      {
         continue;
      }
      for (segment = 0; segment < segments; ++segment)
      {
         snesObjAdd(resource + (definition->kind == DECEPTUS_MECH_COLLAPSING ? (segment & 3) * 38 : 0), x + segment * 24, y, 0, 2);
      }
   }
}

static void snesObjRopes(const DeceptusEngine *engine)
{
   DeceptusU16 distance, length, height;
   DeceptusU8 rope;
   DeceptusI16 x, y;
   for (rope = 0; rope < engine->grab_ropes.count; ++rope)
   {
      length = deceptusGrabRopeLength(engine, rope);
      for (distance = 0; distance < length; distance += height)
      {
         height = distance ? 8 : 16;
         if (height > length - distance)
         {
            height = length - distance;
         }
         deceptusGrabRopePointAt(engine, rope, distance, &x, &y);
         snesObjAdd(SNES_GRAB_ROPE_FIRST + (distance ? height : 0), x - engine->camera_x - 7, y - engine->camera_y - (distance ? 4 : 0), 0,
                    3);
      }
   }
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
   const DeceptusDraw *draw;
   const SnesSpriteResource *entry;
   const SnesObjRequest *request;
   SNES_OBJ_PROFILE(1);
   snes_obj_request_count = snes_obj_count = snes_obj_palette_count = 0;
   snes_sprite_overflow = snes_sprite_blank = 0;
   snes_obj_free_cursor = 0;
   snesObjBegin();
   SNES_OBJ_PROFILE(2);
   if (game->campaign.mode != DECEPTUS_CAMPAIGN_TITLE)
   {
      for (index = 0; index < frame->count; ++index)
      {
         draw = &frame->draws[index];
         if (draw->kind == DECEPTUS_PLAYER || draw->kind == DECEPTUS_PLAYER_SWIM || draw->kind == DECEPTUS_PLAYER_ROPE)
         {
            snesObjPlayer(game, draw);
            continue;
         }
         if (draw->kind == DECEPTUS_RAT)
         {
            resource = SNES_RAT_FIRST;
         }
         else if (draw->kind == DECEPTUS_BLOB_2)
         {
            resource = SNES_BLOB_FIRST;
         }
         else if (draw->kind == DECEPTUS_ARROW_TRAP)
         {
            resource = SNES_TRAP_FIRST;
         }
         else if (draw->kind == DECEPTUS_ARROW_MUZZLE)
         {
            resource = SNES_MUZZLE_FIRST;
         }
         else if (draw->kind == DECEPTUS_ARROW)
         {
            resource = SNES_ARROW_FIRST;
         }
         else if (draw->kind == DECEPTUS_CANNON_2)
         {
            resource = SNES_CANNON_FIRST;
         }
         else if (draw->kind == DECEPTUS_CANNONBALL)
         {
            resource = SNES_CANNONBALL_FIRST;
         }
         else if (draw->kind == DECEPTUS_ORB)
         {
            resource = SNES_ORB_FIRST;
         }
         else if (draw->kind == DECEPTUS_FROG_BLUE)
         {
            resource = SNES_FROG_BLUE_FIRST;
         }
         else if (draw->kind == DECEPTUS_BLUE_FIREBALL)
         {
            resource = SNES_BLUE_FIREBALL_FIRST;
         }
         else if (draw->kind == DECEPTUS_WATER_SPLASH)
         {
            resource = SNES_SPLASH_FIRST;
         }
         else
         {
            snes_sprite_overflow = 1;
            continue;
         }
         snesObjAdd(resource + draw->frame, draw->x, draw->y, draw->flip_x, 2);
      }
      snesObjMechanisms(&game->campaign.engine);
      snesObjRopes(&game->campaign.engine);
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

static void snesSpritesCommit(void)
{
   DeceptusU16 index;
   for (index = 0; index < snes_obj_palette_count; ++index)
   {
      dmaCopyCGram((u8 *)snes_obj_palettes[index], 128 + index * 16, 32);
   }
   dmaCopyOAram(snes_obj_oam, 0, sizeof(snes_obj_oam));
   /* the next SDK VBlank publishes this only after every new tile is ready. */
   memcpy(oamMemory, snes_obj_oam, sizeof(snes_obj_oam));
   for (index = 0; index < SNES_OBJ_PIECES; ++index)
   {
      snes_obj_slots[index].held = snes_obj_slots[index].used;
   }
}
