#include "deceptus/playeranimation.h"

static DeceptusU8 selectSwordPose(const DeceptusAdventure *game)
{
   const DeceptusEngine *engine;
   const DeceptusEntity *player;
   DeceptusU8 pose, left;
   engine = &game->campaign.engine;
   player = &engine->entities[0];
   left = player->facing_left;
   if (game->sword.ticks)
   {
      pose = game->sword.airborne ? 10 : (game->sword.crouched ? 9 : 8);
      left = game->sword.facing_left;
   }
   else if (engine->player_rope.attached)
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
      pose = player->velocity_y < -16 ? 2 : (player->velocity_y > 16 ? 4 : 3);
   }
   else
   {
      pose = player->velocity_x ? 1 : 0;
   }
   return pose + (left ? 11 : 0);
}

DeceptusU8 deceptusPlayerSwordAppearance(const DeceptusAdventure *game, DeceptusPlayerAnimation *animation,
                                         const DeceptusSwordAnimationDef *definition, DeceptusDraw *draw)
{
   const DeceptusU8 *clip;
   const DeceptusEntity *player;
   DeceptusU16 step, duration, elapsed, delta;
   DeceptusU8 pose;
   if (!game->has_sword)
   {
      animation->previous_pose = 255;
      return DECEPTUS_APPEARANCE_NONE;
   }
   pose = selectSwordPose(game);
   if ((definition == 0) || (definition->clips == 0) || (definition->durations == 0) || (definition->frame_count > 256) ||
       (pose >= definition->clip_count))
   {
      return DECEPTUS_APPEARANCE_BAD_CONTENT;
   }
   clip = definition->clips[pose];
   if (!clip[1] || clip[0] > definition->frame_count || clip[1] > definition->frame_count - clip[0])
   {
      return DECEPTUS_APPEARANCE_BAD_CONTENT;
   }
   duration = 0;
   for (step = 0; step < clip[1]; ++step)
   {
      if (!definition->durations[clip[0] + step])
      {
         return DECEPTUS_APPEARANCE_BAD_CONTENT;
      }
      duration += definition->durations[clip[0] + step];
   }
   delta = (DeceptusU16)(game->campaign.engine.ticks - animation->previous_tick);
   animation->pose_ticks = pose != animation->previous_pose || delta > 4 ? 0 : animation->pose_ticks + delta;
   animation->previous_pose = pose;
   animation->previous_tick = game->campaign.engine.ticks;
   /* a rendered tick advances at most four source ticks; frame durations are nonzero. */
   while (animation->pose_ticks >= duration)
   {
      animation->pose_ticks -= duration;
   }
   elapsed = game->sword.ticks ? game->sword.ticks - 1 : animation->pose_ticks;
   for (step = 0; step + 1 < clip[1] && elapsed >= definition->durations[clip[0] + step]; ++step)
   {
      elapsed -= definition->durations[clip[0] + step];
   }
   player = &game->campaign.engine.entities[0];
   draw->kind = DECEPTUS_PLAYER_SWORD;
   draw->frame = (DeceptusU8)(clip[0] + step);
   draw->x = deceptusFloor(player->x) - game->campaign.engine.camera_x - clip[2];
   draw->y = deceptusFloor(player->y) - game->campaign.engine.camera_y - clip[3];
   draw->palette = 0;
   draw->flip_x = 0;
   return DECEPTUS_APPEARANCE_READY;
}
