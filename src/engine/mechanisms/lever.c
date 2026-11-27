/* original counterpart: src/game/mechanisms/lever.cpp. */
/* private mechanism implementation; compiled through ../mechanisms.c. */

static void animateLever(DeceptusMechanismState* state)
{
   if (state->cooldown)
   {
      --state->cooldown;
   }
   if (state->enabled && state->progress < 10)
   {
      ++state->progress;
      state->frame = state->progress;
      state->idle_ticks = 0;
   }
   else if (!state->enabled && state->progress)
   {
      --state->progress;
      state->frame = 11 + 10 - state->progress;
      state->idle_ticks = 0;
   }
   else if (state->enabled)
   {
      state->frame = (DeceptusU8)(22 + state->idle_ticks / 6);
      if (++state->idle_ticks >= 36)
      {
         state->idle_ticks = 0;
      }
   }
}

static void leverConfigure(DeceptusMechanismState* state)
{
   state->progress = state->enabled ? 10 : 0;
   state->frame = state->enabled ? 22 : 0;
}

static DeceptusU8 leverInteract(DeceptusMechanismSet* set, DeceptusU16 index, DeceptusU8 inside, DeceptusU8 interact_pressed)
{
   DeceptusU16 target;
   DeceptusI16 slot;
   DeceptusU8 target_enabled;
   DeceptusMechanismState* state;
   const DeceptusMechanismDef* definition;
   state = &set->states[index];
   definition = &set->definitions[index];
   if (inside && interact_pressed && !state->cooldown && state->handle_available)
   {
      target_enabled = !state->enabled;
      if (definition->flags & DECEPTUS_MECH_INVERT_TARGETS)
      {
         target_enabled = !target_enabled;
      }
      for (target = 0; target < definition->target_count; ++target)
      {
         slot = deceptusMechanismFind(set, set->targets[definition->target_first + target]);
         if (set->timings && (set->timings[slot].flags & DECEPTUS_TIMED_ANIMATION) &&
             (set->definitions[slot].kind == DECEPTUS_MECH_BLOCK || set->definitions[slot].kind == DECEPTUS_MECH_SPIKES) &&
             set->states[slot].enabled != target_enabled && set->states[slot].pending_count >= 8)
         {
            break;
         }
      }
      if (target < definition->target_count)
      {
         set->overflow = 1;
         return 0;
      }
      deceptusMechanismEnable(set, definition->source_id, !state->enabled);
      if (set->timings && (set->timings[index].flags & DECEPTUS_TIMED_ANIMATION))
      {
         state->cooldown = 60;
      }
      for (target = 0; target < definition->target_count; ++target)
      {
         deceptusMechanismEnable(set, set->targets[definition->target_first + target], target_enabled);
      }
   }
   return 1;
}
