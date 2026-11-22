#ifndef DECEPTUS_SNES_OPENING
#include "asset_sizes.h"
#include "deceptus/game.h"
#include "deceptus/pacing.h"
#include <snes.h>

extern char room_patterns, room_patterns_end;
extern char overflow_patterns, overflow_patterns_end;
extern char room_palette, room_palette_end;
extern char room_map, room_map_end;
extern char overflow_map, overflow_map_end;
extern char actor_patterns, actor_patterns_end;
extern char actor_palette, actor_palette_end;
extern const DeceptusRoom deceptus_scene;

static DeceptusEngine engine;
static DeceptusFrame frame;
#ifndef SNES_PACING_MAX_TICKS
#define SNES_PACING_MAX_TICKS 1
#endif
static DeceptusPacing pacing;
static DeceptusVideoCaps video = {256, 224, 12};
static DeceptusI16 accepted_top[DECEPTUS_DRAW_CAPACITY];
static DeceptusI16 accepted_bottom[DECEPTUS_DRAW_CAPACITY];

/* a stable little-endian debug mailbox for emulator integration tests. */
volatile DeceptusU16 deceptus_debug[16];

static DeceptusU16 inputRead(void)
{
   DeceptusU16 pad, buttons;
   pad = padsCurrent(0);
   buttons = 0;
   if (pad & KEY_LEFT)
   {
      buttons |= DECEPTUS_LEFT;
   }
   if (pad & KEY_RIGHT)
   {
      buttons |= DECEPTUS_RIGHT;
   }
   if (pad & (KEY_B | KEY_A))
   {
      buttons |= DECEPTUS_JUMP;
   }
   if (pad & KEY_UP)
   {
      buttons |= DECEPTUS_UP;
   }
   if (pad & KEY_START)
   {
      buttons |= DECEPTUS_PAUSE;
   }
   if (pad & KEY_SELECT)
   {
      buttons |= DECEPTUS_RESTART;
   }
   return buttons;
}

static void videoInit(void)
{
   setScreenOff();
   bgInitTileSet(0, (u8 *)&room_patterns, (u8 *)&room_palette, 0, ROOM_PATTERN_BYTES, ROOM_PALETTE_BYTES, BG_16COLORS, 0x1000);
   bgInitMapSet(0, (u8 *)&room_map, 4096, SC_64x32, 0x0000);
   bgInitTileSet(1, (u8 *)&overflow_patterns, (u8 *)&room_palette, 0, OVERFLOW_PATTERN_BYTES, ROOM_PALETTE_BYTES, BG_16COLORS, 0x5000);
   bgInitMapSet(1, (u8 *)&overflow_map, 4096, SC_64x32, 0x0800);
   oamInitGfxSet((u8 *)&actor_patterns, ACTOR_PATTERN_BYTES, (u8 *)&actor_palette, 64, 0, 0x6000, OBJ_SIZE16_L32);
   setMode(BG_MODE1, 0);
   bgSetDisable(2);
   bgSetScroll(0, 0, 0);
   bgSetScroll(1, 0, 0);
   oamClear(0, 0);
   setScreenOn();
}

static void videoPrepare(void)
{
   DeceptusU16 index, other, accepted, overlaps, object_id, tile_base, piece;
   DeceptusI16 x, y;
   const DeceptusDraw *draw;
   oamClear(0, 0);
   object_id = 0;
   accepted = 0;
   for (index = 0; index < frame.count; ++index)
   {
      draw = &frame.draws[index];
      overlaps = 0;
      for (other = 0; other < accepted; ++other)
      {
         if (draw->y < accepted_bottom[other] && draw->y + 48 > accepted_top[other])
         {
            ++overlaps;
         }
      }
      /* conservative interval budget: at most 8 actors overlap, hence <=16
       * objects and <=32 eight-pixel slivers on any scanline. */
      if (overlaps >= 8 || object_id > 128 * 4 - 6 * 4)
      {
         continue;
      }
      accepted_top[accepted] = draw->y;
      accepted_bottom[accepted] = draw->y + 48;
      ++accepted;
      tile_base = (draw->frame % 4) * 4 + (draw->frame / 4) * 96;
      for (piece = 0; piece < 6; ++piece)
      {
         x = draw->x + (draw->flip_x ? 1 - piece % 2 : piece % 2) * 16;
         y = draw->y + (piece / 2) * 16;
         /* skip vertically clipped pieces to prevent 8-bit OAM y wrapping. */
         if (y <= -16 || y >= 224 || x <= -16 || x >= 256)
         {
            continue;
         }
         oamSet(object_id, x, y, 3, draw->flip_x, 0, tile_base + (piece / 2) * 32 + (piece % 2) * 2, draw->palette);
         oamSetEx(object_id, OBJ_SMALL, OBJ_SHOW);
         object_id += 4;
      }
   }
}

static void debugWrite(void)
{
   deceptus_debug[0] = 0xD3CE;
   deceptus_debug[1] = deceptusFloor(engine.entities[0].x);
   deceptus_debug[2] = deceptusFloor(engine.entities[0].y);
   deceptus_debug[3] = engine.camera_x;
   deceptus_debug[4] = engine.ticks;
   deceptus_debug[5] = engine.paused;
   deceptus_debug[6] = engine.health;
   deceptus_debug[7] = engine.entities[0].grounded;
   deceptus_debug[8] = engine.door_entries;
   deceptus_debug[9] = engine.event_count;
   deceptus_debug[10] = frame.count;
   deceptus_debug[11] = engine.entities[1].active;
   deceptus_debug[12] = engine.checkpoint_id;
   deceptus_debug[13] = engine.checkpoint_x;
   deceptus_debug[14] = engine.checkpoint_y;
   deceptus_debug[15] = engine.content_error;
}

int main(void)
{
   DeceptusU16 previous_vblank, current_vblank, buttons;
   DeceptusU8 ticks;
   deceptusInit(&engine, &deceptus_scene, &video);
   videoInit();
   deceptusPacingInit(&pacing, snes_50hz ? 50 : 60, SNES_PACING_MAX_TICKS);
   previous_vblank = snes_vblank_count - 1;
   while (1)
   {
      current_vblank = snes_vblank_count;
      ticks = deceptusPacingTicks(&pacing, current_vblank - previous_vblank);
      previous_vblank = current_vblank;
      buttons = inputRead();
      while (ticks)
      {
         deceptusTick(&engine, buttons);
         --ticks;
      }
      deceptusRender(&engine, &frame);
      videoPrepare();
      debugWrite();
      WaitForVBlank();
      /* only register writes occur here; all OAM work is buffered above. */
      bgSetScroll(0, frame.camera_x, frame.camera_y);
      bgSetScroll(1, frame.camera_x, frame.camera_y);
      setBrightness(engine.paused ? 7 : 15);
   }
   return 0;
}
#else
#include "deceptus/adventure.h"
#include "deceptus/pacing.h"
#include "snes_assets.h"
#include <snes.h>

extern const DeceptusAdventureDef snes_tutorial_definition;
static DeceptusAdventure adventure;
static DeceptusFrame frame;
#ifndef SNES_PACING_MAX_TICKS
#define SNES_PACING_MAX_TICKS 1
#endif
static DeceptusPacing pacing;
static const DeceptusVideoCaps video = {256, 224, DECEPTUS_DRAW_CAPACITY};
volatile DeceptusU16 deceptus_debug[56];

#include "background.h"
#include "sprites.h"
#include "ui.h"

static DeceptusU16 inputRead(void)
{
   DeceptusU16 pad, buttons;
   pad = padsCurrent(0);
   buttons = 0;
   if (pad & KEY_LEFT)
   {
      buttons |= DECEPTUS_LEFT;
   }
   if (pad & KEY_RIGHT)
   {
      buttons |= DECEPTUS_RIGHT;
   }
   if (pad & KEY_A)
   {
      buttons |= DECEPTUS_JUMP | DECEPTUS_ACCEPT;
   }
   if (pad & KEY_B)
   {
      buttons |= DECEPTUS_JUMP | DECEPTUS_BACK;
   }
   if (pad & KEY_UP)
   {
      buttons |= DECEPTUS_UP;
   }
   if (pad & KEY_DOWN)
   {
      buttons |= DECEPTUS_DOWN;
   }
   if (pad & KEY_START)
   {
      buttons |= DECEPTUS_PAUSE;
   }
   if (pad & KEY_SELECT)
   {
      buttons |= DECEPTUS_RESTART;
   }
   if (pad & (KEY_X | KEY_Y))
   {
      buttons |= DECEPTUS_ATTACK;
   }
   if (pad & KEY_L)
   {
      buttons |= DECEPTUS_ACCEPT;
   }
   if (pad & KEY_R)
   {
      buttons |= DECEPTUS_BACK;
   }
   return buttons;
}

static void debugWrite(void)
{
   const DeceptusEngine *engine;
   engine = &adventure.campaign.engine;
   deceptus_debug[0] = 0xD3CE;
   deceptus_debug[1] = deceptusFloor(engine->entities[0].x);
   deceptus_debug[2] = deceptusFloor(engine->entities[0].y);
   deceptus_debug[3] = engine->camera_x;
   deceptus_debug[4] = engine->ticks;
   deceptus_debug[5] = engine->paused;
   deceptus_debug[6] = engine->health;
   deceptus_debug[7] = engine->entities[0].grounded;
   deceptus_debug[8] = engine->door_entries;
   deceptus_debug[9] = engine->event_count;
   deceptus_debug[10] = frame.count;
   deceptus_debug[12] = engine->checkpoint_id;
   deceptus_debug[13] = engine->checkpoint_x;
   deceptus_debug[14] = engine->checkpoint_y;
   deceptus_debug[15] = engine->content_error;
   deceptus_debug[16] = engine->camera_y;
   deceptus_debug[17] = snes_background_error;
   deceptus_debug[18] = snes_background_uploads;
   deceptus_debug[19] = snes_background_full;
   deceptus_debug[20] = snes_ui_overflow;
   deceptus_debug[21] = snes_sprite_overflow;
   deceptus_debug[22] = snes_sprite_upload_bytes;
   deceptus_debug[23] = snes_obj_count;
   deceptus_debug[24] = snes_obj_palette_count;
   deceptus_debug[25] = snes_sprite_blank;
   deceptus_debug[26] = engine->previous_buttons;
   deceptus_debug[27] = padsCurrent(0);
   deceptus_debug[28] = adventure.campaign.room_index;
   deceptus_debug[29] = adventure.campaign.mode;
   deceptus_debug[30] = adventure.campaign.deaths;
   deceptus_debug[31] = adventure.campaign.seconds;
   deceptus_debug[32] = engine->projectiles_fired;
   deceptus_debug[33] = adventure.campaign.restarts;
   deceptus_debug[36] = adventure.flags;
   deceptus_debug[37] = adventure.inventory.counts[1];
   deceptus_debug[38] = adventure.inventory.counts[2];
   deceptus_debug[39] = adventure.page;
   deceptus_debug[40] = engine->entities[0].velocity_x;
   deceptus_debug[41] = engine->entities[0].velocity_y;
   deceptus_debug[42] = engine->entities[0].x;
   deceptus_debug[43] = engine->entities[0].y;
   deceptus_debug[44] = adventure.campaign.previous_buttons;
   deceptus_debug[46] = adventure.inventory.counts[3];
   deceptus_debug[47] = adventure.inventory.counts[4];
   deceptus_debug[48] = adventure.inventory.counts[5];
   deceptus_debug[49] = adventure.inventory.counts[6];
   deceptus_debug[50] = adventure.pending_ticks;
   deceptus_debug[51] = adventure.has_sword;
   deceptus_debug[52] = adventure.inventory.counts[7];
   deceptus_debug[53] = engine->player_rope.attached;
   deceptus_debug[54] = engine->player_rope.rope_index;
   deceptus_debug[55] = engine->player_rope.node_index;
}

/* setScreenOn waits for a further VBlank, so it is only worth paying after
 * the screen was actually blanked. */
static DeceptusU8 screen_blanked;

static void videoLoad(void)
{
   deceptus_debug[11] = 3;
   setScreenOff();
   screen_blanked = 1;
   snesBackgroundLoad(adventure.campaign.room_index);
   deceptus_debug[11] = 4;
   snesSpritesInit();
   deceptus_debug[11] = 5;
   snesUiInit();
   deceptus_debug[11] = 6;
}

int main(void)
{
   DeceptusU16 scenery_flags, buttons, previous_vblank, current_vblank;
   DeceptusU8 ticks, room_loaded;
   deceptus_debug[0] = 0xD3CE;
   deceptus_debug[11] = 1;
   deceptusAdventureInit(&adventure, &snes_tutorial_definition, &video);
   deceptus_debug[11] = 2;
   videoLoad();
   deceptus_debug[11] = 7;
   deceptusPacingInit(&pacing, snes_50hz ? 50 : 60, SNES_PACING_MAX_TICKS);
   previous_vblank = snes_vblank_count - 1;
   room_loaded = 0;
   while (1)
   {
      deceptus_debug[11] = 8;
      current_vblank = snes_vblank_count;
      /* a room load is not made up for. */
      ticks = deceptusPacingTicks(&pacing, room_loaded ? 1 : current_vblank - previous_vblank);
      previous_vblank = current_vblank;
      room_loaded = 0;
      buttons = inputRead();
      while (ticks)
      {
         --ticks;
         if (deceptusAdventureTick(&adventure, buttons))
         {
            videoLoad();
            deceptusPacingReset(&pacing);
            room_loaded = 1;
            break;
         }
      }
      deceptus_debug[11] = 9;
      deceptusRender(&adventure.campaign.engine, &frame);
      deceptus_debug[11] = 10;
      scenery_flags = deceptusAdventurePresentationFlags(&adventure);
      snesBackgroundSetFlags(scenery_flags);
      snesBackgroundPrepare(frame.camera_x, frame.camera_y);
      deceptus_debug[11] = 11;
      snesSpritesPrepare(&adventure, &frame);
      deceptus_debug[11] = 12;
      snesUiPrepare(&adventure);
      deceptus_debug[11] = 13;
      snesSpritesPreload(snes_background_dma_bytes);
      deceptus_debug[11] = 14;
      debugWrite();
      /* phase 15 tells emulator checks the packet is complete. it used to be
       * observable for most of a display frame while setScreenOn waited; the
       * commits now fit in VBlank, so the wait for it is marked as well. */
      deceptus_debug[11] = 15;
      if (snes_sprite_blank)
      {
         screen_blanked = 1;
      }
      if (snes_background_full)
      {
         setScreenOff();
         screen_blanked = 1;
      }
      WaitForVBlank();
      deceptus_debug[11] = 15;
      /* every video transfer first, while vblank lasts. a frame whose transfers exceed one vblank
         gives the sprites the next one; their tiles and the oam that refers to them stay together. */
      snesBackgroundCommit();
      if (!screen_blanked && snes_background_dma_bytes + snesSpritesCommitBytes() > SNES_COMMIT_DMA_BUDGET)
      {
         WaitForVBlank();
      }
      snesSpritesCommitTransfers();
      deceptus_debug[11] = 16;
      snesSpritesPublish();
      deceptus_debug[11] = 17;
      snesUiCommit();
      deceptus_debug[11] = 18;
      if (screen_blanked)
      {
         setScreenOn();
         screen_blanked = 0;
      }
      deceptus_debug[11] = 19;
   }
   return 0;
}
#endif
