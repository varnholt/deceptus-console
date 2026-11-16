#ifndef DECEPTUS_TYPES_H
#define DECEPTUS_TYPES_H

#include <stdint.h>

typedef int8_t DeceptusI8;
typedef uint8_t DeceptusU8;
typedef int16_t DeceptusI16;
typedef uint16_t DeceptusU16;
typedef int32_t DeceptusI32;
typedef uint32_t DeceptusU32;

/* signed 12.4 room-local positions: -2048..2047px, with 1/16px precision.
 * room/chunk coordinates are separate from simulation positions. */
typedef DeceptusI16 DeceptusFixed;
#define DECEPTUS_ONE 16
#define DECEPTUS_FIXED(pixel) ((DeceptusFixed)(pixel) * DECEPTUS_ONE)

/* floor division, including negative coordinates at room boundaries. */
DeceptusI16 deceptusFloor(DeceptusFixed value);

#endif
