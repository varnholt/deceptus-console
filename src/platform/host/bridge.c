#include "deceptus/game.h"

#ifdef _WIN32
#define PREVIEW_API __declspec(dllexport)
#else
#define PREVIEW_API __attribute__((visibility("default")))
#endif

extern const DeceptusRoom deceptus_scene;
static DeceptusEngine engine;
static DeceptusFrame frame;

PREVIEW_API void previewInit(void)
{
   DeceptusVideoCaps video;
   video.width = 256;
   video.height = 224;
   video.max_draws = DECEPTUS_DRAW_CAPACITY;
   deceptusInit(&engine, &deceptus_scene, &video);
   deceptusRender(&engine, &frame);
}

PREVIEW_API void previewTick(int buttons)
{
   deceptusTick(&engine, (DeceptusU16)buttons);
   deceptusRender(&engine, &frame);
}

/* integer getters avoid exposing target-specific C struct alignment to Python. */
PREVIEW_API int previewState(int field)
{
   switch (field)
   {
   case 0:
      return deceptusFloor(engine.entities[0].x);
   case 1:
      return deceptusFloor(engine.entities[0].y);
   case 2:
      return engine.camera_x;
   case 3:
      return engine.paused;
   case 4:
      return engine.health;
   case 5:
      return engine.door_entries;
   case 6:
      return engine.entities[0].grounded;
   case 7:
      return engine.ticks;
   case 8:
      return engine.checkpoint_id;
   case 9:
      return engine.checkpoint_x;
   case 10:
      return engine.checkpoint_y;
   case 11:
      return engine.camera_y;
   default:
      return 0;
   }
}

PREVIEW_API int previewDraw(int index, int field)
{
   if (index < 0 || index >= frame.count)
   {
      return -1;
   }
   switch (field)
   {
   case 0:
      return frame.draws[index].x;
   case 1:
      return frame.draws[index].y;
   case 2:
      return frame.draws[index].frame;
   case 3:
      return frame.draws[index].palette;
   case 4:
      return frame.draws[index].flip_x;
   default:
      return -1;
   }
}
