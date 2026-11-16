#ifndef DECEPTUS_GRABROPE_H
#define DECEPTUS_GRABROPE_H
#include "deceptus/types.h"

#define DECEPTUS_GRAB_ROPE_CAPACITY 2
#define DECEPTUS_GRAB_ROPE_NODES 24

typedef struct DeceptusGrabRope
{
   DeceptusFixed x[DECEPTUS_GRAB_ROPE_NODES], y[DECEPTUS_GRAB_ROPE_NODES];
   DeceptusFixed previous_x[DECEPTUS_GRAB_ROPE_NODES], previous_y[DECEPTUS_GRAB_ROPE_NODES];
   DeceptusU16 cumulative_length[DECEPTUS_GRAB_ROPE_NODES], total_length;
   DeceptusU8 mechanism_index, node_count, damping_ticks, dynamic_solids;
} DeceptusGrabRope;

typedef struct DeceptusGrabRopeSet
{
   DeceptusGrabRope ropes[DECEPTUS_GRAB_ROPE_CAPACITY];
   DeceptusU8 count;
} DeceptusGrabRopeSet;

struct DeceptusEngine;
DeceptusU8 deceptusGrabRopesInit(struct DeceptusEngine *engine);
void deceptusGrabRopesTick(struct DeceptusEngine *engine);
void deceptusGrabRopePin(struct DeceptusEngine *engine, DeceptusU8 rope, DeceptusU8 node, DeceptusFixed x, DeceptusFixed y);
DeceptusU16 deceptusGrabRopeDistance(DeceptusI32 x, DeceptusI32 y);
DeceptusU16 deceptusGrabRopeLength(const struct DeceptusEngine *engine, DeceptusU8 rope);
DeceptusU8 deceptusGrabRopePointAt(const struct DeceptusEngine *engine, DeceptusU8 rope, DeceptusU16 distance_px, DeceptusI16 *x,
                                   DeceptusI16 *y);
#endif
