#ifndef DECEPTUS_GRABROPE_H
#define DECEPTUS_GRABROPE_H
#include "deceptus/types.h"

#define DECEPTUS_GRAB_ROPE_CAPACITY 2
#define DECEPTUS_GRAB_ROPE_NODES 24

typedef struct deceptus_grabrope {
    deceptus_fixed x[DECEPTUS_GRAB_ROPE_NODES], y[DECEPTUS_GRAB_ROPE_NODES];
    deceptus_fixed previous_x[DECEPTUS_GRAB_ROPE_NODES], previous_y[DECEPTUS_GRAB_ROPE_NODES];
    deceptus_u16 cumulative_length[DECEPTUS_GRAB_ROPE_NODES], total_length;
    deceptus_u8 mechanism_index, node_count, damping_ticks, dynamic_solids;
} deceptus_grabrope;

typedef struct deceptus_grabrope_set {
    deceptus_grabrope ropes[DECEPTUS_GRAB_ROPE_CAPACITY];
    deceptus_u8 count;
} deceptus_grabrope_set;

struct deceptus_engine;
deceptus_u8 deceptus_grabropes_init(struct deceptus_engine *engine);
void deceptus_grabropes_tick(struct deceptus_engine *engine);
void deceptus_grabrope_pin(struct deceptus_engine *engine, deceptus_u8 rope, deceptus_u8 node, deceptus_fixed x,
                           deceptus_fixed y);
deceptus_u16 deceptus_grabrope_distance(deceptus_i32 x, deceptus_i32 y);
deceptus_u16 deceptus_grabrope_length(const struct deceptus_engine *engine, deceptus_u8 rope);
deceptus_u8 deceptus_grabrope_point_at(const struct deceptus_engine *engine, deceptus_u8 rope, deceptus_u16 distance_px,
                                       deceptus_i16 *x, deceptus_i16 *y);
#endif
