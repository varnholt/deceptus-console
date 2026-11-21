/* original counterpart: src/game/mechanisms/gamemechanism.cpp. */
/* private mechanism implementation; compiled through ../mechanisms.c. */
#include "mechanisminternal.h"

static DeceptusU8 overlaps(const DeceptusRect *first, const DeceptusRect *second)
{
   return first->x < second->x + second->width && first->x + first->width > second->x && first->y < second->y + second->height &&
          first->y + first->height > second->y;
}

static void emit(DeceptusMechanismSet *set, DeceptusU16 source_id, DeceptusU8 kind, DeceptusU16 value)
{
   DeceptusMechanismEvent *event;
   if (set->event_count == DECEPTUS_MECHANISM_EVENT_CAPACITY)
   {
      set->overflow = 1;
      return;
   }
   event = &set->events[set->event_count++];
   event->source_id = source_id;
   event->kind = kind;
   event->value = value;
}

#include "blockingrect.c"
#include "checkpoint.c"
#include "collapsingplatform.c"
#include "damagerect.c"
#include "destructibleblockingrect.c"
#include "door.c"
#include "lever.c"
#include "onoffblock.c"
#include "sensorrect.c"
#include "spikeblock.c"
#include "switchanimation.c"
#include "wind.c"

DeceptusI16 deceptusMechanismFind(const DeceptusMechanismSet *set, DeceptusU16 source_id)
{
   DeceptusU16 index;
   for (index = 0; index < set->count; ++index)
   {
      if (set->definitions[index].source_id == source_id)
      {
         return (DeceptusI16)index;
      }
   }
   return -1;
}

DeceptusU8 deceptusMechanismsInit(DeceptusMechanismSet *set, const DeceptusMechanismDef *definitions, DeceptusU16 count,
                                  const DeceptusU16 *targets, DeceptusU16 target_count)
{
   DeceptusU16 index, previous, target;
   memset(set, 0, sizeof(*set));
   if (count > DECEPTUS_MECHANISM_CAPACITY || (count && !definitions) || (target_count && !targets))
   {
      return 0;
   }
   for (index = 0; index < count; ++index)
   {
      if (definitions[index].target_first > target_count ||
          definitions[index].target_count > target_count - definitions[index].target_first)
      {
         return 0;
      }
      for (previous = 0; previous < index; ++previous)
      {
         if (definitions[index].source_id == definitions[previous].source_id)
         {
            return 0;
         }
      }
   }
   for (target = 0; target < target_count; ++target)
   {
      for (index = 0; index < count; ++index)
      {
         if (definitions[index].source_id == targets[target])
         {
            break;
         }
      }
      if (index == count)
      {
         return 0;
      }
   }
   set->definitions = definitions;
   set->targets = targets;
   set->target_count = target_count;
   set->count = (DeceptusU8)count;
   for (index = 0; index < count; ++index)
   {
      switch (definitions[index].kind)
      {
      case DECEPTUS_MECH_BLOCK:
      case DECEPTUS_MECH_COLLAPSING:
      case DECEPTUS_MECH_DOOR:
      case DECEPTUS_MECH_DESTRUCTIBLE:
      case DECEPTUS_MECH_CANNON_BODY:
         bindSolidMechanism(set, index);
         break;
      default:
         break;
      }
      set->states[index].enabled = definitions[index].enabled != 0;
      set->states[index].visible = 1;
      set->states[index].active = set->states[index].enabled;
      set->states[index].handle_available = !(definitions[index].flags & DECEPTUS_MECH_MISSING_HANDLE);
      if (definitions[index].kind == DECEPTUS_MECH_DOOR)
      {
         doorConfigure(&set->states[index]);
      }
      else if (definitions[index].kind == DECEPTUS_MECH_DESTRUCTIBLE)
      {
         destructibleBlockingRectConfigure(&definitions[index], &set->states[index]);
      }
      else if (definitions[index].kind == DECEPTUS_MECH_CANNON_BODY)
      {
         set->states[index].visible = 0;
      }
   }
   return 1;
}

void deceptusMechanismsConfigure(DeceptusMechanismSet *set, const DeceptusMechanismTiming *timings)
{
   DeceptusU16 index;
   DeceptusMechanismState *state;
   set->timings = timings;
   for (index = 0; index < set->count; ++index)
   {
      state = &set->states[index];
      if (set->definitions[index].kind == DECEPTUS_MECH_SPIKES)
      {
         spikeBlockConfigure(state);
      }
      else if (set->definitions[index].kind == DECEPTUS_MECH_BLOCK)
      {
         onOffBlockConfigure(&set->definitions[index], state);
      }
      else if (set->definitions[index].kind == DECEPTUS_MECH_LEVER)
      {
         leverConfigure(state);
      }
      state->target_frame = state->frame;
   }
}

DeceptusU8 deceptusMechanismEnable(DeceptusMechanismSet *set, DeceptusU16 source_id, DeceptusU8 enabled)
{
   DeceptusI16 index;
   index = deceptusMechanismFind(set, source_id);
   if (index < 0)
   {
      return 0;
   }
   enabled = enabled != 0;
   if (set->states[index].enabled != enabled)
   {
      if (set->definitions[index].kind == DECEPTUS_MECH_DOOR)
      {
         doorEnable(&set->states[index], enabled);
         emit(set, source_id, DECEPTUS_MECH_ENABLED, enabled);
         return 1;
      }
      if (set->timings && (set->timings[index].flags & DECEPTUS_TIMED_ANIMATION) &&
          (set->definitions[index].kind == DECEPTUS_MECH_BLOCK || set->definitions[index].kind == DECEPTUS_MECH_SPIKES))
      {
         if (set->states[index].pending_count >= 8)
         {
            set->overflow = 1;
            return 0;
         }
         if (enabled)
         {
            set->states[index].pending |= (DeceptusU8)(1U << set->states[index].pending_count);
         }
         ++set->states[index].pending_count;
      }
      set->states[index].enabled = enabled;
      emit(set, source_id, DECEPTUS_MECH_ENABLED, enabled);
   }
   return 1;
}

void deceptusMechanismsTick(DeceptusMechanismSet *set, const DeceptusRect *player, DeceptusU8 interact_pressed)
{
   DeceptusU16 index;
   DeceptusU8 inside, entering;
   DeceptusMechanismState *state;
   const DeceptusMechanismDef *definition;
   set->event_count = 0;
   set->overflow = 0;
   for (index = 0; index < set->count; ++index)
   {
      definition = &set->definitions[index];
      state = &set->states[index];
      inside = overlaps(player, &definition->bounds);
      entering = inside && !state->inside;
      if (definition->kind == DECEPTUS_MECH_DOOR)
      {
         doorTick(state);
      }
      else if (definition->kind == DECEPTUS_MECH_DESTRUCTIBLE)
      {
         destructibleBlockingRectTick(state);
      }
      if (set->timings)
      {
         if (set->timings[index].flags & DECEPTUS_TIMED_INTERVAL)
         {
            if (++state->timer >= (state->enabled ? set->timings[index].on_ticks : set->timings[index].off_ticks))
            {
               state->timer = 0;
               deceptusMechanismEnable(set, definition->source_id, !state->enabled);
            }
         }
         if (definition->kind == DECEPTUS_MECH_COLLAPSING && state->enabled)
         {
            collapseTick(set, index, player);
         }
         else if (set->timings[index].flags & DECEPTUS_TIMED_ANIMATION)
         {
            if (definition->kind == DECEPTUS_MECH_LEVER)
            {
               animateLever(state);
            }
            else
            {
               animateSwitch(set, index);
            }
         }
      }
      if (definition->kind == DECEPTUS_MECH_LEVER)
      {
         if (!leverInteract(set, index, inside, interact_pressed))
         {
            state->inside = inside;
            continue;
         }
      }
      else if (state->enabled || (definition->kind == DECEPTUS_MECH_SPIKES && set->timings))
      {
         if (definition->kind == DECEPTUS_MECH_CHECKPOINT)
         {
            checkpointContact(set, index, entering);
         }
         else if (definition->kind == DECEPTUS_MECH_SENSOR)
         {
            sensorRectContact(set, index, inside);
         }
         else if (inside)
         {
            if (definition->kind == DECEPTUS_MECH_WIND)
            {
               windContact(set, index);
            }
            else if (definition->kind == DECEPTUS_MECH_DAMAGE)
            {
               damageRectContact(set, index);
            }
            else if (definition->kind == DECEPTUS_MECH_SPIKES)
            {
               spikeBlockContact(set, index);
            }
         }
      }
      state->inside = inside;
   }
}
