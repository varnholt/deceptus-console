#ifndef MECHANISM_PARTS_H
#define MECHANISM_PARTS_H
#include "deceptus/engine.h"

/* one sprite a mechanism shows: a frame of a shared sprite family (appearance.h) at a room position.
 * every platform draws mechanisms from these parts, so their layout is described once. */
typedef struct MechanismPart
{
   DeceptusU16 image;
   DeceptusI16 x;
   DeceptusI16 y;
   DeceptusU8 family;
} MechanismPart;

/* how many parts the mechanism at index shows this frame; zero when it is hidden or has no sprite. */
DeceptusU8 mechanismPartCount(const DeceptusEngine* engine, DeceptusU16 index);
/* one of those parts, part < mechanismPartCount. */
void mechanismPart(const DeceptusEngine* engine, DeceptusU16 index, DeceptusU8 part, MechanismPart* result);

#endif
