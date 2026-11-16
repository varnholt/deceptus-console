#ifndef DECEPTUS_PLAYERROPE_H
#define DECEPTUS_PLAYERROPE_H
#include "deceptus/types.h"

#define DECEPTUS_PLAYER_ROPE_HAND_OFFSET 20

typedef struct DeceptusPlayerRope
{
   DeceptusU16 radius, link_length;
   DeceptusU8 attached, rope_index, node_index, regrab_ticks, grace_ticks;
} DeceptusPlayerRope;

struct DeceptusEngine;
struct DeceptusInput;
void deceptusPlayerRopeReset(struct DeceptusEngine *engine);
DeceptusU8 deceptusPlayerRopeTick(struct DeceptusEngine *engine, const struct DeceptusInput *input);
#endif
