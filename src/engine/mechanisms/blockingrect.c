/* original counterpart: src/game/mechanisms/blockingrect.cpp. */
/* private mechanism implementation; compiled through ../mechanisms.c. */

#if DECEPTUS_MECHANISM_CAPACITY > 32
#error solid mechanism masks require at most 32 mechanisms
#endif

/* bin coordinates are only taken of non-negative bounds, so shifts equal the divisions; 16-bit cpus
   would otherwise call a 32-bit divide for every collision probe. */
#define DECEPTUS_SOLID_BIN_SHIFT 6
#if (1 << DECEPTUS_SOLID_BIN_SHIFT) != DECEPTUS_SOLID_BIN_SIZE
#error DECEPTUS_SOLID_BIN_SHIFT must match DECEPTUS_SOLID_BIN_SIZE
#endif

#define DECEPTUS_SOLID_QUERY_BODY 0
#define DECEPTUS_SOLID_QUERY_PROJECTILE 1
#define DECEPTUS_SOLID_QUERY_SIDEWAYS 2
#define DECEPTUS_SOLID_QUERY_BOX 3

/* a moving solid stays a candidate for every query: its bins would change as it moves. */
static void bindSolidMechanism(DeceptusMechanismSet* set, DeceptusU16 index, DeceptusU8 moving)
{
   const DeceptusRect* bounds;
   DeceptusI32 right, bottom;
   DeceptusU16 bin, last;
   DeceptusU32 mask;
   bounds = &set->definitions[index].bounds;
   mask = set->solid_mask + 1;
   set->solid_indices[set->solid_count++] = (DeceptusU8)index;
   set->solid_mask |= mask;
   right = (DeceptusI32)bounds->x + bounds->width;
   bottom = (DeceptusI32)bounds->y + bounds->height;
   if (moving || bounds->x < 0 || bounds->y < 0 || bounds->width <= 0 || bounds->height <= 0 ||
       right > DECEPTUS_SOLID_BIN_COUNT * DECEPTUS_SOLID_BIN_SIZE || bottom > DECEPTUS_SOLID_BIN_COUNT * DECEPTUS_SOLID_BIN_SIZE)
   {
      /* unusual coordinates remain candidates for every query. */
      set->solid_outside |= mask;
      return;
   }
   last = (DeceptusU16)((DeceptusU32)(right - 1) >> DECEPTUS_SOLID_BIN_SHIFT);
   for (bin = (DeceptusU16)((DeceptusU16)bounds->x >> DECEPTUS_SOLID_BIN_SHIFT); bin <= last; ++bin)
   {
      set->solid_columns[bin] |= mask;
   }
   last = (DeceptusU16)((DeceptusU32)(bottom - 1) >> DECEPTUS_SOLID_BIN_SHIFT);
   for (bin = (DeceptusU16)((DeceptusU16)bounds->y >> DECEPTUS_SOLID_BIN_SHIFT); bin <= last; ++bin)
   {
      set->solid_rows[bin] |= mask;
   }
}

static DeceptusU32 solidCandidates(const DeceptusMechanismSet* set, const DeceptusRect* bounds)
{
   DeceptusI32 right, bottom;
   DeceptusU16 bin, last;
   DeceptusU32 mask, rows;
   right = (DeceptusI32)bounds->x + bounds->width;
   bottom = (DeceptusI32)bounds->y + bounds->height;
   if (bounds->x < 0 || bounds->y < 0 || bounds->width <= 0 || bounds->height <= 0 ||
       right > DECEPTUS_SOLID_BIN_COUNT * DECEPTUS_SOLID_BIN_SIZE || bottom > DECEPTUS_SOLID_BIN_COUNT * DECEPTUS_SOLID_BIN_SIZE)
   {
      return set->solid_mask;
   }
   mask = 0;
   last = (DeceptusU16)((DeceptusU32)(right - 1) >> DECEPTUS_SOLID_BIN_SHIFT);
   for (bin = (DeceptusU16)((DeceptusU16)bounds->x >> DECEPTUS_SOLID_BIN_SHIFT); bin <= last; ++bin)
   {
      mask |= set->solid_columns[bin];
   }
   rows = 0;
   last = (DeceptusU16)((DeceptusU32)(bottom - 1) >> DECEPTUS_SOLID_BIN_SHIFT);
   for (bin = (DeceptusU16)((DeceptusU16)bounds->y >> DECEPTUS_SOLID_BIN_SHIFT); bin <= last; ++bin)
   {
      rows |= set->solid_rows[bin];
   }
   return (mask & rows) | set->solid_outside;
}

const DeceptusRect* deceptusMechanismBounds(const DeceptusMechanismSet* set, DeceptusU16 index)
{
   DeceptusU8 box;
   if (set->definitions[index].kind == DECEPTUS_MECH_MOVEABLE_BOX)
   {
      for (box = 0; box < set->box_count; ++box)
      {
         if (set->boxes[box].mechanism_index == index)
         {
            return &set->boxes[box].bounds;
         }
      }
   }
   return &set->definitions[index].bounds;
}

/* ignore names the box a box query moves, which does not stop itself. */
static DeceptusU8 mechanismSolidQuery(const DeceptusMechanismSet* set, const DeceptusRect* bounds, DeceptusU8 query, DeceptusU16 ignore)
{
   DeceptusU16 candidate, index;
   DeceptusU32 mask;
   DeceptusU8 kind, projectile;
   projectile = query == DECEPTUS_SOLID_QUERY_PROJECTILE;
   if ((set == 0) || (set->solid_count == 0))
   {
      return 0;
   }
   mask = solidCandidates(set, bounds);
   for (candidate = 0; mask; ++candidate, mask >>= 1)
   {
      /* collision probes mostly see sparse masks: step over empty bytes at once. */
      while (!(mask & 255))
      {
         mask >>= 8;
         candidate += 8;
      }
      if (!(mask & 1))
      {
         continue;
      }
      index = set->solid_indices[candidate];
      kind = set->definitions[index].kind;
      /* every solid kind below requires this overlap and has no side effects, so testing it first
         only skips work; most candidates share a 64-pixel bin without touching the probe. */
      if ((index == ignore) || (overlaps(bounds, deceptusMechanismBounds(set, index)) == 0))
      {
         continue;
      }
      if (kind == DECEPTUS_MECH_MOVEABLE_BOX)
      {
         if (query != DECEPTUS_SOLID_QUERY_SIDEWAYS)
         {
            return 1;
         }
      }
      else if (kind == DECEPTUS_MECH_BOX_COLLIDER)
      {
         if (query == DECEPTUS_SOLID_QUERY_BOX)
         {
            return 1;
         }
      }
      else if (kind == DECEPTUS_MECH_BLOCK)
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
               (set->definitions[index].kind == DECEPTUS_MECH_DESTRUCTIBLE && set->states[index].enabled && !set->states[index].triggered
               ) ||
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

/* no mechanism index reaches this, so it ignores nothing. */
#define DECEPTUS_SOLID_IGNORE_NONE 0xffffU

DeceptusU8 deceptusMechanismSolid(const DeceptusMechanismSet* set, const DeceptusRect* bounds)
{
   return mechanismSolidQuery(set, bounds, DECEPTUS_SOLID_QUERY_BODY, DECEPTUS_SOLID_IGNORE_NONE);
}

DeceptusU8 deceptusMechanismProjectileSolid(const DeceptusMechanismSet* set, const DeceptusRect* bounds)
{
   return mechanismSolidQuery(set, bounds, DECEPTUS_SOLID_QUERY_PROJECTILE, DECEPTUS_SOLID_IGNORE_NONE);
}

DeceptusU8 deceptusMechanismSidewaysSolid(const DeceptusMechanismSet* set, const DeceptusRect* bounds)
{
   return mechanismSolidQuery(set, bounds, DECEPTUS_SOLID_QUERY_SIDEWAYS, DECEPTUS_SOLID_IGNORE_NONE);
}

DeceptusU8 deceptusMechanismBoxSolid(const DeceptusMechanismSet* set, const DeceptusRect* bounds, DeceptusU8 box)
{
   return mechanismSolidQuery(set, bounds, DECEPTUS_SOLID_QUERY_BOX, box);
}
