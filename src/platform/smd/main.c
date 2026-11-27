#include <genesis.h>
#include "deceptus/game.h"
#include "deceptus/pacing.h"
#include "smd_assets.h"

extern const DeceptusRoom deceptus_scene;
typedef char SmdEngineMemoryLength[sizeof(DeceptusEngine) < 65536UL ? 1 : -1];
static DeceptusEngine engine;
static DeceptusFrame frame;
#ifndef SMD_PACING_MAX_TICKS
#define SMD_PACING_MAX_TICKS 1
#endif
static DeceptusPacing pacing;
static const DeceptusVideoCaps video = {320, 224, 12};
static DeceptusU8 scanlines[224];
static DeceptusU16 object_count, sprite_error, sequence;
volatile DeceptusU16 smd_debug[23];

static DeceptusU16 inputRead(void)
{
   DeceptusU16 pad, buttons;
   pad = JOY_readJoypad(JOY_1);
   buttons = 0;
   if (pad & BUTTON_LEFT)
   {
      buttons |= DECEPTUS_LEFT;
   }
   if (pad & BUTTON_RIGHT)
   {
      buttons |= DECEPTUS_RIGHT;
   }
   if (pad & (BUTTON_B | BUTTON_C))
   {
      buttons |= DECEPTUS_JUMP;
   }
   if (pad & BUTTON_UP)
   {
      buttons |= DECEPTUS_UP;
   }
   if (pad & BUTTON_START)
   {
      buttons |= DECEPTUS_PAUSE;
   }
   if (pad & BUTTON_A)
   {
      buttons |= DECEPTUS_RESTART;
   }
   return buttons;
}

static void loadTiles(const DeceptusU32* data, DeceptusU16 first, DeceptusU16 count)
{
   DeceptusU16 prefix;
   prefix = first < 1536 ? 1536 - first : 0;
   if (prefix > count)
   {
      prefix = count;
   }
   if (prefix)
   {
      VDP_loadTileData(data, first, prefix, DMA);
   }
   if (count > prefix)
   {
      VDP_loadTileData(data + prefix * 8, first + prefix + 128, count - prefix, DMA);
   }
}

static void videoInit(void)
{
   SYS_disableInts();
   VDP_setEnable(FALSE);
   VDP_setScreenWidth320();
   VDP_setPlaneSize(64, 32, TRUE);
   VDP_setBGAAddress(0xc000);
   VDP_setBGBAddress(0xe000);
   VDP_setSpriteListAddress(0xf000);
   VDP_setHScrollTableAddress(0xf400);
   VDP_setWindowHPos(FALSE, 0);
   VDP_setWindowVPos(FALSE, 0);
   /* this poc uses no window or sdk text; its font tiles may hold artwork. */
   VDP_clearPlane(BG_A, TRUE);
   VDP_clearPlane(BG_B, TRUE);
   PAL_setColors(0, smd_palette, 64, CPU);
   loadTiles(smd_background_tiles, 1, SMD_BACKGROUND_TILES);
   loadTiles(smd_actor_tiles, SMD_ACTOR_FIRST, SMD_ACTOR_TILES);
   VDP_setTileMapDataRect(BG_A, smd_room_map, 0, 0, 64, 28, 64, CPU);
   VDP_resetSprites();
   VDP_setEnable(TRUE);
   SYS_enableInts();
}

static void videoPrepare(void)
{
   const DeceptusDraw* draw;
   DeceptusU16 index, piece, tile;
   DeceptusI16 x, y, top, bottom, line;
   object_count = sprite_error = 0;
   memset(scanlines, 0, sizeof(scanlines));
   for (index = 0; index < frame.count; ++index)
   {
      draw = &frame.draws[index];
      if (draw->frame >= 26)
      {
         sprite_error = 1;
         break;
      }
      for (piece = 0; piece < 6; ++piece)
      {
         tile = smd_frame_tiles[draw->frame * 6 + piece];
         if (tile == 65535)
         {
            continue;
         }
         x = draw->x + (draw->flip_x ? 1 - piece % 2 : piece % 2) * 16;
         y = draw->y + (piece / 2) * 16;
         if (x <= -16 || x >= 320 || y <= -16 || y >= 224)
         {
            continue;
         }
         if (object_count >= 80)
         {
            sprite_error = 1;
            break;
         }
         top = y < 0 ? 0 : y;
         bottom = y + 16 > 224 ? 224 : y + 16;
         for (line = top; line < bottom; ++line)
         {
            if (scanlines[line] >= 20)
            {
               sprite_error = 1;
               break;
            }
         }
         if (sprite_error)
         {
            break;
         }
         for (line = top; line < bottom; ++line)
         {
            ++scanlines[line];
         }
         VDP_setSpriteFull(object_count, x, y, SPRITE_SIZE(2, 2), TILE_ATTR_FULL(3, 1, 0, draw->flip_x, tile), object_count + 1);
         ++object_count;
      }
   }
   if (object_count)
   {
      VDP_setSpriteLink(object_count - 1, 0);
      VDP_updateSprites(object_count, DMA_QUEUE);
   }
   else
   {
      VDP_setSpriteFull(0, 0, 240, SPRITE_SIZE(1, 1), 0, 0);
      VDP_updateSprites(1, DMA_QUEUE);
   }
   VDP_setHorizontalScrollVSync(BG_A, -frame.camera_x);
   VDP_setVerticalScrollVSync(BG_A, frame.camera_y);
}

static void debugWrite(DeceptusU16 buttons)
{
   smd_debug[0] = 0xd3ce;
   smd_debug[1] = 0x4d44;
   smd_debug[2] = ++sequence;
   smd_debug[3] = engine.ticks;
   smd_debug[4] = deceptusFloor(engine.entities[0].x);
   smd_debug[5] = deceptusFloor(engine.entities[0].y);
   smd_debug[6] = engine.camera_x;
   smd_debug[7] = engine.camera_y;
   smd_debug[8] = engine.paused;
   smd_debug[9] = engine.health;
   smd_debug[10] = engine.door_entries;
   smd_debug[11] = engine.checkpoint_id;
   smd_debug[12] = engine.content_error;
   smd_debug[13] = sprite_error;
   smd_debug[14] = frame.count;
   smd_debug[15] = object_count;
   smd_debug[16] = buttons;
   smd_debug[17] = (DeceptusU16)vtimer;
   smd_debug[18] = SYS_getCPULoad();
   smd_debug[19] = engine.entities[0].grounded;
   smd_debug[20] = engine.checkpoint_x;
   smd_debug[21] = engine.checkpoint_y;
   smd_debug[22] = ++sequence;
   smd_debug[2] = sequence;
}

int main(bool hard_reset)
{
   DeceptusU32 previous_vblank;
   DeceptusU16 buttons;
   DeceptusU8 ticks;
   (void)hard_reset;
   deceptusInit(&engine, &deceptus_scene, &video);
   videoInit();
   deceptusPacingInit(&pacing, IS_PAL_SYSTEM ? 50 : 60, SMD_PACING_MAX_TICKS);
   previous_vblank = vtimer - 1;
   while (1)
   {
      ticks = deceptusPacingTicks(&pacing, (DeceptusU16)(vtimer - previous_vblank));
      previous_vblank = vtimer;
      buttons = inputRead();
      while (ticks)
      {
         deceptusTick(&engine, buttons);
         --ticks;
      }
      deceptusRender(&engine, &frame);
      videoPrepare();
      debugWrite(buttons);
      SYS_doVBlankProcess();
   }
   return 0;
}
