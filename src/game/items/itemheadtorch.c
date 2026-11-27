#include "deceptus/itemheadtorch.h"

#define HEADTORCH_ITEM 5

DeceptusU8 deceptusItemHeadtorchEquipped(const DeceptusAdventure* game)
{
   return game->inventory.counts[HEADTORCH_ITEM] != 0;
}
