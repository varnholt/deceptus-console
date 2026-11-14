#ifndef DECEPTUS_PLAYERSWORD_H
#define DECEPTUS_PLAYERSWORD_H

#include "deceptus/engine.h"

/* caller-owned sword state; enemy damage resets with the room's enemies. */
typedef struct deceptus_playersword {
    deceptus_u8 ticks, duration, facing_left, crouched, airborne, hit;
    deceptus_u8 damage[DECEPTUS_ENTITY_CAPACITY];
} deceptus_playersword;

void deceptus_playersword_init(deceptus_playersword *sword);
void deceptus_playersword_tick(deceptus_playersword *sword, deceptus_engine *engine, deceptus_u16 pressed);

#endif
