/* original counterparts: mechanisms/rope.cpp and mechanisms/grabrope.cpp. */
#include "deceptus/physics.h"
#include <string.h>

#if defined(SNES_ENGINE_ASM)
/* 65816 versions in src/platform/snes/engine_math.asm; they return what the c below returns. */
DeceptusU16 snesRopeDistance(DeceptusI32 x, DeceptusI32 y);
DeceptusI32 snesRopeQuotient(DeceptusI32 numerator, DeceptusU16 denominator);
#endif

/* exact 16x16 products where both factors fit; 16-bit cpus otherwise pay a 32-bit multiply call. */
static DeceptusU32 square(DeceptusI32 value)
{
   DeceptusU32 magnitude;
   magnitude = value < 0 ? (DeceptusU32)-value : (DeceptusU32)value;
   if (magnitude <= 65535UL)
   {
#if defined(__mc68000__) || defined(__m68k__)
      /* gcc can leave even a 16x16 product to the 32-bit library call; mulu.w is exact here. */
      __asm__("mulu.w %0,%0" : "+d"(magnitude) : : "cc");
      return magnitude;
#else
      return (DeceptusU32)(DeceptusU16)magnitude * (DeceptusU16)magnitude;
#endif
   }
   return (DeceptusU32)(value * value);
}

static DeceptusI32 product(DeceptusI32 left, DeceptusI32 right)
{
   if (left >= -32768L && left <= 32767L && right >= -32768L && right <= 32767L)
   {
#if defined(__mc68000__) || defined(__m68k__)
      __asm__("muls.w %1,%0" : "+d"(left) : "d"((DeceptusI16)right) : "cc");
      return left;
#else
      return (DeceptusI32)(DeceptusI16)left * (DeceptusI16)right;
#endif
   }
   return left * right;
}

/* numerator / denominator truncated toward zero, as c division; a 32/16 hardware divide on 68000
   when the quotient fits 16 bits. */
static DeceptusI32 quotient(DeceptusI32 numerator, DeceptusU16 denominator)
{
#if defined(__mc68000__) || defined(__m68k__)
   DeceptusI32 result;
   DeceptusI32 range;
   /* 32768 * denominator as a shift: the multiply would be a library call itself. */
   range = (DeceptusI32)denominator << 15;
   if (denominator <= 32767U && numerator > -range && numerator < range)
   {
      result = numerator;
      __asm__("divs.w %1,%0" : "+d"(result) : "dmi"((DeceptusI16)denominator) : "cc");
      return (DeceptusI16)result;
   }
#endif
#if defined(SNES_ENGINE_ASM)
   /* every caller passes a positive denominator. */
   return snesRopeQuotient(numerator, denominator);
#else
   return numerator / (DeceptusI32)denominator;
#endif
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
   DeceptusU32 value, result, bit;
   DeceptusU16 short_value, short_result, short_bit;
#if defined(SNES_ENGINE_ASM)
   if ((DeceptusU32)(x + 65535L) <= 131070UL && (DeceptusU32)(y + 65535L) <= 131070UL)
   {
      return snesRopeDistance(x, y);
   }
#endif
   value = square(x) + square(y);
   if (value <= 65535UL)
   {
      /* neighbouring nodes are short: the same digit-by-digit root in 16-bit words, which 16-bit
         cpus run without 32-bit shift and compare sequences. */
      short_value = (DeceptusU16)value;
      short_result = 0;
      short_bit = (DeceptusU16)1 << 14;
      while (short_bit > short_value)
      {
         short_bit >>= 2;
      }
      while (short_bit)
      {
         if (short_value >= short_result + short_bit)
         {
            short_value -= short_result + short_bit;
            short_result = (short_result >> 1) + short_bit;
         }
         else
         {
            short_result >>= 1;
         }
         short_bit >>= 2;
      }
      return short_result;
   }
   result = 0;
   bit = (DeceptusU32)1 << 30;
   while (bit > value)
   {
      bit >>= 2;
   }
   while (bit)
   {
      if (value >= result + bit)
      {
         value -= result + bit;
         result = (result >> 1) + bit;
      }
      else
      {
         result >>= 1;
      }
      bit >>= 2;
   }
   return (DeceptusU16)result;
}

static DeceptusU8 ropePointClear(const DeceptusEngine *engine, const DeceptusGrabRope *rope, DeceptusFixed x, DeceptusFixed y)
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
   return !rope->dynamic_solids || !deceptusMechanismProjectileSolid(&engine->mechanisms, &point);
}

static void ropeMeasure(DeceptusGrabRope *rope)
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

static void ropeMoveNode(const DeceptusEngine *engine, DeceptusGrabRope *rope, DeceptusU8 node, DeceptusFixed x, DeceptusFixed y)
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

static void ropeSolve(DeceptusEngine *engine, DeceptusGrabRope *rope, DeceptusU8 pinned, DeceptusFixed pin_x, DeceptusFixed pin_y)
{
   const DeceptusMechanismDef *definition;
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
               ropeMoveNode(engine, rope, node, (DeceptusFixed)(rope->x[node] - correction_x),
                            (DeceptusFixed)(rope->y[node] - correction_y));
            }
         }
         else if (node == pinned)
         {
            ropeMoveNode(engine, rope, previous, (DeceptusFixed)(rope->x[previous] + correction_x),
                         (DeceptusFixed)(rope->y[previous] + correction_y));
         }
         else
         {
            correction_x /= 2;
            correction_y /= 2;
            ropeMoveNode(engine, rope, previous, (DeceptusFixed)(rope->x[previous] + correction_x),
                         (DeceptusFixed)(rope->y[previous] + correction_y));
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

static DeceptusU8 ropeDynamicSolids(const DeceptusEngine *engine, const DeceptusMechanismDef *rope)
{
   const DeceptusMechanismDef *definition;
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
         break;
      default:
         /* future moving or unknown solid families must retain the full query. */
         return 1;
      }
   }
   return 0;
}

DeceptusU8 deceptusGrabRopesInit(DeceptusEngine *engine)
{
   const DeceptusMechanismDef *definition;
   DeceptusGrabRope *rope;
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

void deceptusGrabRopesTick(DeceptusEngine *engine)
{
   DeceptusGrabRope *rope;
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

void deceptusGrabRopePin(DeceptusEngine *engine, DeceptusU8 index, DeceptusU8 node, DeceptusFixed x, DeceptusFixed y)
{
   DeceptusGrabRope *rope;
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

DeceptusU16 deceptusGrabRopeLength(const DeceptusEngine *engine, DeceptusU8 index)
{
   const DeceptusGrabRope *rope;
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

DeceptusU8 deceptusGrabRopePointAt(const DeceptusEngine *engine, DeceptusU8 index, DeceptusU16 distance_px, DeceptusI16 *x, DeceptusI16 *y)
{
   const DeceptusGrabRope *rope;
   DeceptusU16 remaining, length;
   DeceptusI32 dx, dy;
   DeceptusU8 node;
   if (index >= engine->grab_ropes.count || !x || !y)
   {
      return 0;
   }
   rope = &engine->grab_ropes.ropes[index];
   if (!engine->mechanisms.states[rope->mechanism_index].enabled)
   {
      return 0;
   }
   remaining = distance_px > 2047 ? 32752 : distance_px * 16;
   for (node = 1; node < rope->node_count; ++node)
   {
      dx = (DeceptusI32)rope->x[node] - rope->x[node - 1];
      dy = (DeceptusI32)rope->y[node] - rope->y[node - 1];
      length = rope->cumulative_length[node] - rope->cumulative_length[node - 1];
      if (remaining <= length && length)
      {
         *x = deceptusFloor((DeceptusFixed)(rope->x[node - 1] + quotient(product(dx, remaining), length)));
         *y = deceptusFloor((DeceptusFixed)(rope->y[node - 1] + quotient(product(dy, remaining), length)));
         return 1;
      }
      remaining -= length;
   }
   *x = deceptusFloor(rope->x[rope->node_count - 1]);
   *y = deceptusFloor(rope->y[rope->node_count - 1]);
   return 1;
}
