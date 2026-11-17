#ifndef DECEPTUS_GAME_H
#define DECEPTUS_GAME_H

#include "deceptus/engine.h"

#define DECEPTUS_PLAYER_SWORD 11
#define DECEPTUS_HEADTORCH 14
#define DECEPTUS_HEADTORCH_BEAM 15

/* initializes caller-owned state; room and collision storage must outlive it. */
void deceptusInit(DeceptusEngine *engine, const DeceptusRoom *room, const DeceptusVideoCaps *video);
/* advances one 60Hz simulation tick; replaces the transient event queue. */
void deceptusTick(DeceptusEngine *engine, DeceptusU16 buttons);
/* builds a bounded list of logical metasprites without modifying simulation. */
void deceptusRender(const DeceptusEngine *engine, DeceptusFrame *frame);
/* move to a room entrance and immediately reframe the camera. */
void deceptusPlacePlayer(DeceptusEngine *engine, DeceptusI16 x, DeceptusI16 y);

#endif
