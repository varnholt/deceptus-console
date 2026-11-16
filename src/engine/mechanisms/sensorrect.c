/* original counterpart: src/game/mechanisms/sensorrect.cpp. */
/* private mechanism implementation; compiled through ../mechanisms.c. */

static void sensorRectContact(DeceptusMechanismSet *set, DeceptusU16 index, DeceptusU8 inside)
{
   if (inside != set->states[index].inside)
   {
      emit(set, set->definitions[index].source_id, inside ? DECEPTUS_MECH_ENTER : DECEPTUS_MECH_LEAVE, inside);
   }
}
