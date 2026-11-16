#include "deceptus/animation.h"

/* bounded integer division without a platform compiler-runtime dependency. */
static DeceptusU32 divide(DeceptusU32 value, DeceptusU16 divisor, DeceptusU16 *remainder)
{
   DeceptusU32 denominator, bit, result;
   denominator = divisor;
   bit = 1;
   result = 0;
   while (denominator <= (value >> 1))
   {
      denominator <<= 1;
      bit <<= 1;
   }
   while (bit)
   {
      if (value >= denominator)
      {
         value -= denominator;
         result |= bit;
      }
      denominator >>= 1;
      bit >>= 1;
   }
   *remainder = (DeceptusU16)value;
   return result;
}

void deceptusAnimationReset(DeceptusAnimation *animation)
{
   animation->frame = 0;
   animation->elapsed = 0;
}

DeceptusU8 deceptusAnimationAdvanceTimed(DeceptusAnimation *animation, DeceptusU16 frames, const DeceptusU16 *durations, DeceptusU16 ticks,
                                         DeceptusU8 loop)
{
   DeceptusU16 previous, remaining, step;
   previous = animation->frame;
   if (!frames || !durations || animation->frame >= frames)
   {
      deceptusAnimationReset(animation);
      return previous != 0;
   }
   while (ticks)
   {
      if (!durations[animation->frame] || animation->elapsed >= durations[animation->frame])
      {
         deceptusAnimationReset(animation);
         return previous != 0;
      }
      remaining = durations[animation->frame] - animation->elapsed;
      step = ticks < remaining ? ticks : remaining;
      animation->elapsed += step;
      ticks -= step;
      if (animation->elapsed == durations[animation->frame])
      {
         animation->elapsed = 0;
         if (animation->frame + 1 < frames)
         {
            ++animation->frame;
         }
         else if (loop)
         {
            animation->frame = 0;
         }
         else
         {
            return previous != animation->frame;
         }
      }
   }
   return previous != animation->frame;
}

DeceptusU8 deceptusAnimationAdvance(DeceptusAnimation *animation, DeceptusU16 frames, DeceptusU16 frame_ticks, DeceptusU16 ticks,
                                    DeceptusU8 loop)
{
   DeceptusU32 elapsed, frame;
   DeceptusU16 previous, remainder;
   previous = animation->frame;
   if (!frames || !frame_ticks)
   {
      deceptusAnimationReset(animation);
      return previous != 0;
   }
   elapsed = (DeceptusU32)animation->elapsed + ticks;
   frame = (DeceptusU32)animation->frame + divide(elapsed, frame_ticks, &remainder);
   if (loop)
   {
      animation->elapsed = remainder;
      divide(frame, frames, &remainder);
      animation->frame = remainder;
   }
   else if (frame >= frames - 1U)
   {
      animation->frame = frames - 1;
      animation->elapsed = 0;
   }
   else
   {
      animation->frame = (DeceptusU16)frame;
      animation->elapsed = remainder;
   }
   return previous != animation->frame;
}
