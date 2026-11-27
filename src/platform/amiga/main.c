#include "deceptus/game.h"
#include "system/input.h"
#include "system/keyboard.h"
#include "video/video.h"

extern const DeceptusRoom deceptus_scene;
static DeceptusEngine engine;
static DeceptusFrame frame;
static const DeceptusVideoCaps video = {320, 224, AMIGA_BOB_CAPACITY};
volatile DeceptusU32 amiga_debug[12];

int main(void)
{
   DeceptusU16 phase, buttons;
   DeceptusU32 start, previous_frame, now, elapsed;
   deceptusInit(&engine, &deceptus_scene, &video);
   if (!amigaVideoInit())
   {
      return 20;
   }
   start = amigaVideoFrames();
   amigaKeyboardReset();
   previous_frame = start - 1;
   phase = 0;
   while (!amigaInputQuit())
   {
      buttons = amigaInputRead() | amigaKeyboardRead();
      /* the portable simulation runs at 60 Hz on the 50 Hz PAL display. */
      now = amigaVideoFrames();
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
         deceptusTick(&engine, buttons);
         phase -= 50;
      }
      deceptusRender(&engine, &frame);
      amigaVideoDraw(&frame);
      amiga_debug[0] = 0xd3ce500;
      amiga_debug[1] = engine.ticks;
      amiga_debug[2] = amigaVideoFrames() - start;
      amiga_debug[3] = deceptusFloor(engine.entities[0].x);
      amiga_debug[4] = deceptusFloor(engine.entities[0].y);
      amiga_debug[5] = engine.health;
      amiga_debug[6] = engine.content_error;
      amiga_debug[7] = engine.camera_x;
      amiga_debug[8] = frame.count;
      amiga_debug[9] = sizeof(engine);
      ++amiga_debug[10];
   }
   amigaVideoClose();
   return 0;
}
