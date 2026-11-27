/* original counterpart: src/game/mechanisms/checkpoint.cpp. */
/* private mechanism implementation; compiled through ../mechanisms.c. */

static void checkpointContact(DeceptusMechanismSet* set, DeceptusU16 index, DeceptusU8 entering)
{
   if (entering && !set->states[index].triggered)
   {
      set->states[index].triggered = 1;
      emit(set, set->definitions[index].source_id, DECEPTUS_MECH_REACHED, 1);
   }
}
