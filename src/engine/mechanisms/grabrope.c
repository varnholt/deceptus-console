/* original counterparts: mechanisms/rope.cpp and mechanisms/grabrope.cpp. */
#include "deceptus/physics.h"
#include <string.h>

deceptus_u16 deceptus_grabrope_distance(deceptus_i32 x, deceptus_i32 y)
{
    deceptus_u32 value, result, bit;
    value = (deceptus_u32)(x * x) + (deceptus_u32)(y * y);
    result = 0;
    bit = (deceptus_u32)1 << 30;
    while (bit > value) {
        bit >>= 2;
    }
    while (bit) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return (deceptus_u16)result;
}

static deceptus_u8 rope_point_clear(const deceptus_engine *engine, const deceptus_grabrope *rope,
                                    deceptus_fixed x, deceptus_fixed y)
{
    deceptus_rect point;
    deceptus_i16 column, row;
    point.x = deceptus_floor(x) - 1;
    point.y = deceptus_floor(y) - 1;
    point.width = 2;
    point.height = 2;
    for (row = deceptus_position_cell(y - DECEPTUS_ONE); row <= deceptus_position_cell(y); ++row) {
        for (column = deceptus_position_cell(x - DECEPTUS_ONE); column <= deceptus_position_cell(x); ++column) {
            if (deceptus_cell(engine->room, column, row) & DECEPTUS_SOLID) {
                return 0;
            }
        }
    }
    return !rope->dynamic_solids || !deceptus_mechanism_projectile_solid(&engine->mechanisms, &point);
}

static void rope_measure(deceptus_grabrope *rope)
{
    deceptus_u8 node;
    rope->cumulative_length[0] = 0;
    for (node = 1; node < rope->node_count; ++node) {
        rope->cumulative_length[node] = rope->cumulative_length[node - 1] +
                                        deceptus_grabrope_distance((deceptus_i32)rope->x[node] - rope->x[node - 1],
                                                                   (deceptus_i32)rope->y[node] - rope->y[node - 1]);
    }
    rope->total_length = rope->cumulative_length[rope->node_count - 1];
}

static void rope_move_node(const deceptus_engine *engine, deceptus_grabrope *rope, deceptus_u8 node, deceptus_fixed x,
                           deceptus_fixed y)
{
    if (x != rope->x[node] && rope_point_clear(engine, rope, x, rope->y[node])) {
        rope->x[node] = x;
    }
    if (y != rope->y[node] && rope_point_clear(engine, rope, rope->x[node], y)) {
        rope->y[node] = y;
    }
}

static void rope_solve(deceptus_engine *engine, deceptus_grabrope *rope, deceptus_u8 pinned, deceptus_fixed pin_x,
                       deceptus_fixed pin_y)
{
    const deceptus_mechanism_def *definition;
    deceptus_i32 dx, dy, correction_x, correction_y, correction_scale;
    deceptus_u16 distance, rest;
    deceptus_u8 iteration, node, previous;
    definition = &engine->mechanisms.definitions[rope->mechanism_index];
    rest = (deceptus_u16)(definition->bounds.height * 16 / rope->node_count);
    for (iteration = 0; iteration < 2; ++iteration) {
        rope->x[0] = DECEPTUS_FIXED(definition->bounds.x);
        rope->y[0] = DECEPTUS_FIXED(definition->bounds.y) + 8;
        if (pinned < rope->node_count && pinned) {
            rope->x[pinned] = pin_x;
            rope->y[pinned] = pin_y;
        }
        for (node = 1; node < rope->node_count; ++node) {
            previous = node - 1;
            dx = (deceptus_i32)rope->x[node] - rope->x[previous];
            dy = (deceptus_i32)rope->y[node] - rope->y[previous];
            distance = deceptus_grabrope_distance(dx, dy);
            if (!distance) {
                continue;
            }
            correction_scale = ((deceptus_i32)distance - rest) * 256 / distance;
            if (!correction_scale) {
                continue;
            }
            correction_x = dx * correction_scale / 256;
            correction_y = dy * correction_scale / 256;
            if (!correction_x && !correction_y) {
                continue;
            }
            if (!previous || previous == pinned) {
                if (node != pinned) {
                    rope_move_node(engine, rope, node, (deceptus_fixed)(rope->x[node] - correction_x),
                                   (deceptus_fixed)(rope->y[node] - correction_y));
                }
            } else if (node == pinned) {
                rope_move_node(engine, rope, previous, (deceptus_fixed)(rope->x[previous] + correction_x),
                               (deceptus_fixed)(rope->y[previous] + correction_y));
            } else {
                correction_x /= 2;
                correction_y /= 2;
                rope_move_node(engine, rope, previous, (deceptus_fixed)(rope->x[previous] + correction_x),
                               (deceptus_fixed)(rope->y[previous] + correction_y));
                rope_move_node(engine, rope, node, (deceptus_fixed)(rope->x[node] - correction_x),
                               (deceptus_fixed)(rope->y[node] - correction_y));
            }
        }
    }
    if (pinned && pinned < rope->node_count) {
        rope->x[pinned] = pin_x;
        rope->y[pinned] = pin_y;
    }
}

static deceptus_u8 rope_dynamic_solids(const deceptus_engine *engine, const deceptus_mechanism_def *rope)
{
    const deceptus_mechanism_def *definition;
    deceptus_i32 reach, left, right, top, bottom;
    deceptus_u8 index;
    /* cover the full chain, attachment link, anchor slack and player extent. */
    reach = (deceptus_i32)rope->bounds.height + rope->bounds.height / rope->value + 36;
    left = (deceptus_i32)rope->bounds.x - reach;
    right = (deceptus_i32)rope->bounds.x + reach;
    top = (deceptus_i32)rope->bounds.y - reach;
    bottom = (deceptus_i32)rope->bounds.y + reach;
    for (index = 0; index < engine->mechanisms.count; ++index) {
        definition = &engine->mechanisms.definitions[index];
        switch (definition->kind) {
        case DECEPTUS_MECH_BLOCK:
        case DECEPTUS_MECH_COLLAPSING:
        case DECEPTUS_MECH_DOOR:
        case DECEPTUS_MECH_DESTRUCTIBLE:
            /* these source families never move their immutable collision bounds. */
            if ((deceptus_i32)definition->bounds.x <= right &&
                (deceptus_i32)definition->bounds.x + definition->bounds.width >= left &&
                (deceptus_i32)definition->bounds.y <= bottom &&
                (deceptus_i32)definition->bounds.y + definition->bounds.height >= top) {
                return 1;
            }
            break;
        case DECEPTUS_MECH_CHECKPOINT:
        case DECEPTUS_MECH_SENSOR:
        case DECEPTUS_MECH_LEVER:
        case DECEPTUS_MECH_DAMAGE:
        case DECEPTUS_MECH_FLAG:
        case DECEPTUS_MECH_SPIKES:
        case DECEPTUS_MECH_WIND:
        case DECEPTUS_MECH_CANNON_BODY:
        case DECEPTUS_MECH_GRAB_ROPE:
            break;
        default:
            /* future moving or unknown solid families must retain the full query. */
            return 1;
        }
    }
    return 0;
}

deceptus_u8 deceptus_grabropes_init(deceptus_engine *engine)
{
    const deceptus_mechanism_def *definition;
    deceptus_grabrope *rope;
    deceptus_u8 index, node;
    memset(&engine->grab_ropes, 0, sizeof(engine->grab_ropes));
    for (index = 0; index < engine->mechanisms.count; ++index) {
        definition = &engine->mechanisms.definitions[index];
        if (definition->kind != DECEPTUS_MECH_GRAB_ROPE) {
            continue;
        }
        if (engine->grab_ropes.count >= DECEPTUS_GRAB_ROPE_CAPACITY || definition->value < 2 ||
            definition->value > DECEPTUS_GRAB_ROPE_NODES || definition->bounds.height <= 0 ||
            definition->bounds.height > 512) {
            return 0;
        }
        rope = &engine->grab_ropes.ropes[engine->grab_ropes.count++];
        rope->mechanism_index = index;
        rope->node_count = (deceptus_u8)definition->value;
        rope->dynamic_solids = rope_dynamic_solids(engine, definition);
        for (node = 0; node < rope->node_count; ++node) {
            rope->x[node] = rope->previous_x[node] = DECEPTUS_FIXED(definition->bounds.x);
            rope->y[node] = rope->previous_y[node] =
                DECEPTUS_FIXED(definition->bounds.y) + 8 +
                (deceptus_fixed)((deceptus_i32)definition->bounds.height * 16 * node / rope->node_count);
        }
        rope_measure(rope);
    }
    return 1;
}

void deceptus_grabropes_tick(deceptus_engine *engine)
{
    deceptus_grabrope *rope;
    deceptus_fixed x, y;
    deceptus_i16 vx, vy;
    deceptus_u8 index, node, damp;
    for (index = 0; index < engine->grab_ropes.count; ++index) {
        rope = &engine->grab_ropes.ropes[index];
        if (!engine->mechanisms.states[rope->mechanism_index].enabled) {
            continue;
        }
        damp = ++rope->damping_ticks >= 60;
        if (damp) {
            rope->damping_ticks = 0;
        }
        for (node = 1; node < rope->node_count; ++node) {
            x = rope->x[node];
            y = rope->y[node];
            vx = x - rope->previous_x[node];
            vy = y - rope->previous_y[node];
            if (damp) {
                vx -= vx / 10;
                vy -= vy / 10;
            }
            vy += engine->room->player_gravity ? engine->room->player_gravity : 4;
            if (vx > 127) {
                vx = 127;
            }
            if (vx < -127) {
                vx = -127;
            }
            if (vy > 127) {
                vy = 127;
            }
            if (vy < -127) {
                vy = -127;
            }
            rope->previous_x[node] = x;
            rope->previous_y[node] = y;
            rope_move_node(engine, rope, node, x + vx, y + vy);
        }
        /* attached chains receive their single final solve after player integration. */
        if (!engine->player_rope.attached || engine->player_rope.rope_index != index) {
            rope_solve(engine, rope, 255, 0, 0);
            rope_measure(rope);
        }
    }
}

void deceptus_grabrope_pin(deceptus_engine *engine, deceptus_u8 index, deceptus_u8 node, deceptus_fixed x,
                           deceptus_fixed y)
{
    deceptus_grabrope *rope;
    if (index >= engine->grab_ropes.count) {
        return;
    }
    rope = &engine->grab_ropes.ropes[index];
    if (!node || node >= rope->node_count) {
        return;
    }
    rope_solve(engine, rope, node, x, y);
    rope->previous_x[node] = x - engine->entities[0].velocity_x;
    rope->previous_y[node] = y - engine->entities[0].velocity_y;
    rope_measure(rope);
}

deceptus_u16 deceptus_grabrope_length(const deceptus_engine *engine, deceptus_u8 index)
{
    const deceptus_grabrope *rope;
    if (index >= engine->grab_ropes.count) {
        return 0;
    }
    rope = &engine->grab_ropes.ropes[index];
    if (!engine->mechanisms.states[rope->mechanism_index].enabled) {
        return 0;
    }
    return (rope->total_length + 15) / 16;
}

deceptus_u8 deceptus_grabrope_point_at(const deceptus_engine *engine, deceptus_u8 index, deceptus_u16 distance_px,
                                       deceptus_i16 *x, deceptus_i16 *y)
{
    const deceptus_grabrope *rope;
    deceptus_u16 remaining, length;
    deceptus_i32 dx, dy;
    deceptus_u8 node;
    if (index >= engine->grab_ropes.count || !x || !y) {
        return 0;
    }
    rope = &engine->grab_ropes.ropes[index];
    if (!engine->mechanisms.states[rope->mechanism_index].enabled) {
        return 0;
    }
    remaining = distance_px > 2047 ? 32752 : distance_px * 16;
    for (node = 1; node < rope->node_count; ++node) {
        dx = (deceptus_i32)rope->x[node] - rope->x[node - 1];
        dy = (deceptus_i32)rope->y[node] - rope->y[node - 1];
        length = rope->cumulative_length[node] - rope->cumulative_length[node - 1];
        if (remaining <= length && length) {
            *x = deceptus_floor((deceptus_fixed)(rope->x[node - 1] + dx * remaining / length));
            *y = deceptus_floor((deceptus_fixed)(rope->y[node - 1] + dy * remaining / length));
            return 1;
        }
        remaining -= length;
    }
    *x = deceptus_floor(rope->x[rope->node_count - 1]);
    *y = deceptus_floor(rope->y[rope->node_count - 1]);
    return 1;
}
