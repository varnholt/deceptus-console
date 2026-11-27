#ifndef DECEPTUS_ANIMATION_H
#define DECEPTUS_ANIMATION_H
#include "deceptus/types.h"

/* a target-independent clock for baked scenery, sprites and interface clips. */
typedef struct DeceptusAnimation
{
   DeceptusU16 frame, elapsed;
} DeceptusAnimation;

void deceptusAnimationReset(DeceptusAnimation* animation);
/* invalid clips stay at frame zero; returns one if the displayed frame changes. */
DeceptusU8
deceptusAnimationAdvance(DeceptusAnimation* animation, DeceptusU16 frames, DeceptusU16 frame_ticks, DeceptusU16 ticks, DeceptusU8 loop);
DeceptusU8 deceptusAnimationAdvanceTimed(
   DeceptusAnimation* animation,
   DeceptusU16 frames,
   const DeceptusU16* durations,
   DeceptusU16 ticks,
   DeceptusU8 loop
);
#endif
