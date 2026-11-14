/* original counterpart: data/scripts/enemies/cannon_2.lua.
 * private implementation compiled through ../enemies.c. */

static void cannon_2_fire(deceptus_engine *engine, const deceptus_entity *cannon, const deceptus_enemy_def *definition)
{
    deceptus_u16 index;
    deceptus_projectile *ball;
    for (index = 0; index < DECEPTUS_PROJECTILE_CAPACITY; ++index) {
        ball = &engine->projectiles[index];
        if (ball->active) {
            continue;
        }
        ball->active = 1;
        ball->kind = DECEPTUS_CANNONBALL;
        ball->owner = 0;
        ball->phase = 0;
        ball->alignment = definition->alignment;
        ball->age = 0;
        ball->gravity = 0;
        ball->damage = definition->damage;
        ball->x = cannon->x + DECEPTUS_FIXED(definition->alignment ? 32 : -32);
        ball->y = cannon->y + DECEPTUS_FIXED(24);
        ball->velocity_x = definition->alignment ? definition->speed : -definition->speed;
        ball->velocity_y = 0;
        ++engine->projectiles_fired;
        return;
    }
    engine->content_error = 6;
}

static void cannon_2_tick(deceptus_engine *engine, deceptus_entity *cannon, const deceptus_enemy_def *definition)
{
    deceptus_u8 previous, row, column;
    previous = cannon->arrived;
    /* jump_buffer_ticks holds the source gun's 1000ms use interval. the
     * script requests fire on several updates while sprite index 17 is held. */
    if (cannon->jump_buffer_ticks) {
        --cannon->jump_buffer_ticks;
    }
    if (++cannon->timer >= definition->period) {
        cannon->timer = 0;
        cannon->state = 0;
        cannon->animation_ticks = 0;
        cannon->arrived = 0;
    }
    cannon->animation_ticks += 16;
    if (cannon->animation_ticks >= 60) {
        cannon->animation_ticks -= 60;
        ++cannon->arrived;
    }
    if (cannon->state) {
        column = cannon->arrived % 3;
        row = 0;
    } else {
        column = cannon->arrived % 12;
        row = 1 + (cannon->arrived / 12) % 4;
        if (column == 5 && row == 2 && !cannon->jump_buffer_ticks) {
            cannon_2_fire(engine, cannon, definition);
            cannon->jump_buffer_ticks = 61;
        }
        if (column == 3 && row == 4) {
            cannon->state = 1;
        }
    }
    /* the source only changes its sprite when the 16fps frame index changes. */
    if (previous != cannon->arrived) {
        cannon->frame = (deceptus_u8)(definition->alignment * 43 + (row ? 3 + (row - 1) * 12 + column : column));
    }
}
