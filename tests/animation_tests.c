#include <assert.h>
#include <stdio.h>
#include "deceptus/animation.h"

int main(void)
{
   static const DeceptusU16 durations[] = {120, 2, 1};
   DeceptusAnimation animation;
   DeceptusU16 index;
   deceptusAnimationReset(&animation);
   assert(!deceptusAnimationAdvance(&animation, 12, 6, 5, 1));
   assert(deceptusAnimationAdvance(&animation, 12, 6, 1, 1));
   assert(animation.frame == 1 && animation.elapsed == 0);
   assert(!deceptusAnimationAdvance(&animation, 12, 6, 0, 1));
   deceptusAnimationAdvance(&animation, 12, 6, 71, 1);
   assert(animation.frame == 0 && animation.elapsed == 5);
   /* repeated playback must remain stable beyond the engine's 16-bit tick wrap. */
   deceptusAnimationReset(&animation);
   for (index = 0; index < 40000; ++index)
   {
      deceptusAnimationAdvance(&animation, 12, 6, 2, 1);
   }
   assert(animation.frame == (80000UL / 6) % 12 && animation.elapsed == 80000UL % 6);
   deceptusAnimationAdvance(&animation, 12, 6, 65535, 0);
   assert(animation.frame == 11 && animation.elapsed == 0);
   assert(!deceptusAnimationAdvance(&animation, 12, 6, 65535, 0));
   assert(deceptusAnimationAdvance(&animation, 0, 6, 10, 1));
   assert(animation.frame == 0 && animation.elapsed == 0);
   deceptusAnimationAdvance(&animation, 65535, 65535, 65535, 1);
   assert(animation.frame == 1 && animation.elapsed == 0);
   deceptusAnimationReset(&animation);
   deceptusAnimationAdvanceTimed(&animation, 3, durations, 119, 1);
   assert(animation.frame == 0 && animation.elapsed == 119);
   deceptusAnimationAdvanceTimed(&animation, 3, durations, 1, 1);
   assert(animation.frame == 1 && animation.elapsed == 0);
   deceptusAnimationAdvanceTimed(&animation, 3, durations, 3, 1);
   assert(animation.frame == 0 && animation.elapsed == 0);
   puts("animation playback checks passed");
   return 0;
}
