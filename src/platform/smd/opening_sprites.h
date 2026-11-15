/* logical source frames mapped to bounded Mega Drive sprites and palette banks. */
#include "deceptus/enemies.h"
#include "opening_video.h"
#include "smd_headtorch_resources.h"
#include "smd_sword_resources.h"
#include <genesis.h>

#define SMD_OBJ_REQUESTS 96
#define SMD_OBJ_PIECES 64

typedef struct smd_obj_request {
    deceptus_u16 resource;
    deceptus_i16 x, y;
    deceptus_u8 flip, priority;
} smd_obj_request;

typedef struct smd_obj_slot {
    deceptus_u16 resource;
    deceptus_u8 piece, valid, used, held, pending, next;
} smd_obj_slot;

static smd_obj_request smd_obj_requests[SMD_OBJ_REQUESTS];
static deceptus_u16 smd_obj_active_resources[SMD_OBJ_REQUESTS];
static smd_obj_slot smd_obj_slots[SMD_OBJ_PIECES];
static deceptus_u8 smd_obj_heads[SMD_SPRITE_RESOURCE_COUNT], smd_obj_epochs[SMD_SPRITE_RESOURCE_COUNT];
static deceptus_u8 smd_obj_epoch;
static deceptus_u16 smd_obj_unused, smd_obj_free_cursor;
static union {
    struct {
        deceptus_i8 objects[225];
        deceptus_i16 slivers[225];
    } ranges;
    struct {
        deceptus_u8 lines[224], slivers[224];
    } reference;
} smd_obj_budget;
#define smd_obj_lines smd_obj_budget.reference.lines
#define smd_obj_slivers smd_obj_budget.reference.slivers
static deceptus_u8 smd_obj_fast_budget;
static deceptus_u16 smd_obj_request_count, smd_obj_count;
static deceptus_u8 smd_sprite_overflow, smd_sprite_room_load, smd_sprite_blank;
static deceptus_u8 smd_sprite_initialized;
static deceptus_u16 smd_sprite_upload_bytes;
static deceptus_u32 smd_sprite_buffer[32];

static void smd_obj_add(deceptus_u16 resource, deceptus_i16 x, deceptus_i16 y, deceptus_u8 flip, deceptus_u8 priority)
{
    const smd_sprite_resource *entry;
    smd_obj_request *request;
    if (resource >= SMD_SPRITE_RESOURCE_COUNT) {
        smd_sprite_overflow = 1;
        return;
    }
    entry = &smd_sprite_resources[resource];
    x += entry->trim_x;
    y += entry->trim_y;
    if (x >= 320 || y >= 224 || x + entry->columns * 16 <= 0 || y + entry->rows * 16 <= 0) {
        return;
    }
    if (smd_obj_request_count >= SMD_OBJ_REQUESTS || entry->columns * entry->rows > 64) {
        smd_sprite_overflow = 1;
        return;
    }
    request = &smd_obj_requests[smd_obj_request_count++];
    request->resource = resource;
    request->x = x;
    request->y = y;
    request->flip = flip;
    request->priority = priority;
}

static deceptus_u16 smd_obj_tile(deceptus_u16 slot) { return SMD_OBJECT_FIRST + slot * 4; }

static deceptus_u16 smd_obj_slot_for(deceptus_u16 resource, deceptus_u8 piece)
{
    deceptus_u16 index;
    deceptus_u8 link, previous;
    smd_obj_slot *slot;
    for (link = smd_obj_heads[resource]; link; link = slot->next) {
        slot = &smd_obj_slots[link - 1];
        if (slot->piece == piece) {
            slot->used = 1;
            return link - 1;
        }
    }
    if (smd_obj_unused < SMD_OBJ_PIECES) {
        index = smd_obj_unused++;
    } else {
        for (index = smd_obj_free_cursor; index < SMD_OBJ_PIECES; ++index) {
            slot = &smd_obj_slots[index];
            if (!slot->used && !slot->held) {
                break;
            }
        }
        smd_obj_free_cursor = index < SMD_OBJ_PIECES ? index + 1 : SMD_OBJ_PIECES;
    }
    if (index >= SMD_OBJ_PIECES) {
        /* explicit pressure fallback: never overwrite displayed tiles unblanked. */
        smd_sprite_blank = 1;
        for (index = 0; index < SMD_OBJ_PIECES && smd_obj_slots[index].used; ++index) {
        }
    }
    if (index >= SMD_OBJ_PIECES) {
        smd_sprite_overflow = 1;
        return 65535;
    }
    slot = &smd_obj_slots[index];
    if (slot->valid) {
        previous = 0;
        link = smd_obj_heads[slot->resource];
        while (link && link != index + 1) {
            previous = link;
            link = smd_obj_slots[link - 1].next;
        }
        if (previous) {
            smd_obj_slots[previous - 1].next = slot->next;
        } else {
            smd_obj_heads[slot->resource] = slot->next;
        }
    }
    slot->resource = resource;
    slot->piece = piece;
    slot->valid = slot->used = slot->pending = 1;
    slot->next = smd_obj_heads[resource];
    smd_obj_heads[resource] = (deceptus_u8)(index + 1);
    return index;
}

static void smd_obj_reset_cache(void)
{
    memset(smd_obj_slots, 0, sizeof(smd_obj_slots));
    memset(smd_obj_heads, 0, sizeof(smd_obj_heads));
    smd_obj_unused = smd_obj_free_cursor = 0;
}

static void smd_obj_piece(deceptus_i16 x, deceptus_i16 y, deceptus_u16 tile, deceptus_u8 palette, deceptus_u8 flip,
                          deceptus_u8 width, deceptus_u8 height)
{
    deceptus_i16 top, bottom, line;
    if (x <= -(deceptus_i16)width || x >= 320 || y <= -(deceptus_i16)height || y >= 224) {
        return;
    }
    top = y < 0 ? 0 : y;
    bottom = y + height > 224 ? 224 : y + height;
    if (smd_obj_count >= 80) {
        smd_sprite_overflow = 1;
        return;
    }
    if (smd_obj_fast_budget) {
        ++smd_obj_budget.ranges.objects[top];
        --smd_obj_budget.ranges.objects[bottom];
        smd_obj_budget.ranges.slivers[top] += width / 8;
        smd_obj_budget.ranges.slivers[bottom] -= width / 8;
    } else {
        for (line = top; line < bottom; ++line) {
            if (smd_obj_lines[line] >= 20 || smd_obj_slivers[line] + width / 8 > 40) {
                smd_sprite_overflow = 1;
                return;
            }
        }
        for (line = top; line < bottom; ++line) {
            ++smd_obj_lines[line];
            smd_obj_slivers[line] += width / 8;
        }
    }
    VDP_setSpriteFull(smd_obj_count, x, y, SPRITE_SIZE(width / 8, height / 8),
                      TILE_ATTR_FULL(palette, 0, 0, flip, tile), smd_obj_count + 1);
    ++smd_obj_count;
}

static void smd_obj_quads(deceptus_i16 x, deceptus_i16 y, deceptus_u16 tile, deceptus_u8 palette, deceptus_u8 flip,
                          deceptus_u8 quads)
{
    deceptus_u8 column, bits, fits;
    deceptus_i16 px, line, top, bottom;
    fits = 0;
    if ((quads & 5) && (quads & 10)) {
        fits = 1;
        top = y < 0 ? 0 : y;
        bottom = y + 16 > 224 ? 224 : y + 16;
        for (line = top; !smd_obj_fast_budget && line < bottom; ++line) {
            if (smd_obj_lines[line] >= 20 || smd_obj_slivers[line] > 38) {
                fits = 0;
                break;
            }
        }
    }
    /* transparent quadrants may share one larger obj when its fetch budget fits. */
    if (quads == 15 || fits) {
        smd_obj_piece(x, y, tile, palette, flip, 16, 16);
        return;
    }
    for (column = 0; column < 2; ++column) {
        bits = (quads >> column) & 5;
        px = x + (flip ? 1 - column : column) * 8;
        if (bits == 5) {
            smd_obj_piece(px, y, tile + column * 2, palette, flip, 8, 16);
        } else {
            if (bits & 1) {
                smd_obj_piece(px, y, tile + column * 2, palette, flip, 8, 8);
            }
            if (bits & 4) {
                smd_obj_piece(px, y + 8, tile + column * 2 + 1, palette, flip, 8, 8);
            }
        }
    }
}

static deceptus_u16 smd_obj_sword(const deceptus_adventure *game, deceptus_i16 *x, deceptus_i16 *y)
{
    static deceptus_u8 previous_pose = 255;
    static deceptus_u16 previous_tick, pose_ticks;
    const deceptus_entity *player;
    const deceptus_u8 *clip;
    deceptus_u16 step, duration, elapsed, delta;
    deceptus_u8 pose, left;
    player = &game->campaign.engine.entities[0];
    left = player->facing_left;
    if (game->sword.ticks) {
        pose = game->sword.airborne ? 10 : game->sword.crouched ? 9 : 8;
        left = game->sword.facing_left;
    } else if (game->campaign.engine.player_rope.attached) {
        pose = 3;
    } else if (player->in_water) {
        pose = player->velocity_x ? 7 : 6;
    } else if (player->crouched) {
        pose = 5;
    } else if (!player->grounded) {
        pose = player->velocity_y < -16 ? 2 : player->velocity_y > 16 ? 4 : 3;
    } else {
        pose = player->velocity_x ? 1 : 0;
    }
    pose += left ? 11 : 0;
    delta = game->campaign.engine.ticks - previous_tick;
    pose_ticks = pose != previous_pose || delta > 4 ? 0 : pose_ticks + delta;
    previous_pose = pose;
    previous_tick = game->campaign.engine.ticks;
    clip = smd_sword_clips[pose];
    duration = 0;
    for (step = 0; step < clip[1]; ++step) {
        duration += smd_sword_durations[clip[0] + step];
    }
    while (pose_ticks >= duration) {
        pose_ticks -= duration;
    }
    elapsed = game->sword.ticks ? game->sword.ticks - 1 : pose_ticks;
    for (step = 0; step + 1 < clip[1] && elapsed >= smd_sword_durations[clip[0] + step]; ++step) {
        elapsed -= smd_sword_durations[clip[0] + step];
    }
    *x = deceptus_floor(player->x) - game->campaign.engine.camera_x - clip[2];
    *y = deceptus_floor(player->y) - game->campaign.engine.camera_y - clip[3];
    return clip[0] + step;
}

static void smd_obj_player(const deceptus_adventure *game, const deceptus_draw *draw)
{
    const smd_headtorch_pose *pose;
    const deceptus_engine *engine;
    deceptus_u16 resource, image;
    deceptus_i16 x, y, helmet_x, helmet_y;
    deceptus_u8 flip;
    engine = &game->campaign.engine;
    image = draw->frame;
    x = draw->x;
    y = draw->y;
    flip = draw->flip_x;
    if (game->has_sword) {
        image = smd_obj_sword(game, &x, &y);
        resource = SMD_SWORD_FIRST + image;
        pose = &smd_headtorch_sword[image];
        flip = 0;
    } else if (draw->kind == DECEPTUS_PLAYER_SWIM) {
        resource = SMD_SWIM_FIRST + image;
        pose = &smd_headtorch_swim[image];
    } else if (draw->kind == DECEPTUS_PLAYER_ROPE) {
        resource = SMD_PLAYER_ROPE_FIRST + image;
        pose = &smd_headtorch_rope[image];
    } else {
        resource = SMD_ACTOR_FIRST + image;
        pose = &smd_headtorch_normal[engine->entities[0].facing_left][image];
    }
    if (game->inventory.counts[5] && pose->valid) {
        helmet_x = deceptus_floor(engine->entities[0].x) - engine->camera_x + pose->x;
        helmet_y = deceptus_floor(engine->entities[0].y) - engine->camera_y + pose->y;
        smd_obj_add(SMD_HEADTORCH_FIRST + pose->side, helmet_x, helmet_y, 0, 2);
        smd_obj_add(SMD_HEADTORCH_BEAM_FIRST,
                    helmet_x + smd_headtorch_lamp_offsets[pose->side][0] - (pose->side ? 64 : 0),
                    helmet_y + smd_headtorch_lamp_offsets[pose->side][1] - 16, pose->side, 2);
    }
    smd_obj_add(resource, x, y, flip, 2);
}

static void smd_obj_mechanisms(const deceptus_engine *engine)
{
    const deceptus_mechanism_def *definition;
    const deceptus_mechanism_state *state;
    deceptus_u16 index, resource, segment, segments;
    deceptus_i16 x, y;
    for (index = 0; index < engine->mechanisms.count; ++index) {
        definition = &engine->mechanisms.definitions[index];
        state = &engine->mechanisms.states[index];
        if (!state->visible) {
            continue;
        }
        segments = 1;
        x = definition->bounds.x - engine->camera_x;
        y = definition->bounds.y - engine->camera_y;
        if (definition->kind == DECEPTUS_MECH_LEVER) {
            resource = SMD_LEVER_FIRST + (state->handle_available ? state->frame : 32);
        } else if (definition->kind == DECEPTUS_MECH_BLOCK && engine->mechanisms.timings &&
                   (engine->mechanisms.timings[index].flags & DECEPTUS_TIMED_ANIMATION)) {
            resource = SMD_BLOCK_FIRST + state->frame;
        } else if (definition->kind == DECEPTUS_MECH_SPIKES) {
            resource = SMD_SPIKES_FIRST + state->frame;
        } else if (definition->kind == DECEPTUS_MECH_COLLAPSING) {
            if (state->frame >= 38 || state->phase == 2) {
                continue;
            }
            resource = SMD_COLLAPSE_FIRST + state->frame;
            segments = definition->bounds.width / 24;
            y -= 24;
            if (state->phase == 1) {
                y += state->timer / 10;
            } else if (state->timer) {
                y += (deceptus_i16)((state->timer / 2) & 3) - 1;
            }
        } else if (definition->kind == DECEPTUS_MECH_DOOR) {
            resource = SMD_IRON_DOOR_FIRST + state->frame;
            x -= 24;
            y -= 24;
        } else if (definition->kind == DECEPTUS_MECH_DESTRUCTIBLE) {
            resource = SMD_WOODEN_PLANKS_FIRST + state->frame;
            if (definition->flags & DECEPTUS_MECH_RIGHT_ALIGNED) {
                resource += 50;
                x -= 102;
            }
        } else {
            continue;
        }
        for (segment = 0; segment < segments; ++segment) {
            smd_obj_add(resource + (definition->kind == DECEPTUS_MECH_COLLAPSING ? (segment & 3) * 38 : 0),
                        x + segment * 24, y, 0, 2);
        }
    }
}

static void smd_obj_ropes(const deceptus_engine *engine)
{
    deceptus_u16 distance, length, height;
    deceptus_u8 rope;
    deceptus_i16 x, y;
    for (rope = 0; rope < engine->grab_ropes.count; ++rope) {
        length = deceptus_grabrope_length(engine, rope);
        for (distance = 0; distance < length; distance += height) {
            height = distance ? 8 : 16;
            if (height > length - distance) {
                height = length - distance;
            }
            deceptus_grabrope_point_at(engine, rope, distance, &x, &y);
            smd_obj_add(SMD_GRAB_ROPE_FIRST + (distance ? height : 0), x - engine->camera_x - 7,
                        y - engine->camera_y - (distance ? 4 : 0), 0, 3);
        }
    }
}

static void smd_sprites_init(void)
{
    deceptus_u16 index;
    if (!smd_sprite_initialized) {
        smd_obj_reset_cache();
        smd_sprite_initialized = 1;
    }
    for (index = 0; index < SMD_OBJ_PIECES; ++index) {
        smd_obj_slots[index].used = smd_obj_slots[index].held = 0;
    }
    VDP_resetSprites();
    smd_sprite_room_load = 1;
}

static void smd_sprites_prepare_pass(const deceptus_adventure *game, const deceptus_frame *frame)
{
    deceptus_u16 index, other, resource, row, column, piece, slot, active_count;
    deceptus_i16 x, y;
    deceptus_u8 palette, link, quads;
    const deceptus_draw *draw;
    const smd_sprite_resource *entry;
    const smd_obj_request *request;
    smd_obj_request_count = smd_obj_count = 0;
    smd_sprite_overflow = smd_sprite_blank = 0;
    smd_obj_free_cursor = 0;
    memset(&smd_obj_budget, 0, sizeof(smd_obj_budget));
    for (index = 0; index < SMD_OBJ_PIECES; ++index) {
        smd_obj_slots[index].used = 0;
    }
    if (game->campaign.mode != DECEPTUS_CAMPAIGN_TITLE) {
        for (index = 0; index < frame->count; ++index) {
            draw = &frame->draws[index];
            if (draw->kind == DECEPTUS_PLAYER || draw->kind == DECEPTUS_PLAYER_SWIM ||
                draw->kind == DECEPTUS_PLAYER_ROPE) {
                smd_obj_player(game, draw);
                continue;
            }
            if (draw->kind == DECEPTUS_RAT) {
                resource = SMD_RAT_FIRST;
            } else if (draw->kind == DECEPTUS_BLOB_2) {
                resource = SMD_BLOB_FIRST;
            } else if (draw->kind == DECEPTUS_ARROW_TRAP) {
                resource = SMD_TRAP_FIRST;
            } else if (draw->kind == DECEPTUS_ARROW_MUZZLE) {
                resource = SMD_MUZZLE_FIRST;
            } else if (draw->kind == DECEPTUS_ARROW) {
                resource = SMD_ARROW_FIRST;
            } else if (draw->kind == DECEPTUS_CANNON_2) {
                resource = SMD_CANNON_FIRST;
            } else if (draw->kind == DECEPTUS_CANNONBALL) {
                resource = SMD_CANNONBALL_FIRST;
            } else if (draw->kind == DECEPTUS_ORB) {
                resource = SMD_ORB_FIRST;
            } else if (draw->kind == DECEPTUS_FROG_BLUE) {
                resource = SMD_FROG_BLUE_FIRST;
            } else if (draw->kind == DECEPTUS_BLUE_FIREBALL) {
                resource = SMD_BLUE_FIREBALL_FIRST;
            } else if (draw->kind == DECEPTUS_WATER_SPLASH) {
                resource = SMD_SPLASH_FIRST;
            } else {
                smd_sprite_overflow = 1;
                continue;
            }
            smd_obj_add(resource + draw->frame, draw->x, draw->y, draw->flip_x, 2);
        }
        smd_obj_mechanisms(&game->campaign.engine);
        smd_obj_ropes(&game->campaign.engine);
    }
    /* protect all existing frames needed later in this presentation. */
    active_count = 0;
    if (!++smd_obj_epoch) {
        memset(smd_obj_epochs, 0, sizeof(smd_obj_epochs));
        ++smd_obj_epoch;
    }
    for (index = 0; index < smd_obj_request_count; ++index) {
        resource = smd_obj_requests[index].resource;
        if (smd_obj_epochs[resource] != smd_obj_epoch) {
            smd_obj_epochs[resource] = smd_obj_epoch;
            smd_obj_active_resources[active_count++] = resource;
        }
    }
    for (other = 0; other < active_count; ++other) {
        for (link = smd_obj_heads[smd_obj_active_resources[other]]; link; link = smd_obj_slots[link - 1].next) {
            smd_obj_slots[link - 1].used = 1;
        }
    }
    for (index = 0; index < smd_obj_request_count; ++index) {
        request = &smd_obj_requests[index];
        entry = &smd_sprite_resources[request->resource];
        palette = entry->palette_bank;
        for (row = 0; row < entry->rows; ++row) {
            for (column = 0; column < entry->columns; ++column) {
                x = request->x + (request->flip ? entry->columns - column - 1 : column) * 16;
                y = request->y + row * 16;
                if (x <= -16 || x >= 320 || y <= -16 || y >= 224) {
                    continue;
                }
                piece = row * entry->columns + column;
#ifdef SMD_SPRITE_OPAQUE_MASK
                if (!(smd_sprite_opaque[request->resource][piece >> 3] & (1U << (piece & 7)))) {
                    continue;
                }
#endif
                slot = smd_obj_slot_for(request->resource, (deceptus_u8)piece);
                if (slot != 65535) {
                    quads = 15;
#ifdef SMD_SPRITE_QUADS
                    quads = smd_sprite_quads[smd_sprite_quad_offsets[request->resource] + piece];
#endif
                    smd_obj_quads(x, y, smd_obj_tile(slot), palette, request->flip, quads);
                }
            }
        }
    }
}

static deceptus_u8 smd_obj_budget_fits(void)
{
    deceptus_i16 objects, slivers;
    deceptus_u16 line;
    for (line = 0, objects = slivers = 0; line < 224; ++line) {
        objects += smd_obj_budget.ranges.objects[line];
        slivers += smd_obj_budget.ranges.slivers[line];
        if (objects > 20 || slivers > 40) {
            return 0;
        }
    }
    return 1;
}

static void smd_sprites_prepare(const deceptus_adventure *game, const deceptus_frame *frame)
{
    deceptus_u8 blank, overflow;
    smd_obj_fast_budget = 1;
    smd_sprites_prepare_pass(game, frame);
    if (!smd_obj_budget_fits()) {
        /* keep the exact quadrant path for scenes that need finer packing. */
        blank = smd_sprite_blank;
        overflow = smd_sprite_overflow;
        smd_obj_fast_budget = 0;
        smd_sprites_prepare_pass(game, frame);
        smd_sprite_blank |= blank;
        smd_sprite_overflow |= overflow;
    }
}

static void smd_sprites_measure(void)
{
    deceptus_u16 index;
    smd_sprite_upload_bytes = 0;
    for (index = 0; index < SMD_OBJ_PIECES; ++index) {
        if (smd_obj_slots[index].pending) {
            smd_sprite_upload_bytes += 128;
        }
    }
}

static void smd_sprites_preload(void)
{
    deceptus_u16 index;
    smd_obj_slot *slot;
    for (index = 0; index < SMD_OBJ_PIECES; ++index) {
        slot = &smd_obj_slots[index];
        if (!slot->pending) {
            continue;
        }
        if (!smd_decode_block(&smd_sprite_resources[slot->resource].graphics, slot->piece,
                              (deceptus_u8 *)smd_sprite_buffer)) {
            return;
        }
        VDP_loadTileData(smd_sprite_buffer, smd_obj_tile(index), 4, DMA);
        slot->pending = 0;
    }
    smd_sprite_room_load = 0;
}

static void smd_sprites_commit(void)
{
    deceptus_u16 index;
    if (smd_obj_count) {
        VDP_setSpriteLink(smd_obj_count - 1, 0);
        VDP_updateSprites(smd_obj_count, DMA);
    } else {
        VDP_setSpriteFull(0, 0, 240, SPRITE_SIZE(1, 1), 0, 0);
        VDP_updateSprites(1, DMA);
    }
    for (index = 0; index < SMD_OBJ_PIECES; ++index) {
        smd_obj_slots[index].held = smd_obj_slots[index].used;
    }
}
