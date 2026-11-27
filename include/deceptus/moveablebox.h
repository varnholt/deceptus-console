#ifndef DECEPTUS_MOVEABLEBOX_H
#define DECEPTUS_MOVEABLEBOX_H
#include "deceptus/types.h"

struct DeceptusEngine;
struct DeceptusEntity;

/* places the room's moveable boxes where their definitions put them; zero when the room holds more
 * than DECEPTUS_MOVEABLE_BOX_CAPACITY. */
DeceptusU8 deceptusMoveableBoxesInit(struct DeceptusEngine* engine);
/* one tick of gravity for every box. */
void deceptusMoveableBoxesFall(struct DeceptusEngine* engine);
/* a body about to walk into a box this tick pushes it first; its own sideways move then stops at the
 * box's new edge. */
void deceptusMoveableBoxesPush(struct DeceptusEngine* engine, const struct DeceptusEntity* body, DeceptusU8 half_width, DeceptusU8 height);

#endif
