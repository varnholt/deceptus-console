#ifndef DECEPTUS_ENEMIES_H
#define DECEPTUS_ENEMIES_H
#include "deceptus/engine.h"
#define DECEPTUS_RAT 4
#define DECEPTUS_ARROW_TRAP 5
#define DECEPTUS_ARROW 6
#define DECEPTUS_ARROW_MUZZLE 7
#define DECEPTUS_BLOB_2 9
#define DECEPTUS_CANNON_2 12
#define DECEPTUS_CANNONBALL 13
#define DECEPTUS_ORB 16
#define DECEPTUS_FROG_BLUE 17
#define DECEPTUS_BLUE_FIREBALL 18

typedef struct DeceptusPathPoint
{
   DeceptusI16 x, y;
} DeceptusPathPoint;
typedef struct DeceptusEnemyDef
{
   DeceptusU16 source_id;
   DeceptusI16 x, y;
   DeceptusU16 path_first, path_count, time_offset;
   DeceptusFixed speed, acceleration;
   DeceptusU16 damage, period, delays[3];
   DeceptusU8 kind, alignment, cycles[5], animation_fps, idle_ticks;
} DeceptusEnemyDef;

void deceptusEnemiesInit(DeceptusEngine *engine);
DeceptusU16 deceptusEnemiesTick(DeceptusEngine *engine);
void deceptusEnemiesRender(const DeceptusEngine *engine, DeceptusFrame *frame);
#endif
