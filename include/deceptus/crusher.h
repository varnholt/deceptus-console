#ifndef CRUSHER_H
#define CRUSHER_H
#include "deceptus/mechanisms.h"

/* crusher images, in the order of the shared sprite family: the blade, the ceiling mount, then one
 * chain link per number of its top rows hidden behind the mount. */
#define CRUSHER_IMAGE_BLADE 0
#define CRUSHER_IMAGE_MOUNT 1
#define CRUSHER_IMAGE_LINK 2
#define CRUSHER_LINK_HEIGHT 24
#define CRUSHER_IMAGE_COUNT (CRUSHER_IMAGE_LINK + CRUSHER_LINK_HEIGHT)

/* every crusher of the room starts its first wait; needs the room's timings. */
void crushersInit(DeceptusMechanismSet *set);
/* one tick of every crusher; nonzero when a blade touches the body at bounds. */
DeceptusU8 crushersTick(DeceptusMechanismSet *set, const DeceptusRect *bounds);
/* the pieces the crusher at mechanism index draws this frame. */
DeceptusU8 crusherPartCount(const DeceptusMechanismSet *set, DeceptusU16 index);
/* the image and room position of one piece, part < crusherPartCount. */
void crusherPart(const DeceptusMechanismSet *set, DeceptusU16 index, DeceptusU8 part, DeceptusU8 *image, DeceptusI16 *x, DeceptusI16 *y);

#endif
