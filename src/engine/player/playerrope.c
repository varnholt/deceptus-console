/* original counterparts: player/playerrope.cpp and player/playerropehold.cpp. */
#include "deceptus/physics.h"
#include <string.h>

void deceptus_playerrope_reset(deceptus_engine *engine)
{
    memset(&engine->player_rope, 0, sizeof(engine->player_rope));
}

static deceptus_fixed playerrope_limit(deceptus_i32 velocity)
{
    if (velocity > 127) {
        return 127;
    }
    if (velocity < -127) {
        return -127;
    }
    return (deceptus_fixed)velocity;
}

static void playerrope_move(deceptus_engine *engine)
{
    deceptus_entity *player;
    deceptus_u8 grounded;
    player = &engine->entities[0];
    grounded = player->grounded;
    player->velocity_x = playerrope_limit(player->velocity_x);
    player->velocity_y = playerrope_limit(player->velocity_y);
    deceptus_move_horizontal_dynamic(engine->room, player, 8, 30, &engine->mechanisms);
    deceptus_move_vertical_dynamic(engine->room, player, 8, 30, &engine->mechanisms);
    if (player->grounded && !grounded) {
        deceptus_event_push(engine, DECEPTUS_EVENT_LAND);
    }
    player->frame = player->velocity_y < 0 ? 16 : 17;
}

static deceptus_u8 playerrope_near(const deceptus_grabrope *rope, const deceptus_entity *player)
{
    deceptus_u8 node;
    deceptus_i16 x, y, left, right, top, bottom, first_x, first_y, second_x, second_y;
    x = deceptus_floor(player->x);
    y = deceptus_floor(player->y);
    for (node = 1; node < rope->node_count; ++node) {
        first_x = deceptus_floor(rope->x[node - 1]);
        first_y = deceptus_floor(rope->y[node - 1]);
        second_x = deceptus_floor(rope->x[node]);
        second_y = deceptus_floor(rope->y[node]);
        left = (first_x < second_x ? first_x : second_x) - 8;
        right = (first_x > second_x ? first_x : second_x) + 8;
        top = (first_y < second_y ? first_y : second_y) - 8;
        bottom = (first_y > second_y ? first_y : second_y) + 8;
        if (x + 8 > left && x - 8 < right && y > top && y - 30 < bottom) {
            return 1;
        }
    }
    return 0;
}

static deceptus_u8 playerrope_grab(deceptus_engine *engine)
{
    deceptus_entity *player;
    deceptus_playerrope *hold;
    const deceptus_grabrope *rope;
    const deceptus_mechanism_def *definition;
    deceptus_u16 best, distance, segment_length;
    deceptus_u8 index, node, closest;
    player = &engine->entities[0];
    hold = &engine->player_rope;
    if (player->grounded || player->in_water || hold->regrab_ticks) {
        return 0;
    }
    for (index = 0; index < engine->grab_ropes.count; ++index) {
        rope = &engine->grab_ropes.ropes[index];
        if (!engine->mechanisms.states[rope->mechanism_index].enabled || !playerrope_near(rope, player)) {
            continue;
        }
        definition = &engine->mechanisms.definitions[rope->mechanism_index];
        closest = 0;
        best = 65535;
        for (node = 0; node < rope->node_count; ++node) {
            distance = deceptus_grabrope_distance((deceptus_i32)player->x - rope->x[node],
                                                  (deceptus_i32)player->y -
                                                      DECEPTUS_FIXED(DECEPTUS_PLAYER_ROPE_HAND_OFFSET) - rope->y[node]);
            if (distance < best) {
                best = distance;
                closest = node;
            }
        }
        segment_length = (deceptus_u16)(definition->bounds.height * 16 / rope->node_count);
        hold->attached = 1;
        hold->rope_index = index;
        hold->node_index = closest;
        hold->link_length = best < segment_length ? best : segment_length;
        hold->radius = (deceptus_u16)((deceptus_i32)definition->bounds.height * 16 * closest / rope->node_count) +
                       hold->link_length + 115;
        player->coyote_ticks = 0;
        player->jump_buffer_ticks = 0;
        player->crouched = 0;
        player->bend_ticks = 0;
        player->animation_ticks = 0;
        return 1;
    }
    return 0;
}

deceptus_u8 deceptus_playerrope_tick(deceptus_engine *engine, const deceptus_input *input)
{
    deceptus_playerrope *hold;
    deceptus_entity *player;
    deceptus_grabrope *rope;
    const deceptus_mechanism_def *definition;
    deceptus_i32 dx, dy, square, acceleration_x, acceleration_y;
    deceptus_fixed previous_x, previous_y, next_x, next_y;
    deceptus_i16 direction, gravity;
    deceptus_u16 distance, pull;
    deceptus_u8 grabbed;
    hold = &engine->player_rope;
    player = &engine->entities[0];
    if (hold->regrab_ticks) {
        --hold->regrab_ticks;
    }
    if (hold->grace_ticks) {
        --hold->grace_ticks;
    }
    if (player->in_water) {
        deceptus_playerrope_reset(engine);
        return 0;
    }
    grabbed = 0;
    if (!hold->attached) {
        grabbed = playerrope_grab(engine);
    }
    gravity = engine->room->player_gravity ? engine->room->player_gravity : 4;
    if (!hold->attached) {
        if (hold->grace_ticks && !player->grounded) {
            player->velocity_y = playerrope_limit((deceptus_i32)player->velocity_y + gravity);
            playerrope_move(engine);
            return 1;
        }
        return 0;
    }
    if (hold->rope_index >= engine->grab_ropes.count) {
        deceptus_playerrope_reset(engine);
        return 0;
    }
    rope = &engine->grab_ropes.ropes[hold->rope_index];
    definition = &engine->mechanisms.definitions[rope->mechanism_index];
    if (!engine->mechanisms.states[rope->mechanism_index].enabled) {
        deceptus_playerrope_reset(engine);
        return 0;
    }
    if (!grabbed && (input->pressed & DECEPTUS_JUMP)) {
        hold->attached = 0;
        hold->regrab_ticks = 60;
        hold->grace_ticks = 36;
        /* source jumpOff adds half the ordinary jump lift without horizontal push. */
        player->velocity_y = playerrope_limit((deceptus_i32)player->velocity_y - 44 + gravity);
        deceptus_event_push(engine, DECEPTUS_EVENT_JUMP);
        playerrope_move(engine);
        return 1;
    }
    direction = ((input->held & DECEPTUS_RIGHT) != 0) - ((input->held & DECEPTUS_LEFT) != 0);
    if (direction) {
        player->facing_left = direction < 0;
    }
    previous_x = player->x;
    previous_y = player->y;
    dx = (deceptus_i32)player->x - DECEPTUS_FIXED(definition->bounds.x);
    dy = (deceptus_i32)player->y - DECEPTUS_FIXED(8) - DECEPTUS_FIXED(definition->bounds.y);
    square = dx * dx + dy * dy;
    acceleration_x = acceleration_y = 0;
    if (square) {
        /* source 14m/s^2 tangential control becomes 3 units of 12.4 per tick. */
        acceleration_x = (deceptus_i32)direction * 3 * dy * dy / square;
        acceleration_y = -(deceptus_i32)direction * 3 * dx * dy / square;
    }
    player->velocity_x = playerrope_limit((deceptus_i32)player->velocity_x + acceleration_x);
    player->velocity_y = playerrope_limit((deceptus_i32)player->velocity_y + gravity + acceleration_y);
    playerrope_move(engine);
    if (hold->link_length) {
        pull = hold->link_length < 19 ? hold->link_length : 19;
        hold->link_length -= pull;
        hold->radius -= pull;
    }
    dx = (deceptus_i32)player->x - DECEPTUS_FIXED(definition->bounds.x);
    dy = (deceptus_i32)player->y - DECEPTUS_FIXED(DECEPTUS_PLAYER_ROPE_HAND_OFFSET) -
         DECEPTUS_FIXED(definition->bounds.y);
    distance = deceptus_grabrope_distance(dx, dy);
    if (distance > hold->radius && distance) {
        next_x = (deceptus_fixed)(DECEPTUS_FIXED(definition->bounds.x) + dx * hold->radius / distance);
        next_y = (deceptus_fixed)(DECEPTUS_FIXED(definition->bounds.y + DECEPTUS_PLAYER_ROPE_HAND_OFFSET) +
                                  dy * hold->radius / distance);
        if (deceptus_body_clear(engine->room, &engine->mechanisms, next_x, next_y, 8, 30)) {
            player->x = next_x;
            player->y = next_y;
        }
    }
    player->velocity_x = playerrope_limit((deceptus_i32)player->x - previous_x);
    player->velocity_y = playerrope_limit((deceptus_i32)player->y - previous_y);
    deceptus_grabrope_pin(engine, hold->rope_index, hold->node_index, player->x,
                          player->y - DECEPTUS_FIXED(DECEPTUS_PLAYER_ROPE_HAND_OFFSET));
    ++player->animation_ticks;
    if (player->animation_ticks >= 11) {
        player->animation_ticks = 0;
    }
    player->frame = player->animation_ticks < 4 ? 0 : player->animation_ticks < 8 ? 1 : 2;
    return 1;
}
