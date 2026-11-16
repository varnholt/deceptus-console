#ifndef DECEPTUS_ENGINE_H
#define DECEPTUS_ENGINE_H

#include "deceptus/grabrope.h"
#include "deceptus/mechanisms.h"
#include "deceptus/playerrope.h"
#include "deceptus/types.h"

#define DECEPTUS_ENTITY_CAPACITY 12
#define DECEPTUS_EVENT_CAPACITY 8
#define DECEPTUS_DRAW_CAPACITY 24
#define DECEPTUS_CELL_SIZE 8

#define DECEPTUS_LEFT 1
#define DECEPTUS_RIGHT 2
#define DECEPTUS_JUMP 4
#define DECEPTUS_UP 8
#define DECEPTUS_PAUSE 16
#define DECEPTUS_RESTART 32
#define DECEPTUS_DOWN 64
#define DECEPTUS_ACCEPT 128
#define DECEPTUS_BACK 256
#define DECEPTUS_ATTACK 512
#define DECEPTUS_SKILL_WALL_JUMP 1
#define DECEPTUS_SKILL_CRAWL 2
#define DECEPTUS_PLAYER_HEIGHT(player) ((player)->crouched ? 18 : 30)

#define DECEPTUS_EMPTY 0
#define DECEPTUS_SOLID 1
#define DECEPTUS_ONE_WAY 2
#define DECEPTUS_HAZARD 4
#define DECEPTUS_WATER 8

#define DECEPTUS_PLAYER 1
#define DECEPTUS_PATROL 2
#define DECEPTUS_DOOR 3
#define DECEPTUS_PLAYER_SWIM 8
#define DECEPTUS_WATER_SPLASH 10
#define DECEPTUS_PLAYER_ROPE 19
#define DECEPTUS_PLAYER_SWORD 11
#define DECEPTUS_HEADTORCH 14
#define DECEPTUS_HEADTORCH_BEAM 15

#define DECEPTUS_EVENT_JUMP 1
#define DECEPTUS_EVENT_HURT 2
#define DECEPTUS_EVENT_DOOR 3
#define DECEPTUS_EVENT_CHECKPOINT 4
#define DECEPTUS_EVENT_ITEM 5
#define DECEPTUS_EVENT_LEVER 6
#define DECEPTUS_EVENT_LEVER_INSERT 7
#define DECEPTUS_EVENT_DIALOGUE 8
#define DECEPTUS_EVENT_STEP 9
#define DECEPTUS_EVENT_LAND 10

typedef struct DeceptusInput
{
   DeceptusU16 held;
   DeceptusU16 pressed;
   DeceptusU16 released;
} DeceptusInput;

struct DeceptusEnemyDef;
struct DeceptusPathPoint;

typedef struct DeceptusWaterConfiguration
{
   DeceptusU8 speed, rise_speed, sink_speed, stroke_delay_ticks, gravity, stroke_force;
   DeceptusU8 splash_durations[20];
} DeceptusWaterConfiguration;

typedef struct DeceptusRoom
{
   const DeceptusU8 *collision;
   DeceptusU16 columns;
   DeceptusU16 rows;
   DeceptusI16 spawn_x;
   DeceptusI16 spawn_y;
   /* negative x omits the prototype patrol or portal. */
   DeceptusI16 patrol_x;
   DeceptusI16 patrol_y;
   DeceptusI16 door_x;
   DeceptusI16 door_y;
   const DeceptusMechanismDef *mechanisms;
   DeceptusU16 mechanism_count;
   const DeceptusU16 *mechanism_targets;
   DeceptusU16 mechanism_target_count;
   const DeceptusMechanismTiming *mechanism_timings;
   const struct DeceptusEnemyDef *enemy_definitions;
   DeceptusU16 enemy_count;
   const struct DeceptusPathPoint *enemy_paths;
   DeceptusU16 enemy_path_count;
   DeceptusU8 max_health;
   DeceptusU8 skills;
   DeceptusU8 player_gravity;
   DeceptusU8 player_gravity_fraction;
   DeceptusU8 player_speed;
   DeceptusU8 player_fall_gravity;
   const DeceptusWaterConfiguration *water;
} DeceptusRoom;

typedef struct DeceptusEntity
{
   DeceptusFixed x;
   DeceptusFixed y;
   DeceptusFixed velocity_x;
   DeceptusFixed velocity_y;
   DeceptusU8 kind;
   DeceptusU8 active;
   DeceptusU8 grounded;
   DeceptusU8 facing_left;
   DeceptusU8 coyote_ticks;
   DeceptusU8 jump_buffer_ticks;
   DeceptusU8 animation_ticks;
   DeceptusU8 drop_ticks;
   DeceptusU8 gravity_fraction;
   DeceptusU8 crouched, bend_ticks;
   DeceptusU8 in_water, water_ticks;
   DeceptusU8 frame;
   DeceptusU8 definition, state, target, inverse, arrived, fired;
   DeceptusU16 timer, pause_ticks, random_seed;
} DeceptusEntity;

typedef struct DeceptusEvent
{
   DeceptusU8 kind;
   DeceptusU8 entity;
} DeceptusEvent;

typedef struct DeceptusDraw
{
   DeceptusI16 x;
   DeceptusI16 y;
   DeceptusU8 frame;
   DeceptusU8 palette;
   DeceptusU8 flip_x;
   DeceptusU8 kind;
} DeceptusDraw;

/* backend capabilities are explicit; gameplay does not assume a 256px screen. */
typedef struct DeceptusVideoCaps
{
   DeceptusU16 width;
   DeceptusU16 height;
   DeceptusU8 max_draws;
} DeceptusVideoCaps;

typedef struct DeceptusFrame
{
   DeceptusDraw draws[DECEPTUS_DRAW_CAPACITY];
   DeceptusU8 count;
   DeceptusI16 camera_x;
   DeceptusI16 camera_y;
} DeceptusFrame;

#define DECEPTUS_PROJECTILE_CAPACITY 16
typedef struct DeceptusProjectile
{
   DeceptusFixed x, y, velocity_x, velocity_y;
   DeceptusU16 age, damage;
   DeceptusU8 active, alignment, gravity;
   DeceptusU8 kind;
   DeceptusU8 owner;
   DeceptusU16 phase;
} DeceptusProjectile;

typedef struct DeceptusEngine
{
   const DeceptusRoom *room;
   DeceptusEntity entities[DECEPTUS_ENTITY_CAPACITY];
   DeceptusEvent events[DECEPTUS_EVENT_CAPACITY];
   DeceptusVideoCaps video;
   DeceptusU16 previous_buttons;
   DeceptusU16 ticks;
   DeceptusU16 door_entries;
   DeceptusI16 camera_x;
   DeceptusU8 event_count;
   DeceptusU8 event_overflow;
   DeceptusU8 paused;
   DeceptusU8 health;
   DeceptusU8 hurt_ticks;
   DeceptusMechanismSet mechanisms;
   DeceptusI16 checkpoint_x;
   DeceptusI16 checkpoint_y;
   DeceptusU16 checkpoint_id;
   DeceptusU8 content_error;
   DeceptusI16 camera_y;
   DeceptusProjectile projectiles[DECEPTUS_PROJECTILE_CAPACITY];
   DeceptusU16 deaths, projectiles_fired;
   DeceptusI16 splash_x, splash_y;
   DeceptusU8 splash_active, splash_frame, splash_elapsed;
   DeceptusGrabRopeSet grab_ropes;
   DeceptusPlayerRope player_rope;
} DeceptusEngine;

/* initializes caller-owned state; room and collision storage must outlive it. */
void deceptusInit(DeceptusEngine *engine, const DeceptusRoom *room, const DeceptusVideoCaps *video);
/* advances one 60Hz simulation tick; replaces the transient event queue. */
void deceptusTick(DeceptusEngine *engine, DeceptusU16 buttons);
/* builds a bounded list of logical metasprites without modifying simulation. */
void deceptusRender(const DeceptusEngine *engine, DeceptusFrame *frame);
/* move to a room entrance and immediately reframe the camera. */
void deceptusPlacePlayer(DeceptusEngine *engine, DeceptusI16 x, DeceptusI16 y);
void deceptusEventPush(DeceptusEngine *engine, DeceptusU8 kind);
/* returns solid outside horizontal/top boundaries, empty below the room. */
DeceptusU8 deceptusCell(const DeceptusRoom *room, DeceptusI16 column, DeceptusI16 row);
/* claims a zeroed entity slot; returns null when the pool is full. */
DeceptusEntity *deceptusSpawn(DeceptusEngine *engine, DeceptusU8 kind, DeceptusI16 x, DeceptusI16 y);

#endif
