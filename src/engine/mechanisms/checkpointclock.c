/* original counterpart: the clock of src/game/mechanisms/checkpoint.cpp (updateSpriteRect).
 *
 * the source shows the first activation frame until the checkpoint is reached, then plays the 20
 * activation frames at 20 per second and loops the 14 ticking frames at 10 per second. reaching the
 * checkpoint itself stays in checkpoint.c; this module only follows its triggered state. */
#include "deceptus/checkpointclock.h"

/* 60 ticks per second over the source's animation speeds of 20 and 10 frames per second. */
#define ACTIVATION_TICKS_PER_FRAME 3
#define ACTIVE_TICKS_PER_FRAME 6
#define ACTIVE_LOOP_TICKS (CHECKPOINT_CLOCK_ACTIVE_FRAMES * ACTIVE_TICKS_PER_FRAME)
/* the source switches to ticking once its activation index passes the start of the last activation frame. */
#define ACTIVATION_END_TICKS ((CHECKPOINT_CLOCK_ACTIVATION_FRAMES - 1) * ACTIVATION_TICKS_PER_FRAME)

#define PHASE_WAITING 0
#define PHASE_ACTIVATING 1
#define PHASE_TICKING 2

static void activationTick(DeceptusMechanismState* state)
{
   ++state->timer;
   state->frame = (DeceptusU8)(state->timer / ACTIVATION_TICKS_PER_FRAME);
   if (state->timer > ACTIVATION_END_TICKS)
   {
      state->phase = PHASE_TICKING;
      state->timer = 0;
   }
}

static void tickingTick(DeceptusMechanismState* state)
{
   ++state->timer;
   if (state->timer == ACTIVE_LOOP_TICKS)
   {
      state->timer = 0;
   }
   state->frame = (DeceptusU8)(CHECKPOINT_CLOCK_ACTIVATION_FRAMES + state->timer / ACTIVE_TICKS_PER_FRAME);
}

void checkpointClocksInit(DeceptusMechanismSet* set)
{
   DeceptusMechanismState* state;
   DeceptusU16 index;
   for (index = 0; index < set->count; ++index)
   {
      if (set->definitions[index].kind != DECEPTUS_MECH_CHECKPOINT)
      {
         continue;
      }
      state = &set->states[index];
      state->phase = PHASE_WAITING;
      state->frame = 0;
      state->timer = 0;
   }
}

void checkpointClocksTick(DeceptusMechanismSet* set)
{
   DeceptusMechanismState* state;
   DeceptusU16 index;
   for (index = 0; index < set->count; ++index)
   {
      if (set->definitions[index].kind != DECEPTUS_MECH_CHECKPOINT)
      {
         continue;
      }
      state = &set->states[index];
      if (state->phase == PHASE_WAITING)
      {
         if (!state->triggered)
         {
            continue;
         }
         state->phase = PHASE_ACTIVATING;
         state->timer = 0;
      }
      if (state->phase == PHASE_ACTIVATING)
      {
         activationTick(state);
      }
      else
      {
         tickingTick(state);
      }
   }
}

DeceptusU8 checkpointClockVisible(const DeceptusMechanismSet* set, DeceptusU16 index)
{
   return (set->definitions[index].flags & CHECKPOINT_HIDDEN) == 0;
}

void checkpointClockPart(const DeceptusMechanismSet* set, DeceptusU16 index, DeceptusU8* image, DeceptusI16* x, DeceptusI16* y)
{
   const DeceptusMechanismDef* definition;
   definition = &set->definitions[index];
   *image = set->states[index].frame;
   *x = definition->bounds.x;
   *y = definition->bounds.y;
   if (definition->flags & CHECKPOINT_RAISED)
   {
      *y = (DeceptusI16)(*y - CHECKPOINT_RAISE);
   }
}
