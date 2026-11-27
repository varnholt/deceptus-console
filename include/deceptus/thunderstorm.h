#ifndef THUNDERSTORM_H
#define THUNDERSTORM_H
#include "deceptus/types.h"

/* the brightness of the sky behind a room's thunderstorm (thunderstormoverlay.cpp): 0 is no flash. */
#define THUNDERSTORM_MAX_LEVEL 16

typedef struct Thunderstorm
{
   DeceptusU16 tick;
   DeceptusU8 active;
} Thunderstorm;

/* one tick of the storm; active is whether the room's storm is on. switching on strikes at once. */
void thunderstormTick(Thunderstorm *storm, DeceptusU8 active);
/* the flash level now, 0 .. THUNDERSTORM_MAX_LEVEL. */
DeceptusU8 thunderstormLevel(const Thunderstorm *storm);

#endif
