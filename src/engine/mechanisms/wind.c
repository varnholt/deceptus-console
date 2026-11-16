/* original counterpart: src/game/mechanisms/wind.cpp. */
/* private implementation, compiled through mechanisms.c. */
static void windContact(DeceptusMechanismSet *set, DeceptusU16 index)
{
   emit(set, set->definitions[index].source_id, DECEPTUS_MECH_FORCE, set->definitions[index].value);
}
