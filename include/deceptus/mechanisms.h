#ifndef DECEPTUS_MECHANISMS_H
#define DECEPTUS_MECHANISMS_H

#include "deceptus/types.h"

#define DECEPTUS_MECHANISM_CAPACITY 32
#define DECEPTUS_MECHANISM_EVENT_CAPACITY 32
#define DECEPTUS_SOLID_BIN_COUNT 32
#define DECEPTUS_SOLID_BIN_SIZE 64
#define DECEPTUS_MECH_CHECKPOINT 1
#define DECEPTUS_MECH_SENSOR 2
#define DECEPTUS_MECH_LEVER 3
#define DECEPTUS_MECH_BLOCK 4
#define DECEPTUS_MECH_DAMAGE 5
#define DECEPTUS_MECH_FLAG 6
#define DECEPTUS_MECH_SPIKES 7
#define DECEPTUS_MECH_COLLAPSING 8
#define DECEPTUS_MECH_WIND 9
#define DECEPTUS_MECH_DOOR 10
#define DECEPTUS_MECH_DESTRUCTIBLE 11
#define DECEPTUS_MECH_CANNON_BODY 12
#define DECEPTUS_MECH_GRAB_ROPE 13
#define DECEPTUS_MECH_MOVEABLE_BOX 14
/* stops moveable boxes only; bodies pass through it. */
#define DECEPTUS_MECH_BOX_COLLIDER 15
#define DECEPTUS_MOVEABLE_BOX_CAPACITY 2
/* a spiked blade on a chain that drops from the ceiling and lifts again; value holds its travel in
 * pixels, on_ticks and off_ticks how long it waits lowered and raised. */
#define MECHANISM_CRUSHER 16
/* one tile of a spike layer (floorspikes.h). */
#define MECHANISM_FLOOR_SPIKES 17
/* a spiked ball swinging on a chain from its anchor tile (spikeball.h). */
#define MECHANISM_SPIKE_BALL 18
#define SPIKE_BALL_CAPACITY 2
#define DECEPTUS_TIMED_ANIMATION 1
#define DECEPTUS_TIMED_INTERVAL 2
#define DECEPTUS_COLLAPSE_ONCE_TOUCHED 4
#define DECEPTUS_MECH_INVERTED 1
#define DECEPTUS_MECH_MISSING_HANDLE 2
#define DECEPTUS_MECH_INVERT_TARGETS 4
#define DECEPTUS_MECH_RIGHT_ALIGNED 8
/* drawn in front of the player: the source draws mechanisms above the player's depth (20) after her. */
#define MECHANISM_IN_FRONT 64
#define DECEPTUS_MECH_ENTER 1
#define DECEPTUS_MECH_LEAVE 2
#define DECEPTUS_MECH_ENABLED 3
#define DECEPTUS_MECH_REACHED 4
#define DECEPTUS_MECH_HIT 5
#define DECEPTUS_MECH_COLLAPSED 6
#define DECEPTUS_MECH_RESTORED 7
#define DECEPTUS_MECH_FORCE 8

typedef struct DeceptusRect
{
   DeceptusI16 x;
   DeceptusI16 y;
   DeceptusI16 width;
   DeceptusI16 height;
} DeceptusRect;

/* source_id is the original Tiled object id, not its position in an array. */
typedef struct DeceptusMechanismDef
{
   DeceptusU16 source_id;
   DeceptusRect bounds;
   DeceptusU16 target_first;
   DeceptusU16 target_count;
   DeceptusU16 value;
   DeceptusU8 kind;
   DeceptusU8 enabled;
   DeceptusU8 flags;
} DeceptusMechanismDef;

typedef struct DeceptusMechanismTiming
{
   DeceptusU16 on_ticks;
   DeceptusU16 off_ticks;
   DeceptusU16 collapse_ticks;
   DeceptusU16 respawn_ticks;
   DeceptusU16 fade_ticks;
   DeceptusU8 flags;
   /* how far into its cycle a periodic mechanism starts: the source's time_offset properties. */
   DeceptusU16 phase_offset_ticks;
} DeceptusMechanismTiming;

typedef struct DeceptusMechanismState
{
   DeceptusU8 enabled;
   DeceptusU8 visible;
   DeceptusU8 inside;
   DeceptusU8 triggered;
   DeceptusU8 active;
   DeceptusU8 frame;
   DeceptusU8 target_frame;
   DeceptusU8 fraction;
   DeceptusU8 progress;
   DeceptusU8 phase;
   DeceptusU8 pending;
   DeceptusU8 pending_count;
   DeceptusU8 handle_available;
   DeceptusU16 timer;
   DeceptusU16 cooldown;
   DeceptusU16 idle_ticks;
} DeceptusMechanismState;

/* a pushable box where it is now; its definition bounds hold where the room places it. */
typedef struct DeceptusMoveableBox
{
   DeceptusRect bounds;
   DeceptusFixed x, y, velocity_y;
   DeceptusU8 mechanism_index;
} DeceptusMoveableBox;

/* a swinging spike ball: where its ball is now and was a tick ago, relative to its anchor in 1/256 px. */
typedef struct SpikeBall
{
   DeceptusI32 x, y, previous_x, previous_y;
   DeceptusU8 mechanism_index;
} SpikeBall;

typedef struct DeceptusMechanismEvent
{
   DeceptusU16 source_id;
   DeceptusU16 value;
   DeceptusU8 kind;
} DeceptusMechanismEvent;

typedef struct DeceptusMechanismSet
{
   const DeceptusMechanismDef* definitions;
   const DeceptusU16* targets;
   const DeceptusMechanismTiming* timings;
   DeceptusMechanismState states[DECEPTUS_MECHANISM_CAPACITY];
   DeceptusMechanismEvent events[DECEPTUS_MECHANISM_EVENT_CAPACITY];
   DeceptusU16 target_count;
   DeceptusU8 count;
   /* the mechanisms carrying MECHANISM_IN_FRONT, drawn before the player. */
   DeceptusU8 front_count;
   DeceptusU8 front_indices[DECEPTUS_MECHANISM_CAPACITY];
   DeceptusU8 event_count;
   DeceptusU8 overflow;
   DeceptusU8 solid_count;
   DeceptusU8 solid_indices[DECEPTUS_MECHANISM_CAPACITY];
   DeceptusU32 solid_mask, solid_outside;
   DeceptusU32 solid_columns[DECEPTUS_SOLID_BIN_COUNT];
   DeceptusU32 solid_rows[DECEPTUS_SOLID_BIN_COUNT];
   DeceptusMoveableBox boxes[DECEPTUS_MOVEABLE_BOX_CAPACITY];
   SpikeBall spike_balls[SPIKE_BALL_CAPACITY];
   DeceptusU8 spike_ball_count;
   DeceptusU8 box_count;
} DeceptusMechanismSet;

/* definitions remain unchanged until reinitialization. invalid or oversized
 * definitions fail without leaving a partially bound set. */
DeceptusU8 deceptusMechanismsInit(
   DeceptusMechanismSet* set,
   const DeceptusMechanismDef* definitions,
   DeceptusU16 count,
   const DeceptusU16* targets,
   DeceptusU16 target_count
);
void deceptusMechanismsConfigure(DeceptusMechanismSet* set, const DeceptusMechanismTiming* timings);
DeceptusI16 deceptusMechanismFind(const DeceptusMechanismSet* set, DeceptusU16 source_id);
/* where the mechanism at index is now: a moveable box leaves its definition bounds. */
const DeceptusRect* deceptusMechanismBounds(const DeceptusMechanismSet* set, DeceptusU16 index);
DeceptusU8 deceptusMechanismEnable(DeceptusMechanismSet* set, DeceptusU16 source_id, DeceptusU8 enabled);
/* applies one weapon hit to the original destructible blocker durability. */
DeceptusU8 deceptusMechanismHit(DeceptusMechanismSet* set, DeceptusU16 source_id, DeceptusU16 damage);
/* clears the transient event queue and evaluates one frame of rectangle interactions. */
void deceptusMechanismsTick(DeceptusMechanismSet* set, const DeceptusRect* player, DeceptusU8 interact_pressed);
DeceptusU8 deceptusMechanismSolid(const DeceptusMechanismSet* set, const DeceptusRect* bounds);
/* source cannon gun projectiles ignore cannon enemy fixtures. */
DeceptusU8 deceptusMechanismProjectileSolid(const DeceptusMechanismSet* set, const DeceptusRect* bounds);
/* a body moving sideways meets moveable boxes at their exact edge (deceptusMoveHorizontalDynamic),
 * not at the edge of the collision cell they cover, so this leaves them out. */
DeceptusU8 deceptusMechanismSidewaysSolid(const DeceptusMechanismSet* set, const DeceptusRect* bounds);
/* what stops the box at mechanism index box: the bodies' solids, other boxes and box colliders. */
DeceptusU8 deceptusMechanismBoxSolid(const DeceptusMechanismSet* set, const DeceptusRect* bounds, DeceptusU8 box);

#endif
