#include "amiga_opening_assets.h"
#include "audio/audio.h"
#include "deceptus/game.h"
#include "deceptus/pacing.h"
#include "songs.h"
#include "system/input.h"
#include "system/keyboard.h"
#include "system/system.h"
#include "video/opening_video.h"
#include "video/rooms.h"

static DeceptusAdventure adventure;
static DeceptusFrame frame;
static const DeceptusVideoCaps video = {320, 224, DECEPTUS_DRAW_CAPACITY};
/* up to three ticks per presentation: the 50 Hz display already needs 1.2 per frame, and the scroll
   surface keeps up with that much camera travel (the game runs at full speed down to 20 fps). */
#ifndef AMIGA_PACING_MAX_TICKS
#define AMIGA_PACING_MAX_TICKS 3
#endif
static DeceptusPacing pacing;
volatile DeceptusU32 amiga_debug[32];

static void debugWrite(void)
{
   const DeceptusEngine* engine;
   const AmigaGraphics* graphics;
   engine = &adventure.campaign.engine;
   graphics = amigaRoomGraphics();
   amiga_debug[0] = 0xd3ce500;
   amiga_debug[1] = engine->ticks;
   amiga_debug[2] = amigaOpeningVideoFrames();
   amiga_debug[3] = deceptusFloor(engine->entities[0].x);
   amiga_debug[4] = deceptusFloor(engine->entities[0].y);
   amiga_debug[5] = engine->health;
   amiga_debug[6] = engine->content_error;
   amiga_debug[7] = engine->camera_x;
   amiga_debug[8] = frame.count;
   amiga_debug[9] = sizeof(adventure);
   amiga_debug[12] = adventure.campaign.room_index;
   amiga_debug[13] = adventure.campaign.mode;
   amiga_debug[14] = adventure.flags;
   amiga_debug[15] = adventure.inventory.counts[3];
   amiga_debug[16] = adventure.has_sword;
   amiga_debug[17] = adventure.campaign.elapsed_ticks;
   amiga_debug[18] = graphics->world_size;
   amiga_debug[19] = graphics->sprite_size;
   amiga_debug[20] = amigaRoomLoads();
}

int main(void)
{
   DeceptusRoomProvider provider;
   DeceptusU16 buttons;
   DeceptusU32 previous_frame, now, elapsed;
   DeceptusU8 ticks, room_loaded;
   if (!amigaSystemOpen())
   {
      return 20;
   }
   /* display pages first, then the room banks take what remains. */
   if (!amigaOpeningVideoAllocate() || !amigaRoomsAllocate())
   {
      amigaOpeningVideoRelease();
      amigaSystemClose();
      return 20;
   }
   provider.load = amigaRoomLoad;
   provider.context = &adventure;
   deceptusAdventureInitWithRoomProvider(&adventure, &amiga_tutorial_definition, &video, &provider);
   audioStart(SONG_LEVEL_TEST_TRACK_MUFFLER_AWAKENING);
   if (adventure.campaign.engine.content_error)
   {
      debugWrite();
      amigaSystemClose();
      amigaRoomsRelease();
      amigaOpeningVideoRelease();
      return 20;
   }
   deceptusRender(&adventure.campaign.engine, &frame);
   if (!amigaOpeningVideoDraw(&adventure, &frame))
   {
      amigaOpeningVideoClose();
      amigaRoomsRelease();
      amigaOpeningVideoRelease();
      return 20;
   }
   deceptusPacingInit(&pacing, 50, AMIGA_PACING_MAX_TICKS);
   previous_frame = amigaOpeningVideoFrames();
   room_loaded = 0;
   while (!amigaInputQuit())
   {
      now = amigaOpeningVideoFrames();
      elapsed = now - previous_frame;
      previous_frame = now;
      /* a room load reads from disk and is not made up for. */
      ticks = deceptusPacingTicks(&pacing, room_loaded ? 1 : (DeceptusU16)(elapsed > 255 ? 255 : elapsed));
      room_loaded = 0;
      amiga_debug[11] = pacing.dropped_ticks;
      while (ticks)
      {
         --ticks;
         buttons = amigaInputRead() | amigaKeyboardRead();
         amiga_debug[22] = buttons;
         if (deceptusAdventureTick(&adventure, buttons))
         {
            deceptusPacingReset(&pacing);
            room_loaded = 1;
            break;
         }
      }
      if (adventure.has_sword != amigaRoomSwordGraphics() && !amigaRoomLoadGraphics(adventure.campaign.room_index, adventure.has_sword))
      {
         amiga_debug[21] = 2;
         break;
      }
      audioPlayCues(&adventure.sounds);
      deceptusRender(&adventure.campaign.engine, &frame);
      debugWrite();
      if (adventure.campaign.engine.content_error || !amigaOpeningVideoDraw(&adventure, &frame))
      {
         amiga_debug[21] = 1;
         break;
      }
      ++amiga_debug[10];
   }
   audioStop();
   amigaOpeningVideoClose();
   amigaRoomsRelease();
   amigaOpeningVideoRelease();
   return amiga_debug[21] ? 20 : 0;
}
