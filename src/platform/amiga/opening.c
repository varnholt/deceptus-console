#include "amiga_opening_assets.h"
#include "deceptus/game.h"
#include "input.h"
#include "keyboard.h"
#include "opening_video.h"
#include "rooms.h"
#include "system.h"

static DeceptusAdventure adventure;
static DeceptusFrame frame;
static const DeceptusVideoCaps video = {320, 224, DECEPTUS_DRAW_CAPACITY};
volatile DeceptusU32 amiga_debug[32];

static void debugWrite(void)
{
   const DeceptusEngine *engine;
   const AmigaGraphics *graphics;
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
   DeceptusU16 phase, buttons;
   DeceptusU32 previous_frame, now, elapsed;
   if (!amigaSystemOpen())
   {
      return 20;
   }
   provider.load = amigaRoomLoad;
   provider.context = &adventure;
   deceptusAdventureInitWithRoomProvider(&adventure, &amiga_tutorial_definition, &video, &provider);
   if (adventure.campaign.engine.content_error)
   {
      debugWrite();
      amigaSystemClose();
      return 20;
   }
   deceptusRender(&adventure.campaign.engine, &frame);
   if (!amigaOpeningVideoDraw(&adventure, &frame))
   {
      amigaOpeningVideoClose();
      return 20;
   }
   previous_frame = amigaOpeningVideoFrames();
   phase = 0;
   while (!amigaInputQuit())
   {
      now = amigaOpeningVideoFrames();
      elapsed = now - previous_frame;
      previous_frame = now;
      if (elapsed > 5)
      {
         amiga_debug[11] += elapsed - 5;
         elapsed = 5;
      }
      phase += elapsed * 60;
      while (phase >= 50)
      {
         buttons = amigaInputRead() | amigaKeyboardRead();
         deceptusAdventureTick(&adventure, buttons);
         amiga_debug[22] = buttons;
         phase -= 50;
      }
      if (adventure.has_sword != amigaRoomSwordGraphics() && !amigaRoomLoadGraphics(adventure.campaign.room_index, adventure.has_sword))
      {
         amiga_debug[21] = 2;
         break;
      }
      deceptusRender(&adventure.campaign.engine, &frame);
      debugWrite();
      if (adventure.campaign.engine.content_error || !amigaOpeningVideoDraw(&adventure, &frame))
      {
         amiga_debug[21] = 1;
         break;
      }
      ++amiga_debug[10];
   }
   amigaOpeningVideoClose();
   return amiga_debug[21] ? 20 : 0;
}
