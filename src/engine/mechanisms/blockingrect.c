/* original counterpart: src/game/mechanisms/blockingrect.cpp. */
/* private mechanism implementation; compiled through ../mechanisms.c. */

static deceptus_u8 mechanism_solid_query(const deceptus_mechanism_set *set, const deceptus_rect *bounds,
                                         deceptus_u8 projectile)
{
    deceptus_u16 index;
    if (!set) {
        return 0;
    }
    for (index = 0; index < set->count; ++index) {
        if (set->definitions[index].kind == DECEPTUS_MECH_BLOCK) {
            if (on_off_block_solid(set, index, bounds)) {
                return 1;
            }
        } else if (set->definitions[index].kind == DECEPTUS_MECH_COLLAPSING) {
            if (collapsing_platform_solid(set, index, bounds)) {
                return 1;
            }
        } else if ((set->definitions[index].kind == DECEPTUS_MECH_DOOR && set->states[index].active) ||
                   (set->definitions[index].kind == DECEPTUS_MECH_DESTRUCTIBLE && set->states[index].enabled &&
                    !set->states[index].triggered) ||
                   (set->definitions[index].kind == DECEPTUS_MECH_CANNON_BODY && set->states[index].enabled &&
                    !projectile)) {
            if (overlaps(bounds, &set->definitions[index].bounds)) {
                return 1;
            }
        }
    }
    return 0;
}

deceptus_u8 deceptus_mechanism_solid(const deceptus_mechanism_set *set, const deceptus_rect *bounds)
{
    return mechanism_solid_query(set, bounds, 0);
}

deceptus_u8 deceptus_mechanism_projectile_solid(const deceptus_mechanism_set *set, const deceptus_rect *bounds)
{
    return mechanism_solid_query(set, bounds, 1);
}
