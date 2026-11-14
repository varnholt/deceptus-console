#ifndef DECEPTUS_PLAYERROPE_H
#define DECEPTUS_PLAYERROPE_H
#include "deceptus/types.h"

#define DECEPTUS_PLAYER_ROPE_HAND_OFFSET 20

typedef struct deceptus_playerrope {
    deceptus_u16 radius, link_length;
    deceptus_u8 attached, rope_index, node_index, regrab_ticks, grace_ticks;
} deceptus_playerrope;

struct deceptus_engine;
struct deceptus_input;
void deceptus_playerrope_reset(struct deceptus_engine *engine);
deceptus_u8 deceptus_playerrope_tick(struct deceptus_engine *engine, const struct deceptus_input *input);
#endif
