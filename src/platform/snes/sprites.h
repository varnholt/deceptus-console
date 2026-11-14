/* logical source frames mapped to bounded SNES OBJ tiles and palette banks. */
#include "deceptus/enemies.h"
#include "snes_headtorch_resources.h"
#include "snes_sword_resources.h"
#include <string.h>

#define SNES_OBJ_REQUESTS 96
#define SNES_OBJ_PIECES 128

typedef struct snes_obj_request {
    deceptus_u16 resource;
    deceptus_i16 x, y;
    deceptus_u8 flip, priority;
} snes_obj_request;

typedef struct snes_obj_slot {
    deceptus_u16 resource;
    deceptus_u8 piece, valid, used, held, pending, next;
} snes_obj_slot;

static snes_obj_request snes_obj_requests[SNES_OBJ_REQUESTS];
static deceptus_u16 snes_obj_active_resources[SNES_OBJ_REQUESTS];
static snes_obj_slot snes_obj_slots[SNES_OBJ_PIECES];
static deceptus_u8 snes_obj_heads[SNES_SPRITE_RESOURCE_COUNT], snes_obj_epochs[SNES_SPRITE_RESOURCE_COUNT];
static deceptus_u8 snes_obj_epoch;
static deceptus_u16 snes_obj_unused, snes_obj_free_cursor;
static deceptus_u8 snes_obj_oam[544];
static deceptus_u8 snes_obj_lines[224], snes_obj_slivers[224];
static const deceptus_u16 *snes_obj_palettes[8];
static deceptus_u16 snes_obj_request_count, snes_obj_count, snes_obj_palette_count;
static deceptus_u8 snes_sprite_overflow, snes_sprite_room_load, snes_sprite_blank;
static deceptus_u8 snes_sprite_initialized;
static deceptus_u16 snes_sprite_upload_bytes;

static void snes_obj_add(deceptus_u16 resource, deceptus_i16 x, deceptus_i16 y, deceptus_u8 flip, deceptus_u8 priority)
{
    const snes_sprite_resource *entry;
    snes_obj_request *request;
    if (resource >= SNES_SPRITE_RESOURCE_COUNT) {
        snes_sprite_overflow = 1;
        return;
    }
    entry = &snes_sprite_resources[resource];
    x += entry->trim_x;
    y += entry->trim_y;
    if (x >= 256 || y >= 224 || x + entry->columns * 16 <= 0 || y + entry->rows * 16 <= 0) {
        return;
    }
    if (snes_obj_request_count >= SNES_OBJ_REQUESTS || entry->columns * entry->rows > 64) {
        snes_sprite_overflow = 1;
        return;
    }
    request = &snes_obj_requests[snes_obj_request_count++];
    request->resource = resource;
    request->x = x;
    request->y = y;
    request->flip = flip;
    request->priority = priority;
}

static deceptus_u16 snes_obj_tile(deceptus_u16 slot)
{
    return (slot / 64) * 256 + ((slot & 63) / 8) * 32 + (slot & 7) * 2;
}

static deceptus_u16 snes_obj_slot_for(deceptus_u16 resource, deceptus_u8 piece)
{
    deceptus_u16 index;
    deceptus_u8 link, previous;
    snes_obj_slot *slot;
    for (link = snes_obj_heads[resource]; link; link = slot->next) {
        slot = &snes_obj_slots[link - 1];
        if (slot->piece == piece) {
            slot->used = 1;
            return link - 1;
        }
    }
    if (snes_obj_unused < SNES_OBJ_PIECES) {
        index = snes_obj_unused++;
    } else {
        for (index = snes_obj_free_cursor; index < SNES_OBJ_PIECES; ++index) {
            slot = &snes_obj_slots[index];
            if (!slot->used && !slot->held) {
                break;
            }
        }
        snes_obj_free_cursor = index < SNES_OBJ_PIECES ? index + 1 : SNES_OBJ_PIECES;
    }
    if (index >= SNES_OBJ_PIECES) {
        /* explicit pressure fallback: never overwrite displayed tiles unblanked. */
        snes_sprite_blank = 1;
        for (index = 0; index < SNES_OBJ_PIECES && snes_obj_slots[index].used; ++index) {
        }
    }
    if (index >= SNES_OBJ_PIECES) {
        snes_sprite_overflow = 1;
        return 65535;
    }
    slot = &snes_obj_slots[index];
    if (slot->valid) {
        previous = 0;
        link = snes_obj_heads[slot->resource];
        while (link && link != index + 1) {
            previous = link;
            link = snes_obj_slots[link - 1].next;
        }
        if (previous) {
            snes_obj_slots[previous - 1].next = slot->next;
        } else {
            snes_obj_heads[slot->resource] = slot->next;
        }
    }
    slot->resource = resource;
    slot->piece = piece;
    slot->valid = slot->used = slot->pending = 1;
    slot->next = snes_obj_heads[resource];
    snes_obj_heads[resource] = (deceptus_u8)(index + 1);
    return index;
}

static void snes_obj_reset_cache(void)
{
    memset(snes_obj_slots, 0, sizeof(snes_obj_slots));
    memset(snes_obj_heads, 0, sizeof(snes_obj_heads));
    snes_obj_unused = snes_obj_free_cursor = 0;
}

static deceptus_u8 snes_obj_palette(const deceptus_u16 *palette)
{
    deceptus_u16 index, colour;
    for (index = 0; index < snes_obj_palette_count; ++index) {
        if (snes_obj_palettes[index] == palette) {
            return (deceptus_u8)index;
        }
        for (colour = 0; colour < 16 && snes_obj_palettes[index][colour] == palette[colour]; ++colour) {
        }
        if (colour == 16) {
            return (deceptus_u8)index;
        }
    }
    if (snes_obj_palette_count == 8) {
        snes_sprite_overflow = 1;
        return 0;
    }
    snes_obj_palettes[snes_obj_palette_count++] = palette;
    return (deceptus_u8)(snes_obj_palette_count - 1);
}

static void snes_obj_piece(deceptus_i16 x, deceptus_i16 y, deceptus_u16 tile, deceptus_u8 palette, deceptus_u8 flip,
                           deceptus_u8 priority, deceptus_u8 size)
{
    deceptus_i16 line, top, bottom;
    deceptus_u16 object, high, shift;
    if (x <= -(deceptus_i16)size) {
        return;
    }
    if (x >= 256) {
        return;
    }
    if (y <= -(deceptus_i16)size) {
        return;
    }
    if (y >= 224) {
        return;
    }
    top = y < 0 ? 0 : y;
    bottom = y + size > 224 ? 224 : y + size;
    for (line = top; line < bottom; ++line) {
        if (snes_obj_lines[line] >= 32) {
            snes_sprite_overflow = 1;
            return;
        }
        if (snes_obj_slivers[line] + size / 8 > 34) {
            snes_sprite_overflow = 1;
            return;
        }
    }
    if (snes_obj_count == 128) {
        snes_sprite_overflow = 1;
        return;
    }
    for (line = top; line < bottom; ++line) {
        ++snes_obj_lines[line];
        snes_obj_slivers[line] += size / 8;
    }
    object = snes_obj_count * 4;
    snes_obj_oam[object] = (deceptus_u8)x;
    snes_obj_oam[object + 1] = (deceptus_u8)y;
    snes_obj_oam[object + 2] = (deceptus_u8)tile;
    snes_obj_oam[object + 3] = (deceptus_u8)((tile >> 8) | (palette << 1) | (priority << 4) | (flip ? 0x40 : 0));
    high = 512 + snes_obj_count / 4;
    shift = (snes_obj_count & 3) * 2;
    snes_obj_oam[high] |= (deceptus_u8)(((((deceptus_u16)x >> 8) & 1) | (size == 16 ? 2 : 0)) << shift);
    ++snes_obj_count;
}

static deceptus_u16 snes_obj_sword(const deceptus_adventure *game, deceptus_i16 *x, deceptus_i16 *y)
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
    clip = snes_sword_clips[pose];
    duration = 0;
    for (step = 0; step < clip[1]; ++step) {
        duration += snes_sword_durations[clip[0] + step];
    }
    while (pose_ticks >= duration) {
        pose_ticks -= duration;
    }
    elapsed = game->sword.ticks ? game->sword.ticks - 1 : pose_ticks;
    for (step = 0; step + 1 < clip[1] && elapsed >= snes_sword_durations[clip[0] + step]; ++step) {
        elapsed -= snes_sword_durations[clip[0] + step];
    }
    *x = deceptus_floor(player->x) - game->campaign.engine.camera_x - clip[2];
    *y = deceptus_floor(player->y) - game->campaign.engine.camera_y - clip[3];
    return clip[0] + step;
}

static void snes_obj_player(const deceptus_adventure *game, const deceptus_draw *draw)
{
    const snes_headtorch_pose *pose;
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
        image = snes_obj_sword(game, &x, &y);
        resource = SNES_SWORD_FIRST + image;
        pose = &snes_headtorch_sword[image];
        flip = 0;
    } else if (draw->kind == DECEPTUS_PLAYER_SWIM) {
        resource = SNES_SWIM_FIRST + image;
        pose = &snes_headtorch_swim[image];
    } else if (draw->kind == DECEPTUS_PLAYER_ROPE) {
        resource = SNES_PLAYER_ROPE_FIRST + image;
        pose = &snes_headtorch_rope[image];
    } else {
        resource = SNES_ACTOR_FIRST + image;
        pose = &snes_headtorch_normal[engine->entities[0].facing_left][image];
    }
    if (game->inventory.counts[5] && pose->valid) {
        helmet_x = deceptus_floor(engine->entities[0].x) - engine->camera_x + pose->x;
        helmet_y = deceptus_floor(engine->entities[0].y) - engine->camera_y + pose->y;
        snes_obj_add(SNES_HEADTORCH_FIRST + pose->side, helmet_x, helmet_y, 0, 2);
        snes_obj_add(SNES_HEADTORCH_BEAM_FIRST,
                     helmet_x + snes_headtorch_lamp_offsets[pose->side][0] - (pose->side ? 64 : 0),
                     helmet_y + snes_headtorch_lamp_offsets[pose->side][1] - 16, pose->side, 2);
    }
    snes_obj_add(resource, x, y, flip, 2);
}

static void snes_obj_mechanisms(const deceptus_engine *engine)
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
            resource = SNES_LEVER_FIRST + (state->handle_available ? state->frame : 32);
        } else if (definition->kind == DECEPTUS_MECH_BLOCK && engine->mechanisms.timings &&
                   (engine->mechanisms.timings[index].flags & DECEPTUS_TIMED_ANIMATION)) {
            resource = SNES_BLOCK_FIRST + state->frame;
        } else if (definition->kind == DECEPTUS_MECH_SPIKES) {
            resource = SNES_SPIKES_FIRST + state->frame;
        } else if (definition->kind == DECEPTUS_MECH_COLLAPSING) {
            if (state->frame >= 38 || state->phase == 2) {
                continue;
            }
            resource = SNES_COLLAPSE_FIRST + state->frame;
            segments = definition->bounds.width / 24;
            y -= 24;
            if (state->phase == 1) {
                y += state->timer / 10;
            } else if (state->timer) {
                y += (deceptus_i16)((state->timer / 2) & 3) - 1;
            }
        } else if (definition->kind == DECEPTUS_MECH_DOOR) {
            resource = SNES_IRON_DOOR_FIRST + state->frame;
            x -= 24;
            y -= 24;
        } else if (definition->kind == DECEPTUS_MECH_DESTRUCTIBLE) {
            resource = SNES_WOODEN_PLANKS_FIRST + state->frame;
            if (definition->flags & DECEPTUS_MECH_RIGHT_ALIGNED) {
                resource += 50;
                x -= 102;
            }
        } else {
            continue;
        }
        for (segment = 0; segment < segments; ++segment) {
            snes_obj_add(resource + (definition->kind == DECEPTUS_MECH_COLLAPSING ? (segment & 3) * 38 : 0),
                         x + segment * 24, y, 0, 2);
        }
    }
}

static void snes_obj_ropes(const deceptus_engine *engine)
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
            snes_obj_add(SNES_GRAB_ROPE_FIRST + (distance ? height : 0), x - engine->camera_x - 7,
                         y - engine->camera_y - (distance ? 4 : 0), 0, 3);
        }
    }
}

static void snes_sprites_init(void)
{
    deceptus_u16 index;
    if (!snes_sprite_initialized) {
        snes_obj_reset_cache();
        snes_sprite_initialized = 1;
    }
    for (index = 0; index < SNES_OBJ_PIECES; ++index) {
        snes_obj_slots[index].used = snes_obj_slots[index].held = 0;
    }
    oamInitGfxAttr(0x6000, OBJ_SIZE8_L16);
    oamClear(0, 0);
    snes_sprite_room_load = 1;
}

static void snes_sprites_prepare(const deceptus_adventure *game, const deceptus_frame *frame)
{
    deceptus_u16 index, other, resource, row, column, piece, slot, active_count;
    deceptus_i16 x, y;
    deceptus_u8 palette, link, quads, quadrant;
    const deceptus_draw *draw;
    const snes_sprite_resource *entry;
    const snes_obj_request *request;
    snes_obj_request_count = snes_obj_count = snes_obj_palette_count = 0;
    snes_sprite_overflow = snes_sprite_blank = 0;
    snes_obj_free_cursor = 0;
    memset(snes_obj_oam, 0, sizeof(snes_obj_oam));
    memset(snes_obj_lines, 0, sizeof(snes_obj_lines));
    memset(snes_obj_slivers, 0, sizeof(snes_obj_slivers));
    for (index = 0; index < 128; ++index) {
        snes_obj_oam[index * 4 + 1] = 240;
        snes_obj_slots[index].used = 0;
    }
    if (game->campaign.mode != DECEPTUS_CAMPAIGN_TITLE) {
        for (index = 0; index < frame->count; ++index) {
            draw = &frame->draws[index];
            if (draw->kind == DECEPTUS_PLAYER || draw->kind == DECEPTUS_PLAYER_SWIM ||
                draw->kind == DECEPTUS_PLAYER_ROPE) {
                snes_obj_player(game, draw);
                continue;
            }
            if (draw->kind == DECEPTUS_RAT) {
                resource = SNES_RAT_FIRST;
            } else if (draw->kind == DECEPTUS_BLOB_2) {
                resource = SNES_BLOB_FIRST;
            } else if (draw->kind == DECEPTUS_ARROW_TRAP) {
                resource = SNES_TRAP_FIRST;
            } else if (draw->kind == DECEPTUS_ARROW_MUZZLE) {
                resource = SNES_MUZZLE_FIRST;
            } else if (draw->kind == DECEPTUS_ARROW) {
                resource = SNES_ARROW_FIRST;
            } else if (draw->kind == DECEPTUS_CANNON_2) {
                resource = SNES_CANNON_FIRST;
            } else if (draw->kind == DECEPTUS_CANNONBALL) {
                resource = SNES_CANNONBALL_FIRST;
            } else if (draw->kind == DECEPTUS_ORB) {
                resource = SNES_ORB_FIRST;
            } else if (draw->kind == DECEPTUS_FROG_BLUE) {
                resource = SNES_FROG_BLUE_FIRST;
            } else if (draw->kind == DECEPTUS_BLUE_FIREBALL) {
                resource = SNES_BLUE_FIREBALL_FIRST;
            } else if (draw->kind == DECEPTUS_WATER_SPLASH) {
                resource = SNES_SPLASH_FIRST;
            } else {
                snes_sprite_overflow = 1;
                continue;
            }
            snes_obj_add(resource + draw->frame, draw->x, draw->y, draw->flip_x, 2);
        }
        snes_obj_mechanisms(&game->campaign.engine);
        snes_obj_ropes(&game->campaign.engine);
    }
    /* protect all existing frames needed later in this presentation. */
    active_count = 0;
    if (!++snes_obj_epoch) {
        memset(snes_obj_epochs, 0, sizeof(snes_obj_epochs));
        ++snes_obj_epoch;
    }
    for (index = 0; index < snes_obj_request_count; ++index) {
        resource = snes_obj_requests[index].resource;
        if (snes_obj_epochs[resource] != snes_obj_epoch) {
            snes_obj_epochs[resource] = snes_obj_epoch;
            snes_obj_active_resources[active_count++] = resource;
        }
    }
    for (other = 0; other < active_count; ++other) {
        for (link = snes_obj_heads[snes_obj_active_resources[other]]; link; link = snes_obj_slots[link - 1].next) {
            snes_obj_slots[link - 1].used = 1;
        }
    }
    for (index = 0; index < snes_obj_request_count; ++index) {
        request = &snes_obj_requests[index];
        entry = &snes_sprite_resources[request->resource];
        palette = snes_obj_palette(entry->palette);
        for (row = 0; row < entry->rows; ++row) {
            for (column = 0; column < entry->columns; ++column) {
                x = request->x + (request->flip ? entry->columns - column - 1 : column) * 16;
                y = request->y + row * 16;
                if (x <= -16 || x >= 256 || y <= -16 || y >= 224) {
                    continue;
                }
                piece = row * entry->columns + column;
#ifdef SNES_SPRITE_OPAQUE_MASK
                if (!(snes_sprite_opaque[request->resource][piece >> 3] & (1U << (piece & 7)))) {
                    continue;
                }
#endif
                slot = snes_obj_slot_for(request->resource, (deceptus_u8)piece);
                if (slot != 65535) {
                    quads = 15;
#ifdef SNES_SPRITE_QUADS
                    quads = snes_sprite_quads[snes_sprite_quad_offsets[request->resource] + piece];
#endif
                    if (quads == 15) {
                        snes_obj_piece(x, y, snes_obj_tile(slot), palette, request->flip, request->priority, 16);
                    } else {
                        for (quadrant = 0; quadrant < 4; ++quadrant) {
                            if (quads & (1U << quadrant)) {
                                snes_obj_piece(x + (request->flip ? 1 - (quadrant & 1) : quadrant & 1) * 8,
                                               y + (quadrant >> 1) * 8,
                                               snes_obj_tile(slot) + (quadrant & 1) + (quadrant >> 1) * 16, palette,
                                               request->flip, request->priority, 8);
                            }
                        }
                    }
                }
            }
        }
    }
}

static void snes_sprites_preload(void)
{
    deceptus_u16 index, tile, budget;
    const deceptus_u8 *graphics;
    snes_obj_slot *slot;
    budget = 0;
    snes_sprite_upload_bytes = 0;
    if (snes_sprite_blank) {
        setScreenOff();
    }
    for (index = 0; index < SNES_OBJ_PIECES; ++index) {
        slot = &snes_obj_slots[index];
        if (!slot->pending) {
            continue;
        }
        if (!snes_sprite_room_load && !snes_sprite_blank && (!budget || budget >= 2048)) {
            WaitForVBlank();
            budget = 0;
        }
        tile = 0x6000 + snes_obj_tile(index) * 16;
        graphics = snes_sprite_resources[slot->resource].graphics + slot->piece * 128;
        dmaCopyVram((u8 *)graphics, tile, 64);
        dmaCopyVram((u8 *)(graphics + 64), tile + 256, 64);
        slot->pending = 0;
        budget += 128;
        snes_sprite_upload_bytes += 128;
    }
    snes_sprite_room_load = 0;
}

static void snes_sprites_commit(void)
{
    deceptus_u16 index;
    for (index = 0; index < snes_obj_palette_count; ++index) {
        dmaCopyCGram((u8 *)snes_obj_palettes[index], 128 + index * 16, 32);
    }
    dmaCopyOAram(snes_obj_oam, 0, sizeof(snes_obj_oam));
    /* the next SDK VBlank publishes this only after every new tile is ready. */
    memcpy(oamMemory, snes_obj_oam, sizeof(snes_obj_oam));
    for (index = 0; index < SNES_OBJ_PIECES; ++index) {
        snes_obj_slots[index].held = snes_obj_slots[index].used;
    }
}
