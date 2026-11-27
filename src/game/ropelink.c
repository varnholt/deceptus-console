/* original counterpart: the textured strip src/game/mechanisms/rope.cpp draws along its chain. the
 * consoles draw it as pre-turned straight pieces, picked without trigonometry. */
#include "deceptus/ropelink.h"
#include "arith.h"

/* ceil(log2(ROPE_LINK_ANGLES)): the search narrows the ROPE_LINK_ANGLES answers to one. */
#define SEARCH_STEPS 5

/* tan of the angles halfway between two piece images, in 1/256. */
static const DeceptusI16 angle_bounds[ROPE_LINK_ANGLES - 1] = {
   -2599, -844, -479, -312, -210, -137, -78, -25, 25, 78, 137, 210, 312, 479, 844, 2599};

DeceptusU8 ropeLinkAngle(DeceptusI16 dx, DeceptusI16 dy)
{
   DeceptusI32 across;
   DeceptusI16 down;
   DeceptusU8 low, high, middle, step;
   across = dx;
   down = dy;
   if (down < 0)
   {
      across = -across;
      down = (DeceptusI16)-down;
   }
   across <<= 8;
   /* the image is the number of bounds the direction lies at or beyond: a binary search over them,
    * since every comparison is a multiply. both factors are 16-bit; the primitive keeps gcc from
    * widening a constant-folded bound into a 32-bit library multiply. */
   low = 0;
   high = ROPE_LINK_ANGLES - 1;
   for (step = 0; step < SEARCH_STEPS; ++step)
   {
      if (low == high)
      {
         break;
      }
      middle = (DeceptusU8)((low + high) / 2);
      if (across < arithMulS16(down, angle_bounds[middle]))
      {
         high = middle;
      }
      else
      {
         low = (DeceptusU8)(middle + 1);
      }
   }
   return low;
}

void ropeLinkCell(DeceptusI16 x0, DeceptusI16 y0, DeceptusI16 x1, DeceptusI16 y1, DeceptusU16 covered, DeceptusU16 full,
                  DeceptusI16 *x, DeceptusI16 *y)
{
   DeceptusI16 dx, dy;
   dx = (DeceptusI16)(x1 - x0);
   dy = (DeceptusI16)(y1 - y0);
   if (covered == full)
   {
      /* every piece but the last: its centre is the middle of its points, without a division. */
      *x = (DeceptusI16)(x1 - dx / 2 - ROPE_LINK_CELL / 2);
      *y = (DeceptusI16)(y1 - dy / 2 - ROPE_LINK_CELL / 2);
      return;
   }
   if (covered == 0)
   {
      covered = 1;
   }
   /* the piece's centre lies half a full piece back from its end along the rope. */
   *x = (DeceptusI16)(x1 - (DeceptusI16)((DeceptusI32)dx * full / (2 * (DeceptusI32)covered)) - ROPE_LINK_CELL / 2);
   *y = (DeceptusI16)(y1 - (DeceptusI16)((DeceptusI32)dy * full / (2 * (DeceptusI32)covered)) - ROPE_LINK_CELL / 2);
}
