/* original counterpart: src/game/mechanisms/spikeblock.cpp. */
/* private mechanism implementation; compiled through ../mechanisms.c. */

static void spikeBlockConfigure(DeceptusMechanismState* state)
{
   state->frame = state->enabled ? 32 : 0;
}

static void spikeBlockContact(DeceptusMechanismSet* set, DeceptusU16 index)
{
   if (!set->timings || (set->states[index].frame >= 29 && set->states[index].frame <= 35))
   {
      damageRectContact(set, index);
   }
}
