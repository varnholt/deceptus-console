#ifndef DECEPTUS_ROOMCONTENT_H
#define DECEPTUS_ROOMCONTENT_H
#include "deceptus/enemies.h"

/* buffers and input bytes are caller-owned and must outlive the loaded room. */
typedef struct DeceptusRoomContent
{
   DeceptusRoom room;
   DeceptusWaterConfiguration water;
   DeceptusMechanismDef* mechanisms;
   DeceptusMechanismTiming* timings;
   DeceptusU16* targets;
   DeceptusEnemyDef* enemies;
   DeceptusPathPoint* paths;
   DeceptusU16 mechanism_capacity, target_capacity, enemy_capacity, path_capacity;
} DeceptusRoomContent;

/* DRM1 uses explicit big-endian fields, independent of compiler structure layout. */
DeceptusU8 deceptusRoomContentLoad(DeceptusRoomContent* content, const DeceptusU8* bytes, DeceptusU32 size);
#endif
