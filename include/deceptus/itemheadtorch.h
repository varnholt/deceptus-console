#ifndef DECEPTUS_ITEMHEADTORCH_H
#define DECEPTUS_ITEMHEADTORCH_H

#include "deceptus/adventure.h"

typedef struct DeceptusHeadtorchPose
{
   DeceptusI16 x, y;
   DeceptusU8 side, valid;
} DeceptusHeadtorchPose;

DeceptusU8 deceptusItemHeadtorchEquipped(const DeceptusAdventure *game);

#endif
