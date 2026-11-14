/* original counterpart: data/scripts/enemies/orb.lua. source contours and
 * nearest-key alignment are imported by tools/enemy_paths.py. */

static void orb_tick(deceptus_engine *engine, deceptus_entity *orb, const deceptus_enemy_def *definition)
{
    const deceptus_path_point *point;
    deceptus_fixed distance, remaining, amount;
    deceptus_u16 fraction;
    distance = definition->speed;
    fraction = orb->gravity_fraction + definition->acceleration;
    if (fraction >= 256) {
        fraction -= 256;
        ++distance;
    }
    orb->gravity_fraction = (deceptus_u8)fraction;
    /* axis-aligned source contours can cross at most one corner per tick;
     * every imported edge is at least one collision cell long. */
    while (distance > 0) {
        point = &engine->room->enemy_paths[definition->path_first + orb->target];
        remaining = DECEPTUS_FIXED(point->x) - orb->x;
        if (!remaining) {
            remaining = DECEPTUS_FIXED(point->y) - orb->y;
        }
        if (remaining < 0) {
            remaining = -remaining;
        }
        amount = distance < remaining ? distance : remaining;
        if (orb->x != DECEPTUS_FIXED(point->x)) {
            orb->x += orb->x < DECEPTUS_FIXED(point->x) ? amount : -amount;
        } else {
            orb->y += orb->y < DECEPTUS_FIXED(point->y) ? amount : -amount;
        }
        distance -= amount;
        if (amount == remaining) {
            if (++orb->target >= definition->path_count) {
                orb->target = 0;
            }
        }
    }
    orb->animation_ticks += definition->animation_fps;
    if (orb->animation_ticks >= 60) {
        orb->animation_ticks -= 60;
        orb->frame = (orb->frame + 1) & 7;
    }
}

static deceptus_u16 orb_contact(const deceptus_engine *engine, const deceptus_entity *orb,
                                const deceptus_enemy_def *definition)
{
    const deceptus_entity *player;
    deceptus_i16 dx, dy, x, y, feet;
    player = &engine->entities[0];
    x = deceptus_floor(orb->x);
    y = deceptus_floor(orb->y);
    feet = deceptus_floor(player->y);
    dx = x - deceptus_floor(player->x);
    if (dx < 0) {
        dx = -dx;
    }
    dx = dx > 9 ? dx - 9 : 0;
    dy = y < feet - DECEPTUS_PLAYER_HEIGHT(player) ? feet - DECEPTUS_PLAYER_HEIGHT(player) - y
                                                   : (y > feet ? y - feet : 0);
    return dx <= 11 && dy <= 11 && dx * dx + dy * dy <= 121 ? definition->damage : 0;
}
