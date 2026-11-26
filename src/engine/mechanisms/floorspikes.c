/* original counterpart: src/game/mechanisms/spikes.cpp, the tiles of the interval_spikes, trap_spikes
 * and toggle_spikes layers. spikes placed as tiles have no properties, so the source's defaults hold:
 * 2 s up and down (the room's timings), 250 ms trap delay, animation speed 35, damage 100. */
#include "deceptus/floorspikes.h"

/* spikes.cpp frames: 0 unused, 1..5 extend, 6..14 retract; below frame 10 they hurt. */
#define EXTRACT_START 1
#define EXTRACT_END 5
#define RETRACT_START 6
#define RETRACT_END FLOOR_SPIKES_RETRACTED
#define DEADLY_BELOW (FLOOR_SPIKES_FRAMES - 5)
#define SPIKE_DAMAGE 100
/* trap_time_ms 250 at 60 ticks per second. */
#define TRAP_DELAY_TICKS 15
/* TOLERANCE_PX: the hurting rectangle is this much smaller than the tile on every side. */
#define TOLERANCE 5
#define TILE 24

/* the source advances _tu by speed * dt * (longest frame time / frame time): in 1/256 frames per tick
 * that is speed 35 * longest 120 * 256 / 60 ticks, divided by the frame's own time. */
#define FRACTION_ONE 256
#define FRAME_STEP_NUMERATOR (35L * 120L * FRACTION_ONE / 60L)
static const DeceptusU8 frame_times[FLOOR_SPIKES_FRAMES] = {1, 50, 50, 50, 100, 100, 120, 30, 30, 30, 30, 30, 30, 30, 30};

#define PHASE_RETRACTING 0
#define PHASE_EXTRACTING 1

/* one tick of animation towards frame limit, which it does not pass. */
static void advance(DeceptusMechanismState *state, DeceptusU8 limit)
{
   DeceptusU16 position, step;
   position = (DeceptusU16)(state->frame * FRACTION_ONE + state->fraction);
   step = (DeceptusU16)(FRAME_STEP_NUMERATOR / frame_times[state->frame]);
   position = (DeceptusU16)(position + step);
   if (position > limit * FRACTION_ONE)
   {
      position = (DeceptusU16)(limit * FRACTION_ONE);
   }
   state->frame = (DeceptusU8)(position / FRACTION_ONE);
   state->fraction = (DeceptusU8)(position % FRACTION_ONE);
}

static void jumpTo(DeceptusMechanismState *state, DeceptusU8 frame)
{
   state->frame = frame;
   state->fraction = 0;
}

/* updateInterval: extend, wait on_ticks, retract, wait off_ticks. */
static void intervalTick(DeceptusMechanismState *state, const DeceptusMechanismTiming *timing)
{
   ++state->timer;
   if ((state->frame == EXTRACT_END) && (state->phase == PHASE_EXTRACTING))
   {
      state->phase = PHASE_RETRACTING;
      state->timer = 0;
      state->pending = 1;
   }
   else if ((state->frame == RETRACT_END) && (state->phase == PHASE_RETRACTING))
   {
      state->phase = PHASE_EXTRACTING;
      state->timer = 0;
      state->pending = 1;
   }
   if (state->pending)
   {
      if (state->timer < ((state->phase == PHASE_RETRACTING) ? timing->on_ticks : timing->off_ticks))
      {
         return;
      }
      state->pending = 0;
      if (state->frame == EXTRACT_END)
      {
         jumpTo(state, RETRACT_START);
      }
      else if (state->frame == RETRACT_END)
      {
         jumpTo(state, EXTRACT_START);
      }
   }
   advance(state, (state->phase == PHASE_EXTRACTING) ? EXTRACT_END : RETRACT_END);
}

/* updateTrap: a body touching the retracted spikes makes them extend after the trap delay, stay up
 * for on_ticks and retract. triggered marks the running count. */
static void trapTick(DeceptusMechanismState *state, const DeceptusMechanismTiming *timing, DeceptusU8 touched)
{
   if (state->triggered)
   {
      ++state->timer;
   }
   else if (touched)
   {
      state->triggered = 1;
      state->timer = 0;
   }
   if (!state->triggered)
   {
      advance(state, RETRACT_END);
      return;
   }
   if ((state->timer > TRAP_DELAY_TICKS) && (state->phase != PHASE_EXTRACTING))
   {
      state->phase = PHASE_EXTRACTING;
      jumpTo(state, EXTRACT_START);
   }
   if (state->phase == PHASE_EXTRACTING)
   {
      advance(state, EXTRACT_END);
   }
   if ((state->timer > TRAP_DELAY_TICKS) && (state->timer - TRAP_DELAY_TICKS > timing->on_ticks))
   {
      state->triggered = 0;
      state->phase = PHASE_RETRACTING;
      jumpTo(state, RETRACT_START);
   }
}

static DeceptusU8 touches(const DeceptusRect *tile, const DeceptusRect *body)
{
   return (body->x < tile->x + TILE - TOLERANCE) && (body->x + body->width > tile->x + TOLERANCE) &&
          (body->y < tile->y + TILE - TOLERANCE) && (body->y + body->height > tile->y + TOLERANCE);
}

void floorSpikesInit(DeceptusMechanismSet *set)
{
   const DeceptusMechanismDef *definition;
   DeceptusMechanismState *state;
   DeceptusU16 index;
   for (index = 0; index < set->count; ++index)
   {
      definition = &set->definitions[index];
      if (definition->kind != MECHANISM_FLOOR_SPIKES)
      {
         continue;
      }
      state = &set->states[index];
      /* trap spikes wait fully retracted; the others start at the frame their tile shows. */
      jumpTo(state, (definition->flags & FLOOR_SPIKES_TRAP) ? (DeceptusU8)RETRACT_END
                                                            : (DeceptusU8)(definition->value % FLOOR_SPIKES_ATLAS_COLUMNS));
      state->phase = PHASE_RETRACTING;
      state->timer = (set->timings != 0) ? set->timings[index].phase_offset_ticks : 0;
      state->pending = 0;
      state->triggered = 0;
   }
}

DeceptusU16 floorSpikesTick(DeceptusMechanismSet *set, const DeceptusRect *bounds)
{
   const DeceptusMechanismDef *definition;
   DeceptusMechanismState *state;
   DeceptusU16 index, damage;
   DeceptusU8 touched;
   damage = 0;
   if (set->timings == 0)
   {
      return 0;
   }
   for (index = 0; index < set->count; ++index)
   {
      definition = &set->definitions[index];
      if (definition->kind != MECHANISM_FLOOR_SPIKES)
      {
         continue;
      }
      state = &set->states[index];
      touched = touches(&definition->bounds, bounds);
      if (definition->flags & FLOOR_SPIKES_INTERVAL)
      {
         intervalTick(state, &set->timings[index]);
      }
      else if (definition->flags & FLOOR_SPIKES_TRAP)
      {
         trapTick(state, &set->timings[index], touched);
      }
      else
      {
         advance(state, state->enabled ? (DeceptusU8)EXTRACT_END : (DeceptusU8)RETRACT_END);
      }
      if (touched && (state->frame < DEADLY_BELOW))
      {
         damage = SPIKE_DAMAGE;
      }
   }
   return damage;
}

DeceptusU8 floorSpikesImage(const DeceptusMechanismSet *set, DeceptusU16 index)
{
   DeceptusU8 row;
   row = (DeceptusU8)(set->definitions[index].value / FLOOR_SPIKES_ATLAS_COLUMNS);
   return (DeceptusU8)(row * FLOOR_SPIKES_FRAMES + set->states[index].frame);
}
