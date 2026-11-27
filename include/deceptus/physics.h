#ifndef DECEPTUS_PHYSICS_H
#define DECEPTUS_PHYSICS_H

#include "deceptus/engine.h"

/* signed room-local fixed position to collision-cell coordinate. */
DeceptusI16 deceptusPositionCell(DeceptusFixed position);
DeceptusU8 deceptusBodyClear(
   const DeceptusRoom* room,
   const DeceptusMechanismSet* mechanisms,
   DeceptusFixed x,
   DeceptusFixed y,
   DeceptusU8 half_width,
   DeceptusU8 height
);
/* axis-separated swept body movement. velocities must be less than one cell
 * per tick; the caller supplies its body size and applies acceleration. */
void deceptusMoveHorizontal(const DeceptusRoom* room, DeceptusEntity* entity, DeceptusU8 half_width, DeceptusU8 height);
void deceptusMoveVertical(const DeceptusRoom* room, DeceptusEntity* entity, DeceptusU8 half_width, DeceptusU8 height);
void deceptusMoveHorizontalDynamic(
   const DeceptusRoom* room,
   DeceptusEntity* entity,
   DeceptusU8 half_width,
   DeceptusU8 height,
   const DeceptusMechanismSet* mechanisms
);
void deceptusMoveVerticalDynamic(
   const DeceptusRoom* room,
   DeceptusEntity* entity,
   DeceptusU8 half_width,
   DeceptusU8 height,
   const DeceptusMechanismSet* mechanisms
);

DeceptusI16 deceptusWallSide(const DeceptusRoom* room, const DeceptusEntity* entity, const DeceptusMechanismSet* mechanisms);

#endif
