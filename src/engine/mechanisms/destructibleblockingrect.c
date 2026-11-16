/* original counterpart: src/game/mechanisms/destructibleblockingrect.cpp. */
/* private mechanism implementation; compiled through ../mechanisms.c. */

static void destructibleBlockingRectConfigure(const DeceptusMechanismDef *definition, DeceptusMechanismState *state)
{
   state->cooldown = definition->value;
   state->active = state->enabled;
}

static void destructibleBlockingRectTick(DeceptusMechanismState *state)
{
   if (state->idle_ticks)
   {
      --state->idle_ticks;
   }
   if (!state->triggered || state->frame >= 49)
   {
      return;
   }
   /* the source advances destruction at 40fps and retains its final frame. */
   state->fraction += 2;
   if (state->fraction >= 3)
   {
      state->fraction -= 3;
      ++state->frame;
   }
}

DeceptusU8 deceptusMechanismHit(DeceptusMechanismSet *set, DeceptusU16 source_id, DeceptusU16 damage)
{
   DeceptusI16 index;
   DeceptusMechanismState *state;
   index = deceptusMechanismFind(set, source_id);
   if (index < 0 || !damage || set->definitions[index].kind != DECEPTUS_MECH_DESTRUCTIBLE)
   {
      return 0;
   }
   state = &set->states[index];
   if (!state->enabled || state->triggered)
   {
      return 0;
   }
   state->idle_ticks = 18;
   if (damage >= state->cooldown)
   {
      state->cooldown = 0;
      state->enabled = 0;
      state->active = 0;
      state->triggered = 1;
      state->frame = 0;
      state->fraction = 0;
   }
   else
   {
      state->cooldown -= damage;
   }
   return 1;
}
