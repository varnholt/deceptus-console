#ifdef GBA_GRAB_ROPE_FIRST
#if GBA_CAMPAIGN
#include "area0/gba_rope_resources.h"
#else
#include "gba_rope_resources.h"
#endif
#include "deceptus/grabrope.h"

extern DeceptusI32 __aeabi_idiv(DeceptusI32 numerator, DeceptusI32 denominator);

static const DeceptusI16 rope_sines[32] = {0, 50,  98,  142,  181,  213,  237,  251,  256,  251,  237,  213,  181,  142,  98,  50,
                                           0, -50, -98, -142, -181, -213, -237, -251, -256, -251, -237, -213, -181, -142, -98, -50};

static DeceptusU16 ropeAngle(DeceptusI16 x, DeceptusI16 y)
{
   DeceptusI16 small, large, swap;
   DeceptusU16 angle;
   small = x < 0 ? -x : x;
   large = y < 0 ? -y : y;
   swap = small > large;
   if (swap)
   {
      angle = small;
      small = large;
      large = angle;
   }
   small *= 1024;
   angle = small < large * 101 ? 0 : small < large * 311 ? 1 : small < large * 548 ? 2 : small < large * 840 ? 3 : 4;
   if (swap)
   {
      angle = 8 - angle;
   }
   if (y < 0)
   {
      angle = 16 - angle;
   }
   return (x < 0 ? 32 - angle : angle) & 31;
}

static void __attribute__((section(".iwram"), noinline)) ropeObjects(DeceptusU16 *count)
{
   DeceptusU16 angle, best_angle, distance, length, height, slice, tile, along, remaining, interval;
   DeceptusU8 rope, node, previous_node;
   DeceptusI16 x, y, end_x, end_y, dx, dy, center, cosine, sine;
   DeceptusI32 reciprocal, delta_x, delta_y;
   GbaObject *object;
   const DeceptusGrabRope *chain;
   deceptus_debug[71] = 0;
   if (!engine.grab_ropes.count)
   {
      return;
   }
   for (angle = 0; angle < 32; ++angle)
   {
      sine = rope_sines[angle];
      cosine = rope_sines[(angle + 8) & 31];
      objects[angle * 4].affine = (DeceptusU16)cosine;
      objects[angle * 4 + 1].affine = (DeceptusU16)-sine;
      objects[angle * 4 + 2].affine = (DeceptusU16)sine;
      objects[angle * 4 + 3].affine = (DeceptusU16)cosine;
   }
   for (rope = 0; rope < engine.grab_ropes.count; ++rope)
   {
      chain = &engine.grab_ropes.ropes[rope];
      if (!engine.mechanisms.states[chain->mechanism_index].visible)
      {
         continue;
      }
      length = deceptusGrabRopeLength(&engine, rope);
      x = deceptusFloor(chain->x[0]);
      y = deceptusFloor(chain->y[0]);
      node = 1;
      previous_node = 255;
      reciprocal = 0;
      for (distance = 0; distance < length; distance += height)
      {
         height = distance ? 8 : 16;
         if (height > length - distance)
         {
            height = length - distance;
         }
         slice = distance ? height : 0;
         /* walk cached arc intervals once and share their reciprocal across
          * coordinates and neighbouring strips. */
         along = (distance + height) * 16;
         while (node + 1 < chain->node_count && chain->cumulative_length[node] < along)
         {
            ++node;
         }
         interval = chain->cumulative_length[node] - chain->cumulative_length[node - 1];
         remaining = along - chain->cumulative_length[node - 1];
         if (remaining > interval)
         {
            remaining = interval;
         }
         if (node != previous_node)
         {
            reciprocal = interval ? __aeabi_idiv(65536, interval) : 0;
            previous_node = node;
         }
         delta_x = (DeceptusI32)chain->x[node] - chain->x[node - 1];
         delta_y = (DeceptusI32)chain->y[node] - chain->y[node - 1];
         end_x = deceptusFloor((DeceptusFixed)(chain->x[node - 1] + delta_x * remaining * reciprocal / 65536));
         end_y = deceptusFloor((DeceptusFixed)(chain->y[node - 1] + delta_y * remaining * reciprocal / 65536));
         dx = end_x - x;
         dy = end_y - y;
         best_angle = ropeAngle(dx, dy);
         /* affine matrices rotate native pixels without scaling. all pieces
          * share 32 directions, independent of the number of visible ropes. */
         center = 8 - gba_rope_slice_tops[slice];
         x += rope_sines[best_angle] * center / 256 - frame.camera_x - 16;
         y += rope_sines[(best_angle + 8) & 31] * center / 256 - frame.camera_y - 16;
         if (x <= -32 || x >= 240 || y <= -32 || y >= 160)
         {
            x = end_x;
            y = end_y;
            continue;
         }
         tile = mechanismResource(GBA_GRAB_ROPE_FIRST + slice);
         if (tile == 65535 || *count >= 128)
         {
            engine.content_error = engine.content_error ? engine.content_error : 4;
            return;
         }
         object = &objects[(*count)++];
         ++deceptus_debug[71];
         object->attribute0 = ((DeceptusU16)y & 255) | (1 << 8) | (1 << 9) | (1 << 13);
         object->attribute1 = ((DeceptusU16)x & 511) | (best_angle << 9) | (1 << 14);
         object->attribute2 = tile + (1 << 10);
         x = end_x;
         y = end_y;
      }
   }
}
#else
static void ropeObjects(DeceptusU16 *count)
{
   (void)count;
}
#endif
