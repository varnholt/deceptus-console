/* original counterparts: Player::updateAtmosphere and PlayerJump::updateJump. */
#include "deceptus/playerwater.h"
#include "deceptus/physics.h"

DeceptusU8 deceptusPlayerWaterTick(DeceptusEngine *engine, const DeceptusInput *input)
{
   DeceptusEntity *player;
   const DeceptusWaterConfiguration *water;
   DeceptusI16 direction, target, surface_row;
   DeceptusU8 wet;
   player = &engine->entities[0];
   water = engine->room->water;
   if (engine->splash_active)
   {
      if ((water == 0) || (water->splash_durations[engine->splash_frame] == 0) ||
          (++engine->splash_elapsed >= water->splash_durations[engine->splash_frame]))
      {
         engine->splash_elapsed = 0;
         if ((++engine->splash_frame >= 20) || (water == 0))
         {
            engine->splash_active = 0;
         }
      }
   }
   wet = water && (deceptusCell(engine->room, deceptusPositionCell(player->x), deceptusPositionCell(player->y - 15 * DECEPTUS_ONE)) &
                   DECEPTUS_WATER);
   if (!wet)
   {
      if (player->in_water)
      {
         player->velocity_x = 0;
         player->velocity_y = (input->held & DECEPTUS_JUMP) ? -64 : 0;
      }
      player->in_water = 0;
      player->water_ticks = 0;
      return 0;
   }
   if (!player->in_water)
   {
      if (player->velocity_y > 0 && water->splash_durations[0])
      {
         surface_row = deceptusPositionCell(player->y - 15 * DECEPTUS_ONE);
         while (surface_row > 0 && (deceptusCell(engine->room, deceptusPositionCell(player->x), surface_row - 1) & DECEPTUS_WATER))
         {
            --surface_row;
         }
         engine->splash_x = deceptusFloor(player->x);
         engine->splash_y = surface_row * 8;
         engine->splash_active = 1;
         engine->splash_frame = 0;
         engine->splash_elapsed = 0;
      }
      player->water_ticks = 0;
      player->animation_ticks = 0;
      player->frame = 0;
   }
   player->in_water = 1;
   if (player->water_ticks < 255)
   {
      ++player->water_ticks;
   }
   player->crouched = 0;
   player->bend_ticks = 0;
   player->jump_buffer_ticks = 0;
   player->coyote_ticks = 0;
   direction = ((input->held & DECEPTUS_RIGHT) != 0) - ((input->held & DECEPTUS_LEFT) != 0);
   target = direction * water->speed;
   if (player->velocity_x < target)
   {
      ++player->velocity_x;
   }
   else if (player->velocity_x > target)
   {
      --player->velocity_x;
   }
   if (direction)
   {
      player->facing_left = direction < 0;
   }
   player->velocity_y += water->gravity;
   if ((input->held & DECEPTUS_JUMP) && player->water_ticks > water->stroke_delay_ticks)
   {
      player->velocity_y -= water->stroke_force;
   }
   if (player->velocity_y > water->sink_speed)
   {
      player->velocity_y = water->sink_speed;
   }
   if (player->velocity_y < -(DeceptusI16)water->rise_speed)
   {
      player->velocity_y = -(DeceptusI16)water->rise_speed;
   }
   deceptusMoveHorizontalDynamic(engine->room, player, 8, 30, &engine->mechanisms);
   deceptusMoveVerticalDynamic(engine->room, player, 8, 30, &engine->mechanisms);
   if (direction)
   {
      if (player->frame < 5 || player->frame > 12)
      {
         player->frame = 5;
         player->animation_ticks = 0;
      }
      player->animation_ticks += 5;
      if (player->animation_ticks >= 42)
      {
         player->animation_ticks -= 42;
         if (++player->frame >= 13)
         {
            player->frame = 5;
         }
      }
   }
   else
   {
      if (player->frame >= 5)
      {
         player->frame = 0;
         player->animation_ticks = 0;
      }
      player->animation_ticks += 2;
      if (player->animation_ticks >= 9)
      {
         player->animation_ticks -= 9;
         if (++player->frame >= 5)
         {
            player->frame = 0;
         }
      }
   }
   return 1;
}

void deceptusPlayerWaterRender(const DeceptusEngine *engine, DeceptusFrame *frame)
{
   DeceptusDraw *draw;
   DeceptusI16 x, y;
   if (!engine->splash_active || frame->count >= DECEPTUS_DRAW_CAPACITY || frame->count >= engine->video.max_draws)
   {
      return;
   }
   x = engine->splash_x - engine->camera_x - 36;
   y = engine->splash_y - engine->camera_y - 38;
   if (x <= -80 || y <= -80 || x >= engine->video.width || y >= engine->video.height)
   {
      return;
   }
   draw = &frame->draws[frame->count++];
   draw->x = x;
   draw->y = y;
   draw->frame = engine->splash_frame;
   draw->kind = DECEPTUS_WATER_SPLASH;
   draw->palette = 0;
   draw->flip_x = 0;
}
