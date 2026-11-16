/* original counterpart: src/game/mechanisms/blockingrect.cpp. */
/* private mechanism implementation; compiled through ../mechanisms.c. */

static DeceptusU8 mechanismSolidQuery(const DeceptusMechanismSet *set, const DeceptusRect *bounds, DeceptusU8 projectile)
{
   DeceptusU16 index;
   if (!set)
   {
      return 0;
   }
   for (index = 0; index < set->count; ++index)
   {
      if (set->definitions[index].kind == DECEPTUS_MECH_BLOCK)
      {
         if (onOffBlockSolid(set, index, bounds))
         {
            return 1;
         }
      }
      else if (set->definitions[index].kind == DECEPTUS_MECH_COLLAPSING)
      {
         if (collapsingPlatformSolid(set, index, bounds))
         {
            return 1;
         }
      }
      else if ((set->definitions[index].kind == DECEPTUS_MECH_DOOR && set->states[index].active) ||
               (set->definitions[index].kind == DECEPTUS_MECH_DESTRUCTIBLE && set->states[index].enabled &&
                !set->states[index].triggered) ||
               (set->definitions[index].kind == DECEPTUS_MECH_CANNON_BODY && set->states[index].enabled && !projectile))
      {
         if (overlaps(bounds, &set->definitions[index].bounds))
         {
            return 1;
         }
      }
   }
   return 0;
}

DeceptusU8 deceptusMechanismSolid(const DeceptusMechanismSet *set, const DeceptusRect *bounds)
{
   return mechanismSolidQuery(set, bounds, 0);
}

DeceptusU8 deceptusMechanismProjectileSolid(const DeceptusMechanismSet *set, const DeceptusRect *bounds)
{
   return mechanismSolidQuery(set, bounds, 1);
}
