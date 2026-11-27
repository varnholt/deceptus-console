#include "deceptus/adventure.h"
#include "deceptus/game.h"
#include "deceptus/pacing.h"
#include "opening_video.h"
#include <genesis.h>

static DeceptusAdventure adventure;
static DeceptusFrame frame;
static const DeceptusVideoCaps video = {320, 224, DECEPTUS_DRAW_CAPACITY};
static DeceptusU16 sequence;
/* the simulation runs at 60 ticks per second; a presentation that takes longer than a display frame
 * catches up with up to three ticks, as on the SNES and the Amiga, so the game keeps its speed down
 * to 20 presentations per second. */
#ifndef SMD_PACING_MAX_TICKS
#define SMD_PACING_MAX_TICKS 3
#endif
static DeceptusPacing pacing;
volatile DeceptusU16 smd_opening_packets[2][56];
static volatile DeceptusU16 *smd_opening_debug = smd_opening_packets[0];
static DeceptusU8 packet_slot;
/* the heap is only allocated from while a room loads, so its free size is sampled there instead
   of walking the block list for every published packet. */
static DeceptusU16 smd_free_memory;

#include "opening_sprites.h"
#include "opening_ui.h"

/* direct, bounded transfers own presentation; the sdk queue is not needed. */
void __wrap_DMA_init(void)
{
   DMA_setAutoFlush(FALSE);
   DMA_clearQueue();
}

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
   if (pad & BUTTON_DOWN)
   {
      buttons |= DECEPTUS_DOWN;
   }
   if (pad & BUTTON_UP)
   {
      buttons |= DECEPTUS_UP;
   }
   if (pad & BUTTON_B)
   {
      buttons |= DECEPTUS_JUMP;
   }
   if (pad & BUTTON_C)
   {
      buttons |= DECEPTUS_JUMP | DECEPTUS_BACK;
   }
   if (pad & BUTTON_START)
   {
      buttons |= DECEPTUS_PAUSE;
   }
   if (pad & BUTTON_MODE)
   {
      buttons |= DECEPTUS_RESTART;
   }
   if (pad & BUTTON_X)
   {
      buttons |= DECEPTUS_ACCEPT;
   }
   if (pad & BUTTON_Z)
   {
      buttons |= DECEPTUS_BACK;
   }
   if (pad & BUTTON_A)
   {
      buttons |= DECEPTUS_ATTACK;
   }
   return buttons;
}

static void videoLoad(void)
{
   VDP_setEnable(FALSE);
   smdBackgroundLoad(adventure.campaign.room_index);
   smdSpritesInit();
   smdUiInit();
   smd_free_memory = MEM_getFree();
}

static void debugWrite(DeceptusU16 buttons, DeceptusU8 blank)
{
   const DeceptusEngine *engine;
   engine = &adventure.campaign.engine;
   packet_slot ^= 1;
   smd_opening_debug = smd_opening_packets[packet_slot];
   smd_opening_debug[0] = 0xd3ce;
   smd_opening_debug[1] = 0x4d4f;
   smd_opening_debug[2] = ++sequence;
   smd_opening_debug[3] = engine->ticks;
   smd_opening_debug[4] = deceptusFloor(engine->entities[0].x);
   smd_opening_debug[5] = deceptusFloor(engine->entities[0].y);
   smd_opening_debug[6] = engine->camera_x;
   smd_opening_debug[7] = engine->camera_y;
   smd_opening_debug[8] = adventure.campaign.room_index;
   smd_opening_debug[9] = adventure.campaign.mode;
   smd_opening_debug[10] = adventure.flags;
   smd_opening_debug[11] = engine->health;
   smd_opening_debug[12] = adventure.campaign.deaths;
   smd_opening_debug[13] = engine->content_error;
   smd_opening_debug[14] = smd_background_error;
   smd_opening_debug[15] = smd_graphics_error;
   smd_opening_debug[16] = smd_sprite_overflow;
   smd_opening_debug[17] = smd_ui_overflow;
   smd_opening_debug[18] = adventure.has_sword;
   smd_opening_debug[19] = adventure.page;
   smd_opening_debug[20] = buttons;
   smd_opening_debug[21] = adventure.campaign.previous_buttons;
   smd_opening_debug[22] = smd_obj_count;
   smd_opening_debug[23] = smd_background_dma_bytes;
   smd_opening_debug[24] = smd_sprite_upload_bytes;
   smd_opening_debug[25] = smd_background_uploads;
   smd_opening_debug[26] = (DeceptusU16)vtimer;
   smd_opening_debug[27] = SYS_getCPULoad();
   smd_opening_debug[28] = engine->player_rope.attached;
   smd_opening_debug[29] = engine->entities[0].grounded;
   smd_opening_debug[30] = engine->projectiles_fired;
   smd_opening_debug[31] = engine->entities[0].velocity_x;
   smd_opening_debug[32] = engine->entities[0].velocity_y;
   smd_opening_debug[33] = engine->entities[0].x;
   smd_opening_debug[34] = engine->entities[0].y;
   smd_opening_debug[35] = adventure.pending_ticks;
   smd_opening_debug[36] = adventure.automatic_ticks;
   smd_opening_debug[37] = smd_free_memory;
   smd_opening_debug[38] = blank;
   smd_opening_debug[39] = smd_sprite_blank;
   smd_opening_debug[47] = smd_ui_dirty;
   smd_opening_debug[48] = smd_background_full;
   smd_opening_debug[40] = adventure.inventory.counts[0];
   smd_opening_debug[41] = adventure.inventory.counts[1];
   smd_opening_debug[42] = adventure.inventory.counts[2];
   smd_opening_debug[43] = adventure.inventory.counts[3];
   smd_opening_debug[44] = adventure.inventory.counts[4];
   smd_opening_debug[45] = adventure.inventory.counts[5];
   smd_opening_debug[46] = adventure.inventory.counts[6];
   smd_opening_debug[55] = ++sequence;
   smd_opening_debug[2] = sequence;
}

int main(bool hard_reset)
{
   DeceptusU32 previous_vblank;
   DeceptusU16 buttons, flags;
   DeceptusU8 blank, ticks, room_loaded;
   (void)hard_reset;
   VDP_setEnable(FALSE);
   VDP_setScreenWidth320();
   VDP_setPlaneSize(64, 32, TRUE);
   VDP_setBGAAddress(0xc000);
   VDP_setBGBAddress(0xe000);
   VDP_setWindowAddress(0xb000);
   VDP_setSpriteListAddress(0xf000);
   VDP_setHScrollTableAddress(0xf400);
   PAL_setColors(32, smd_sprite_palette2, 16, CPU);
   PAL_setColors(48, smd_sprite_palette3, 16, CPU);
   JOY_setSupport(PORT_1, JOY_SUPPORT_6BTN);
   deceptusAdventureInit(&adventure, &smd_tutorial_definition, &video);
   videoLoad();
   deceptusPacingInit(&pacing, IS_PAL_SYSTEM ? 50 : 60, SMD_PACING_MAX_TICKS);
   previous_vblank = vtimer - 1;
   room_loaded = 0;
   while (1)
   {
      /* a room load is not made up for. */
      ticks = deceptusPacingTicks(&pacing, room_loaded ? 1 : (DeceptusU16)(vtimer - previous_vblank));
      previous_vblank = vtimer;
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
      deceptusRender(&adventure.campaign.engine, &frame);
      flags = deceptusAdventurePresentationFlags(&adventure);
      smdBackgroundSetFlags(flags);
      smdBackgroundPrepare(frame.camera_x, frame.camera_y);
      smdSpritesPrepare(&adventure, &frame);
      smdSpritesMeasure();
      smdUiPrepare(&adventure);
      blank =
         smd_background_full || smd_sprite_blank || smd_background_dma_bytes + smd_sprite_upload_bytes + (smd_ui_dirty ? 4096 : 0) > 5500;
      debugWrite(buttons, blank);
      SYS_doVBlankProcess();
      if (blank)
      {
         VDP_setEnable(FALSE);
      }
      smdSpritesPreload();
      smdBackgroundCommit();
      smdSpritesCommit();
      smdUiCommit();
      if (blank)
      {
         SYS_doVBlankProcess();
      }
      VDP_setEnable(TRUE);
   }
   return 0;
}
