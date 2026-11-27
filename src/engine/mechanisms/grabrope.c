/* original counterparts: mechanisms/rope.cpp and mechanisms/grabrope.cpp. */
#include <string.h>
#include "arith.h"
#include "deceptus/physics.h"

/* exact 16x16 products where both factors fit; 16-bit cpus otherwise pay a 32-bit multiply call. */
static DeceptusU32 square(DeceptusI32 value)
{
   DeceptusU32 magnitude;
   magnitude = value < 0 ? (DeceptusU32)-value : (DeceptusU32)value;
   if (magnitude <= 65535UL)
   {
      return arithSquareU16((DeceptusU16)magnitude);
   }
   return (DeceptusU32)(value * value);
}

static DeceptusI32 product(DeceptusI32 left, DeceptusI32 right)
{
   if (left >= -32768L && left <= 32767L && right >= -32768L && right <= 32767L)
   {
      return arithMulS16((DeceptusI16)left, (DeceptusI16)right);
   }
   return left * right;
}

/* numerator / denominator truncated toward zero, as c division; every caller passes a positive
   denominator. */
static DeceptusI32 quotient(DeceptusI32 numerator, DeceptusU16 denominator)
{
   return arithDivS32U16(numerator, denominator);
}

/* value / 256 truncated toward zero, as c division, without a 32-bit divide call. */
static DeceptusI32 dividedBy256(DeceptusI32 value)
{
   if (value < 0)
   {
      return -(DeceptusI32)((DeceptusU32)-value >> 8);
   }
   return (DeceptusI32)((DeceptusU32)value >> 8);
}

DeceptusU16 deceptusGrabRopeDistance(DeceptusI32 x, DeceptusI32 y)
{
   return arithRootU32(square(x) + square(y));
}

static DeceptusU8 ropePointClear(const DeceptusEngine* engine, const DeceptusGrabRope* rope, DeceptusFixed x, DeceptusFixed y)
{
   DeceptusRect point;
   DeceptusI16 column, row, first_column, last_column, last_row;
   point.x = deceptusFloor(x) - 1;
   point.y = deceptusFloor(y) - 1;
   point.width = 2;
   point.height = 2;
   first_column = deceptusPositionCell(x - DECEPTUS_ONE);
   last_column = deceptusPositionCell(x);
   last_row = deceptusPositionCell(y);
   for (row = deceptusPositionCell(y - DECEPTUS_ONE); row <= last_row; ++row)
   {
      for (column = first_column; column <= last_column; ++column)
      {
         if (deceptusCell(engine->room, column, row) & DECEPTUS_SOLID)
         {
            return 0;
         }
      }
   }
   return (rope->dynamic_solids == 0) || (deceptusMechanismProjectileSolid(&engine->mechanisms, &point) == 0);
}

static void ropeMeasure(DeceptusGrabRope* rope)
{
   DeceptusU8 node;
   rope->cumulative_length[0] = 0;
   for (node = 1; node < rope->node_count; ++node)
   {
      rope->cumulative_length[node] =
         rope->cumulative_length[node - 1] +
         deceptusGrabRopeDistance((DeceptusI32)rope->x[node] - rope->x[node - 1], (DeceptusI32)rope->y[node] - rope->y[node - 1]);
   }
   rope->total_length = rope->cumulative_length[rope->node_count - 1];
}

static void ropeMoveNode(const DeceptusEngine* engine, DeceptusGrabRope* rope, DeceptusU8 node, DeceptusFixed x, DeceptusFixed y)
{
   if (x != rope->x[node] && ropePointClear(engine, rope, x, rope->y[node]))
   {
      rope->x[node] = x;
   }
   if (y != rope->y[node] && ropePointClear(engine, rope, rope->x[node], y))
   {
      rope->y[node] = y;
   }
}

static void ropeSolve(DeceptusEngine* engine, DeceptusGrabRope* rope, DeceptusU8 pinned, DeceptusFixed pin_x, DeceptusFixed pin_y)
{
   const DeceptusMechanismDef* definition;
   DeceptusI32 dx, dy, correction_x, correction_y, correction_scale;
   DeceptusU16 distance, rest;
   DeceptusU8 iteration, node, previous;
   definition = &engine->mechanisms.definitions[rope->mechanism_index];
   rest = (DeceptusU16)(definition->bounds.height * 16 / rope->node_count);
   for (iteration = 0; iteration < 2; ++iteration)
   {
      rope->x[0] = DECEPTUS_FIXED(definition->bounds.x);
      rope->y[0] = DECEPTUS_FIXED(definition->bounds.y) + 8;
      if (pinned < rope->node_count && pinned)
      {
         rope->x[pinned] = pin_x;
         rope->y[pinned] = pin_y;
      }
      for (node = 1; node < rope->node_count; ++node)
      {
         previous = node - 1;
         dx = (DeceptusI32)rope->x[node] - rope->x[previous];
         dy = (DeceptusI32)rope->y[node] - rope->y[previous];
         distance = deceptusGrabRopeDistance(dx, dy);
         if (!distance)
         {
            continue;
         }
         correction_scale = quotient(((DeceptusI32)distance - rest) * 256, distance);
         if (!correction_scale)
         {
            continue;
         }
         correction_x = dividedBy256(product(dx, correction_scale));
         correction_y = dividedBy256(product(dy, correction_scale));
         if (!correction_x && !correction_y)
         {
            continue;
         }
         if (!previous || previous == pinned)
         {
            if (node != pinned)
            {
               ropeMoveNode(
                  engine, rope, node, (DeceptusFixed)(rope->x[node] - correction_x), (DeceptusFixed)(rope->y[node] - correction_y)
               );
            }
         }
         else if (node == pinned)
         {
            ropeMoveNode(
               engine, rope, previous, (DeceptusFixed)(rope->x[previous] + correction_x), (DeceptusFixed)(rope->y[previous] + correction_y)
            );
         }
         else
         {
            correction_x /= 2;
            correction_y /= 2;
            ropeMoveNode(
               engine, rope, previous, (DeceptusFixed)(rope->x[previous] + correction_x), (DeceptusFixed)(rope->y[previous] + correction_y)
            );
            ropeMoveNode(engine, rope, node, (DeceptusFixed)(rope->x[node] - correction_x), (DeceptusFixed)(rope->y[node] - correction_y));
         }
      }
   }
   if (pinned && pinned < rope->node_count)
   {
      rope->x[pinned] = pin_x;
      rope->y[pinned] = pin_y;
   }
}

static DeceptusU8 ropeDynamicSolids(const DeceptusEngine* engine, const DeceptusMechanismDef* rope)
{
   const DeceptusMechanismDef* definition;
   DeceptusI32 reach, left, right, top, bottom;
   DeceptusU8 index;
   /* cover the full chain, attachment link, anchor slack and player extent. */
   reach = (DeceptusI32)rope->bounds.height + rope->bounds.height / rope->value + 36;
   left = (DeceptusI32)rope->bounds.x - reach;
   right = (DeceptusI32)rope->bounds.x + reach;
   top = (DeceptusI32)rope->bounds.y - reach;
   bottom = (DeceptusI32)rope->bounds.y + reach;
   for (index = 0; index < engine->mechanisms.count; ++index)
   {
      definition = &engine->mechanisms.definitions[index];
      switch (definition->kind)
      {
         case DECEPTUS_MECH_BLOCK:
         case DECEPTUS_MECH_COLLAPSING:
         case DECEPTUS_MECH_DOOR:
         case DECEPTUS_MECH_DESTRUCTIBLE:
            /* these source families never move their immutable collision bounds. */
            if ((DeceptusI32)definition->bounds.x <= right && (DeceptusI32)definition->bounds.x + definition->bounds.width >= left &&
                (DeceptusI32)definition->bounds.y <= bottom && (DeceptusI32)definition->bounds.y + definition->bounds.height >= top)
            {
               return 1;
            }
            break;
         case DECEPTUS_MECH_CHECKPOINT:
         case DECEPTUS_MECH_SENSOR:
         case DECEPTUS_MECH_LEVER:
         case DECEPTUS_MECH_DAMAGE:
         case DECEPTUS_MECH_FLAG:
         case DECEPTUS_MECH_SPIKES:
         case DECEPTUS_MECH_WIND:
         case DECEPTUS_MECH_CANNON_BODY:
         case DECEPTUS_MECH_GRAB_ROPE:
         /* box colliders stop moveable boxes only; the rope's projectile query never sees them. */
         case DECEPTUS_MECH_BOX_COLLIDER:
            break;
         default:
            /* future moving or unknown solid families must retain the full query. */
            return 1;
      }
   }
   return 0;
}

DeceptusU8 deceptusGrabRopesInit(DeceptusEngine* engine)
{
   const DeceptusMechanismDef* definition;
   DeceptusGrabRope* rope;
   DeceptusU8 index, node;
   memset(&engine->grab_ropes, 0, sizeof(engine->grab_ropes));
   for (index = 0; index < engine->mechanisms.count; ++index)
   {
      definition = &engine->mechanisms.definitions[index];
      if (definition->kind != DECEPTUS_MECH_GRAB_ROPE)
      {
         continue;
      }
      if (engine->grab_ropes.count >= DECEPTUS_GRAB_ROPE_CAPACITY || definition->value < 2 ||
          definition->value > DECEPTUS_GRAB_ROPE_NODES || definition->bounds.height <= 0 || definition->bounds.height > 512)
      {
         return 0;
      }
      rope = &engine->grab_ropes.ropes[engine->grab_ropes.count++];
      rope->mechanism_index = index;
      rope->node_count = (DeceptusU8)definition->value;
      rope->dynamic_solids = ropeDynamicSolids(engine, definition);
      for (node = 0; node < rope->node_count; ++node)
      {
         rope->x[node] = rope->previous_x[node] = DECEPTUS_FIXED(definition->bounds.x);
         rope->y[node] = rope->previous_y[node] = DECEPTUS_FIXED(definition->bounds.y) + 8 +
                                                  (DeceptusFixed)((DeceptusI32)definition->bounds.height * 16 * node / rope->node_count);
      }
      ropeMeasure(rope);
   }
   return 1;
}

void deceptusGrabRopesTick(DeceptusEngine* engine)
{
   DeceptusGrabRope* rope;
   DeceptusFixed x, y;
   DeceptusFixed before[4][DECEPTUS_GRAB_ROPE_NODES];
   DeceptusI16 vx, vy;
   DeceptusU8 index, node, damp, attached;
   for (index = 0; index < engine->grab_ropes.count; ++index)
   {
      rope = &engine->grab_ropes.ropes[index];
      if (!engine->mechanisms.states[rope->mechanism_index].enabled)
      {
         continue;
      }
      damp = ++rope->damping_ticks >= 60;
      if (damp)
      {
         rope->damping_ticks = 0;
      }
      attached = engine->player_rope.attached && engine->player_rope.rope_index == index;
      /* at rest every velocity is zero, so damping changes nothing and gravity is undone by the
         solve: the tick reproduces its input exactly. only moving solids or a hold can disturb it. */
      if (rope->settled && !rope->dynamic_solids && !attached)
      {
         continue;
      }
      for (node = 0; node < rope->node_count; ++node)
      {
         before[0][node] = rope->x[node];
         before[1][node] = rope->y[node];
         before[2][node] = rope->previous_x[node];
         before[3][node] = rope->previous_y[node];
      }
      for (node = 1; node < rope->node_count; ++node)
      {
         x = rope->x[node];
         y = rope->y[node];
         vx = x - rope->previous_x[node];
         vy = y - rope->previous_y[node];
         if (damp)
         {
            vx -= vx / 10;
            vy -= vy / 10;
         }
         vy += engine->room->player_gravity ? engine->room->player_gravity : 4;
         if (vx > 127)
         {
            vx = 127;
         }
         if (vx < -127)
         {
            vx = -127;
         }
         if (vy > 127)
         {
            vy = 127;
         }
         if (vy < -127)
         {
            vy = -127;
         }
         rope->previous_x[node] = x;
         rope->previous_y[node] = y;
         ropeMoveNode(engine, rope, node, x + vx, y + vy);
      }
      /* attached chains receive their single final solve after player integration. */
      if (!attached)
      {
         ropeSolve(engine, rope, 255, 0, 0);
         ropeMeasure(rope);
      }
      rope->settled = !attached;
      for (node = 0; rope->settled && node < rope->node_count; ++node)
      {
         rope->settled = before[0][node] == rope->x[node] && before[1][node] == rope->y[node] &&
                         before[2][node] == rope->previous_x[node] && before[3][node] == rope->previous_y[node];
      }
   }
}

void deceptusGrabRopePin(DeceptusEngine* engine, DeceptusU8 index, DeceptusU8 node, DeceptusFixed x, DeceptusFixed y)
{
   DeceptusGrabRope* rope;
   if (index >= engine->grab_ropes.count)
   {
      return;
   }
   rope = &engine->grab_ropes.ropes[index];
   if (!node || node >= rope->node_count)
   {
      return;
   }
   rope->settled = 0;
   ropeSolve(engine, rope, node, x, y);
   rope->previous_x[node] = x - engine->entities[0].velocity_x;
   rope->previous_y[node] = y - engine->entities[0].velocity_y;
   ropeMeasure(rope);
}

DeceptusU16 deceptusGrabRopeLength(const DeceptusEngine* engine, DeceptusU8 index)
{
   const DeceptusGrabRope* rope;
   if (index >= engine->grab_ropes.count)
   {
      return 0;
   }
   rope = &engine->grab_ropes.ropes[index];
   if (!engine->mechanisms.states[rope->mechanism_index].enabled)
   {
      return 0;
   }
   return (rope->total_length + 15) / 16;
}

DeceptusU8 deceptusGrabRopePointFrom(
   const DeceptusEngine* engine,
   DeceptusU8 index,
   DeceptusU16 distance_px,
   DeceptusU8* node_hint,
   DeceptusI16* x,
   DeceptusI16* y
)
{
   const DeceptusGrabRope* rope;
   DeceptusU16 remaining, length;
   DeceptusI32 dx, dy;
   DeceptusU8 node;
   if ((index >= engine->grab_ropes.count) || (x == 0) || (y == 0) || (node_hint == 0) || (*node_hint == 0))
   {
      return 0;
   }
   rope = &engine->grab_ropes.ropes[index];
   if (!engine->mechanisms.states[rope->mechanism_index].enabled)
   {
      return 0;
   }
   remaining = distance_px > 2047 ? 32752 : distance_px * 16;
   node = *node_hint;
   if (node < rope->node_count)
   {
      /* the links before the hint were passed by a shorter distance, so they are passed again: the
         walk from the anchor would arrive here with exactly this remainder. */
      remaining -= rope->cumulative_length[node - 1];
   }
   for (; node < rope->node_count; ++node)
   {
      dx = (DeceptusI32)rope->x[node] - rope->x[node - 1];
      dy = (DeceptusI32)rope->y[node] - rope->y[node - 1];
      length = rope->cumulative_length[node] - rope->cumulative_length[node - 1];
      if (remaining <= length && length)
      {
         *node_hint = node;
         *x = deceptusFloor((DeceptusFixed)(rope->x[node - 1] + quotient(product(dx, remaining), length)));
         *y = deceptusFloor((DeceptusFixed)(rope->y[node - 1] + quotient(product(dy, remaining), length)));
         return 1;
      }
      remaining -= length;
   }
   *node_hint = rope->node_count;
   *x = deceptusFloor(rope->x[rope->node_count - 1]);
   *y = deceptusFloor(rope->y[rope->node_count - 1]);
   return 1;
}

DeceptusU8 deceptusGrabRopePointAt(const DeceptusEngine* engine, DeceptusU8 index, DeceptusU16 distance_px, DeceptusI16* x, DeceptusI16* y)
{
   DeceptusU8 node;
   node = 1;
   return deceptusGrabRopePointFrom(engine, index, distance_px, &node, x, y);
}
