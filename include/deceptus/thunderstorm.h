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
/* one colour channel (0 .. maximum) of the lit sky at a flash level. thunderstormoverlay.cpp blends a grey
   as bright as the flash, with the flash as its alpha, so weak flashes even dim a bright sky a little. */
DeceptusU8 thunderstormChannel(DeceptusU8 channel, DeceptusU8 maximum, DeceptusU8 level);

#endif
