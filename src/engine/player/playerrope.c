/* original counterparts: player/playerrope.cpp and player/playerropehold.cpp. */
#include "deceptus/physics.h"
#include <string.h>

void deceptusPlayerRopeReset(DeceptusEngine *engine)
{
   memset(&engine->player_rope, 0, sizeof(engine->player_rope));
}

static DeceptusFixed playerRopeLimit(DeceptusI32 velocity)
{
   if (velocity > 127)
   {
      return 127;
   }
   if (velocity < -127)
   {
      return -127;
   }
   return (DeceptusFixed)velocity;
}

static void playerRopeMove(DeceptusEngine *engine)
{
   DeceptusEntity *player;
   DeceptusU8 grounded;
   player = &engine->entities[0];
   grounded = player->grounded;
   player->velocity_x = playerRopeLimit(player->velocity_x);
   player->velocity_y = playerRopeLimit(player->velocity_y);
   deceptusMoveHorizontalDynamic(engine->room, player, 8, 30, &engine->mechanisms);
   deceptusMoveVerticalDynamic(engine->room, player, 8, 30, &engine->mechanisms);
   if (player->grounded && !grounded)
   {
      deceptusEventPush(engine, DECEPTUS_EVENT_LAND);
   }
   player->frame = player->velocity_y < 0 ? 16 : 17;
}

static DeceptusU8 playerRopeNear(const DeceptusGrabRope *rope, const DeceptusEntity *player)
{
   DeceptusU8 node;
   DeceptusI16 x, y, left, right, top, bottom, first_x, first_y, second_x, second_y;
   x = deceptusFloor(player->x);
   y = deceptusFloor(player->y);
   for (node = 1; node < rope->node_count; ++node)
   {
      first_x = deceptusFloor(rope->x[node - 1]);
      first_y = deceptusFloor(rope->y[node - 1]);
      second_x = deceptusFloor(rope->x[node]);
      second_y = deceptusFloor(rope->y[node]);
      left = (first_x < second_x ? first_x : second_x) - 8;
      right = (first_x > second_x ? first_x : second_x) + 8;
      top = (first_y < second_y ? first_y : second_y) - 8;
      bottom = (first_y > second_y ? first_y : second_y) + 8;
      if (x + 8 > left && x - 8 < right && y > top && y - 30 < bottom)
      {
         return 1;
      }
   }
   return 0;
}

static DeceptusU8 playerRopeGrab(DeceptusEngine *engine)
{
   DeceptusEntity *player;
   DeceptusPlayerRope *hold;
   const DeceptusGrabRope *rope;
   const DeceptusMechanismDef *definition;
   DeceptusU16 best, distance, segment_length;
   DeceptusU8 index, node, closest;
   player = &engine->entities[0];
   hold = &engine->player_rope;
   if (player->grounded || player->in_water || hold->regrab_ticks)
   {
      return 0;
   }
   for (index = 0; index < engine->grab_ropes.count; ++index)
   {
      rope = &engine->grab_ropes.ropes[index];
      if (!engine->mechanisms.states[rope->mechanism_index].enabled || !playerRopeNear(rope, player))
      {
         continue;
      }
      definition = &engine->mechanisms.definitions[rope->mechanism_index];
      closest = 0;
      best = 65535;
      for (node = 0; node < rope->node_count; ++node)
      {
         distance = deceptusGrabRopeDistance((DeceptusI32)player->x - rope->x[node],
                                             (DeceptusI32)player->y - DECEPTUS_FIXED(DECEPTUS_PLAYER_ROPE_HAND_OFFSET) - rope->y[node]);
         if (distance < best)
         {
            best = distance;
            closest = node;
         }
      }
      segment_length = (DeceptusU16)(definition->bounds.height * 16 / rope->node_count);
      hold->attached = 1;
      hold->rope_index = index;
      hold->node_index = closest;
      hold->link_length = best < segment_length ? best : segment_length;
      hold->radius = (DeceptusU16)((DeceptusI32)definition->bounds.height * 16 * closest / rope->node_count) + hold->link_length + 115;
      player->coyote_ticks = 0;
      player->jump_buffer_ticks = 0;
      player->crouched = 0;
      player->bend_ticks = 0;
      player->animation_ticks = 0;
      return 1;
   }
   return 0;
}

DeceptusU8 deceptusPlayerRopeTick(DeceptusEngine *engine, const DeceptusInput *input)
{
   DeceptusPlayerRope *hold;
   DeceptusEntity *player;
   DeceptusGrabRope *rope;
   const DeceptusMechanismDef *definition;
   DeceptusI32 dx, dy, square, acceleration_x, acceleration_y;
   DeceptusFixed previous_x, previous_y, next_x, next_y;
   DeceptusI16 direction, gravity;
   DeceptusU16 distance, pull;
   DeceptusU8 grabbed;
   hold = &engine->player_rope;
   player = &engine->entities[0];
   if (hold->regrab_ticks)
   {
      --hold->regrab_ticks;
   }
   if (hold->grace_ticks)
   {
      --hold->grace_ticks;
   }
   if (player->in_water)
   {
      deceptusPlayerRopeReset(engine);
      return 0;
   }
   grabbed = 0;
   if (!hold->attached)
   {
      grabbed = playerRopeGrab(engine);
   }
   gravity = engine->room->player_gravity ? engine->room->player_gravity : 4;
   if (!hold->attached)
   {
      if (hold->grace_ticks && !player->grounded)
      {
         player->velocity_y = playerRopeLimit((DeceptusI32)player->velocity_y + gravity);
         playerRopeMove(engine);
         return 1;
      }
      return 0;
   }
   if (hold->rope_index >= engine->grab_ropes.count)
   {
      deceptusPlayerRopeReset(engine);
      return 0;
   }
   rope = &engine->grab_ropes.ropes[hold->rope_index];
   definition = &engine->mechanisms.definitions[rope->mechanism_index];
   if (!engine->mechanisms.states[rope->mechanism_index].enabled)
   {
      deceptusPlayerRopeReset(engine);
      return 0;
   }
   if (!grabbed && (input->pressed & DECEPTUS_JUMP))
   {
      hold->attached = 0;
      hold->regrab_ticks = 60;
      hold->grace_ticks = 36;
      /* source jumpOff adds half the ordinary jump lift without horizontal push. */
      player->velocity_y = playerRopeLimit((DeceptusI32)player->velocity_y - 44 + gravity);
      deceptusEventPush(engine, DECEPTUS_EVENT_JUMP);
      playerRopeMove(engine);
      return 1;
   }
   direction = ((input->held & DECEPTUS_RIGHT) != 0) - ((input->held & DECEPTUS_LEFT) != 0);
   if (direction)
   {
      player->facing_left = direction < 0;
   }
   previous_x = player->x;
   previous_y = player->y;
   dx = (DeceptusI32)player->x - DECEPTUS_FIXED(definition->bounds.x);
   dy = (DeceptusI32)player->y - DECEPTUS_FIXED(8) - DECEPTUS_FIXED(definition->bounds.y);
   square = dx * dx + dy * dy;
   acceleration_x = acceleration_y = 0;
   if (square)
   {
      /* source 14m/s^2 tangential control becomes 3 units of 12.4 per tick. */
      acceleration_x = (DeceptusI32)direction * 3 * dy * dy / square;
      acceleration_y = -(DeceptusI32)direction * 3 * dx * dy / square;
   }
   player->velocity_x = playerRopeLimit((DeceptusI32)player->velocity_x + acceleration_x);
   player->velocity_y = playerRopeLimit((DeceptusI32)player->velocity_y + gravity + acceleration_y);
   playerRopeMove(engine);
   if (hold->link_length)
   {
      pull = hold->link_length < 19 ? hold->link_length : 19;
      hold->link_length -= pull;
      hold->radius -= pull;
   }
   dx = (DeceptusI32)player->x - DECEPTUS_FIXED(definition->bounds.x);
   dy = (DeceptusI32)player->y - DECEPTUS_FIXED(DECEPTUS_PLAYER_ROPE_HAND_OFFSET) - DECEPTUS_FIXED(definition->bounds.y);
   distance = deceptusGrabRopeDistance(dx, dy);
   if (distance > hold->radius && distance)
   {
      next_x = (DeceptusFixed)(DECEPTUS_FIXED(definition->bounds.x) + dx * hold->radius / distance);
      next_y = (DeceptusFixed)(DECEPTUS_FIXED(definition->bounds.y + DECEPTUS_PLAYER_ROPE_HAND_OFFSET) + dy * hold->radius / distance);
      if (deceptusBodyClear(engine->room, &engine->mechanisms, next_x, next_y, 8, 30))
      {
         player->x = next_x;
         player->y = next_y;
      }
   }
   player->velocity_x = playerRopeLimit((DeceptusI32)player->x - previous_x);
   player->velocity_y = playerRopeLimit((DeceptusI32)player->y - previous_y);
   deceptusGrabRopePin(engine, hold->rope_index, hold->node_index, player->x, player->y - DECEPTUS_FIXED(DECEPTUS_PLAYER_ROPE_HAND_OFFSET));
   ++player->animation_ticks;
   if (player->animation_ticks >= 11)
   {
      player->animation_ticks = 0;
   }
   player->frame = player->animation_ticks < 4 ? 0 : player->animation_ticks < 8 ? 1 : 2;
   return 1;
}
