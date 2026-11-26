#ifndef SPIKE_BALL_H
#define SPIKE_BALL_H
#include "deceptus/mechanisms.h"

/* spike ball images, in the order of the shared sprite family: the ball turned from pointing right
 * (image 0) through hanging down to pointing left (image SPIKE_BALL_ROTATIONS - 1), then the two
 * alternating chain links. ball images are SPIKE_BALL_FRAME_SIZE square around the ball's centre. */
#define SPIKE_BALL_ROTATIONS 17
#define SPIKE_BALL_IMAGE_LINK_A SPIKE_BALL_ROTATIONS
#define SPIKE_BALL_IMAGE_LINK_B (SPIKE_BALL_ROTATIONS + 1)
#define SPIKE_BALL_IMAGE_COUNT (SPIKE_BALL_ROTATIONS + 2)
#define SPIKE_BALL_FRAME_SIZE 64
#define SPIKE_BALL_LINK_SIZE 8
/* the ball and the chain links between the anchor and the ball. */
#define SPIKE_BALL_LINKS 7
#define SPIKE_BALL_PART_COUNT (1 + SPIKE_BALL_LINKS)

/* every spike ball of the room starts held out to the right of its anchor, as in the source. */
void spikeBallsInit(DeceptusMechanismSet *set);
/* one tick of every spike ball; nonzero when a ball touches the body at bounds. */
DeceptusU8 spikeBallsTick(DeceptusMechanismSet *set, const DeceptusRect *bounds);
/* the image and room position of one piece of the spike ball at mechanism index:
 * part 0 is the ball, the others the chain links, part < SPIKE_BALL_PART_COUNT. */
void spikeBallPart(const DeceptusMechanismSet *set, DeceptusU16 index, DeceptusU8 part, DeceptusU8 *image, DeceptusI16 *x, DeceptusI16 *y);

#endif
