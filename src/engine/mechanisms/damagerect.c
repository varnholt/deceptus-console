/* original counterpart: src/game/mechanisms/damagerect.cpp. */
/* private mechanism implementation; compiled through ../mechanisms.c. */

static void damageRectContact(DeceptusMechanismSet *set, DeceptusU16 index)
{
   emit(set, set->definitions[index].source_id, DECEPTUS_MECH_HIT, set->definitions[index].value);
}
