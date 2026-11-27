#ifndef CHECKPOINT_CLOCK_H
#define CHECKPOINT_CLOCK_H
#include "deceptus/mechanisms.h"

/* checkpoint flags: an invisible checkpoint only saves (the source's visible false), and the clock of
 * a raised checkpoint stands one source tile above its trigger rectangle (its sprite_pos). */
#define CHECKPOINT_HIDDEN 16
#define CHECKPOINT_RAISED 32
#define CHECKPOINT_RAISE 24

/* clock images, in the order of the shared sprite family: the source's activation row, then its
 * ticking row. image 0 is also the clock that has not been reached yet. */
#define CHECKPOINT_CLOCK_ACTIVATION_FRAMES 20
#define CHECKPOINT_CLOCK_ACTIVE_FRAMES 14
#define CHECKPOINT_CLOCK_IMAGE_COUNT (CHECKPOINT_CLOCK_ACTIVATION_FRAMES + CHECKPOINT_CLOCK_ACTIVE_FRAMES)
#define CHECKPOINT_CLOCK_WIDTH 72
#define CHECKPOINT_CLOCK_HEIGHT 120

/* every clock of the room starts not reached. */
void checkpointClocksInit(DeceptusMechanismSet* set);
/* one tick of every clock: a reached clock plays its activation once, then ticks. */
void checkpointClocksTick(DeceptusMechanismSet* set);
/* nonzero when the checkpoint at mechanism index has a clock to draw. */
DeceptusU8 checkpointClockVisible(const DeceptusMechanismSet* set, DeceptusU16 index);
/* the image and room position of the clock at mechanism index. */
void checkpointClockPart(const DeceptusMechanismSet* set, DeceptusU16 index, DeceptusU8* image, DeceptusI16* x, DeceptusI16* y);

#endif
