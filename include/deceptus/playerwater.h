#ifndef DECEPTUS_PLAYER_WATER_H
#define DECEPTUS_PLAYER_WATER_H
#include "deceptus/engine.h"
DeceptusU8 deceptusPlayerWaterTick(DeceptusEngine *engine, const DeceptusInput *input);
void deceptusPlayerWaterRender(const DeceptusEngine *engine, DeceptusFrame *frame);
#endif
