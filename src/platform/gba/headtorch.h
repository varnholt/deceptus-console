#include "deceptus/game.h"
#include "deceptus/itemheadtorch.h"
#if GBA_TUTORIAL && defined(GBA_HEADTORCH_FIRST)
static void __attribute__((section(".romtext"), noinline)) headtorchPrepare(void)
{
   const GbaHeadtorchPose *pose;
   const DeceptusDraw *body;
   DeceptusDraw helmet, beam;
   DeceptusI16 x, y;
   DeceptusU16 index, player_index;
   if (!deceptusItemHeadtorchEquipped(&adventure))
   {
      return;
   }
   pose = 0;
   player_index = 0;
   for (index = 0; index < frame.count; ++index)
   {
      body = &frame.draws[index];
      if (body->kind == DECEPTUS_PLAYER)
      {
         pose = &gba_headtorch_normal[engine.entities[0].facing_left][body->frame];
      }
      else if (body->kind == DECEPTUS_PLAYER_SWIM)
      {
         pose = &gba_headtorch_swim[body->frame];
      }
      else if (body->kind == DECEPTUS_PLAYER_SWORD)
      {
         pose = &gba_headtorch_sword[body->frame];
#ifdef GBA_PLAYER_ROPE_FIRST
      }
      else if (body->kind == DECEPTUS_PLAYER_ROPE)
      {
         pose = &gba_headtorch_rope[body->frame];
#endif
      }
      if (pose)
      {
         player_index = index;
         break;
      }
   }
   if (!pose || !pose->valid)
   {
      return;
   }
   if (frame.count + 2 > DECEPTUS_DRAW_CAPACITY)
   {
      engine.content_error = 4;
      return;
   }
   x = deceptusFloor(engine.entities[0].x) - frame.camera_x + pose->x;
   y = deceptusFloor(engine.entities[0].y) - frame.camera_y + pose->y;
   helmet.x = x;
   helmet.y = y;
   helmet.kind = DECEPTUS_HEADTORCH;
   helmet.frame = pose->side;
   helmet.palette = 0;
   helmet.flip_x = 0;
   beam = helmet;
   beam.kind = DECEPTUS_HEADTORCH_BEAM;
   beam.frame = 0;
   beam.x += gba_headtorch_lamp_offsets[pose->side][0] - (pose->side ? 64 : 0);
   beam.y += gba_headtorch_lamp_offsets[pose->side][1] - 16;
   beam.flip_x = pose->side;
   /* the helmet covers the hair; the light uses a separate additive overlay. */
   for (index = frame.count; index > player_index; --index)
   {
      frame.draws[index] = frame.draws[index - 1];
   }
   frame.draws[player_index] = helmet;
   frame.draws[frame.count + 1] = beam;
   frame.count += 2;
}
#else
static void headtorchPrepare(void)
{
}
#endif
