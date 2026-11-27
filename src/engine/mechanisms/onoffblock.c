/* original counterpart: src/game/mechanisms/onoffblock.cpp. */
/* private mechanism implementation; compiled through ../mechanisms.c. */

static void onOffBlockConfigure(const DeceptusMechanismDef* definition, DeceptusMechanismState* state)
{
   state->active = state->enabled ^ ((definition->flags & DECEPTUS_MECH_INVERTED) != 0);
   state->frame = state->active ? 14 : 0;
}

static DeceptusU8 onOffBlockSolid(const DeceptusMechanismSet* set, DeceptusU16 index, const DeceptusRect* bounds)
{
   DeceptusU8 enabled;
   enabled = set->states[index].enabled;
   if (set->definitions[index].flags & DECEPTUS_MECH_INVERTED)
   {
      enabled = !enabled;
   }
   if (set->timings && (set->timings[index].flags & DECEPTUS_TIMED_ANIMATION))
   {
      enabled = set->states[index].active;
   }
   return enabled && overlaps(bounds, &set->definitions[index].bounds);
}
