#ifndef DECEPTUS_PLAYERSWORD_H
#define DECEPTUS_PLAYERSWORD_H

#include "deceptus/engine.h"

/* caller-owned sword state; enemy damage resets with the room's enemies. */
typedef struct DeceptusPlayerSword
{
   DeceptusU8 ticks, duration, facing_left, crouched, airborne, hit;
   DeceptusU8 damage[DECEPTUS_ENTITY_CAPACITY];
} DeceptusPlayerSword;

void deceptusPlayerSwordInit(DeceptusPlayerSword *sword);
void deceptusPlayerSwordTick(DeceptusPlayerSword *sword, DeceptusEngine *engine, DeceptusU16 pressed);

#endif
