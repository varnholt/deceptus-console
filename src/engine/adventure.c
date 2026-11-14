#include "deceptus/adventure.h"
#include <string.h>

deceptus_u8 deceptus_inventory_add(deceptus_inventory *inventory, deceptus_u8 item)
{
    if (!item || item >= DECEPTUS_INVENTORY_ITEMS || inventory->counts[item] == 255) {
        return 0;
    }
    ++inventory->counts[item];
    return 1;
}

deceptus_u8 deceptus_inventory_remove(deceptus_inventory *inventory, deceptus_u8 item)
{
    if (!item || item >= DECEPTUS_INVENTORY_ITEMS || !inventory->counts[item]) {
        return 0;
    }
    --inventory->counts[item];
    return 1;
}

static deceptus_u8 overlaps(const deceptus_rect *bounds, deceptus_i16 x, deceptus_i16 y)
{
    return x + 8 > bounds->x && x - 8 < bounds->x + bounds->width && y > bounds->y &&
           y - 30 < bounds->y + bounds->height;
}

deceptus_u8 deceptus_interaction_available(const deceptus_adventure *game, const deceptus_interaction *interaction)
{
    deceptus_i16 slot;
    if (interaction->effect == DECEPTUS_ACTION_EXAMINE_BLOCKER) {
        slot = deceptus_mechanism_find(&game->campaign.engine.mechanisms, interaction->mechanism_id);
        if (slot < 0) {
            return 0;
        }
        if (game->campaign.engine.mechanisms.definitions[slot].kind == DECEPTUS_MECH_DOOR) {
            if (game->campaign.engine.mechanisms.states[slot].enabled) {
                return 0;
            }
        } else if (!game->campaign.engine.mechanisms.states[slot].active ||
                   game->campaign.engine.mechanisms.states[slot].triggered) {
            return 0;
        }
    }
    if (interaction->effect == DECEPTUS_ACTION_SWORD && interaction->mechanism_id) {
        slot = deceptus_mechanism_find(&game->campaign.engine.mechanisms, interaction->mechanism_id);
        if (slot < 0 || game->campaign.engine.mechanisms.states[slot].enabled) {
            return 0;
        }
    }
    return (game->flags & interaction->required_flags) == interaction->required_flags &&
           !(game->flags & interaction->forbidden_flags) &&
           (!interaction->cost_item ||
            (interaction->cost_item < DECEPTUS_INVENTORY_ITEMS && game->inventory.counts[interaction->cost_item]));
}

void deceptus_adventure_init(deceptus_adventure *game, const deceptus_adventure_def *definition,
                             const deceptus_video_caps *video)
{
    deceptus_video_caps saved_video;
    deceptus_u8 index;
    saved_video = *video;
    memset(game, 0, sizeof(*game));
    game->definition = definition;
    game->prompt = 255;
    game->room_label_ticks = 120;
    if (!definition || !definition->campaign || !definition->rooms || !definition->campaign->rooms ||
        !definition->campaign->room_count || definition->campaign->room_count > DECEPTUS_ADVENTURE_ROOMS ||
        definition->item_count >= DECEPTUS_INVENTORY_ITEMS) {
        game->campaign.engine.content_error = 8;
        return;
    }
    game->campaign.definition = definition->campaign;
    game->campaign.video = saved_video;
    for (index = 0; index < definition->campaign->room_count; ++index) {
        if (!definition->campaign->rooms[index] ||
            (definition->rooms[index].binding_count && !definition->rooms[index].bindings) ||
            (definition->rooms[index].portal_count && !definition->rooms[index].portals) ||
            (definition->rooms[index].interaction_count && !definition->rooms[index].interactions)) {
            game->campaign.engine.content_error = 8;
            return;
        }
    }
    deceptus_init(&game->campaign.engine, definition->campaign->rooms[0], &saved_video);
    deceptus_tick(&game->campaign.engine, 0);
}

static void save_room(deceptus_adventure *game)
{
    deceptus_u8 room;
    room = game->campaign.room_index;
    memcpy(game->saved[room], game->campaign.engine.mechanisms.states, sizeof(game->saved[room]));
    game->visited[room] = 1;
}

static deceptus_u8 linked_valid(const deceptus_mechanism_set *set, deceptus_u16 source_id)
{
    return deceptus_mechanism_find(set, source_id) >= 0;
}

static void enable_linked(deceptus_mechanism_set *set, deceptus_u16 source_id, deceptus_u8 enabled)
{
    const deceptus_mechanism_def *definition;
    deceptus_i16 slot;
    deceptus_u16 target;
    slot = deceptus_mechanism_find(set, source_id);
    if (slot < 0) {
        return;
    }
    definition = &set->definitions[slot];
    deceptus_mechanism_enable(set, source_id, enabled);
    for (target = 0; target < definition->target_count; ++target) {
        deceptus_mechanism_enable(set, set->targets[definition->target_first + target], enabled);
    }
}

static void apply_bindings(deceptus_adventure *game)
{
    const deceptus_adventure_room *room;
    const deceptus_adventure_binding *binding;
    deceptus_u8 index;
    room = &game->definition->rooms[game->campaign.room_index];
    for (index = 0; index < room->binding_count; ++index) {
        binding = &room->bindings[index];
        if ((game->flags & binding->required_flags) != binding->required_flags ||
            (game->flags & binding->forbidden_flags)) {
            continue;
        }
        /* the eyes appear immediately; the source ward releases 0.45 seconds later. */
        if (game->pending_ticks && game->pending_room == game->campaign.room_index &&
            game->pending_action->mechanism_id == binding->mechanism_id) {
            continue;
        }
        if (!linked_valid(&game->campaign.engine.mechanisms, binding->mechanism_id)) {
            game->campaign.engine.content_error = 8;
            return;
        }
        enable_linked(&game->campaign.engine.mechanisms, binding->mechanism_id, binding->enabled);
    }
}

static void load_room(deceptus_adventure *game, deceptus_u8 room, deceptus_u16 buttons)
{
    deceptus_engine *engine;
    deceptus_u8 health;
    engine = &game->campaign.engine;
    health = engine->health;
    game->campaign.room_index = room;
    deceptus_init(engine, game->definition->campaign->rooms[room], &game->campaign.video);
    deceptus_playersword_init(&game->sword);
    engine->health = health;
    if (game->visited[room]) {
        memcpy(engine->mechanisms.states, game->saved[room], sizeof(game->saved[room]));
    }
    apply_bindings(game);
    engine->previous_buttons = buttons;
    game->automatic_action = 0;
    game->automatic_ticks = 0;
    game->portal_cooldown = 12;
    game->room_label_ticks = 120;
    game->prompt = 255;
}

static deceptus_u8 interact(deceptus_adventure *game, const deceptus_interaction *action)
{
    deceptus_i16 slot;
    slot = -1;
    if (action->effect == DECEPTUS_ACTION_HANDLE) {
        slot = deceptus_mechanism_find(&game->campaign.engine.mechanisms, action->mechanism_id);
        if (slot < 0 || game->campaign.engine.mechanisms.definitions[slot].kind != DECEPTUS_MECH_LEVER) {
            return 0;
        }
    }
    if (action->effect == DECEPTUS_ACTION_DOOR) {
        slot = deceptus_mechanism_find(&game->campaign.engine.mechanisms, action->mechanism_id);
        if (slot < 0 || game->campaign.engine.mechanisms.definitions[slot].kind != DECEPTUS_MECH_DOOR ||
            game->campaign.engine.mechanisms.states[slot].cooldown) {
            return 0;
        }
    }
    if ((action->effect == DECEPTUS_ACTION_DISABLE_LINKED || action->effect == DECEPTUS_ACTION_OWL_EYES) &&
        (!linked_valid(&game->campaign.engine.mechanisms, action->mechanism_id) ||
         (action->effect == DECEPTUS_ACTION_OWL_EYES && game->pending_ticks))) {
        return 0;
    }
    if (action->grant_item &&
        (action->grant_item >= DECEPTUS_INVENTORY_ITEMS || game->inventory.counts[action->grant_item] == 255)) {
        return 0;
    }
    if (action->cost_item && action->effect != DECEPTUS_ACTION_DOOR &&
        !deceptus_inventory_remove(&game->inventory, action->cost_item)) {
        return 0;
    }
    if (action->grant_item) {
        deceptus_inventory_add(&game->inventory, action->grant_item);
    }
    if (slot >= 0 && action->effect == DECEPTUS_ACTION_HANDLE) {
        game->campaign.engine.mechanisms.states[slot].handle_available = 1;
    }
    game->flags |= action->set_flags;
    if (action->effect == DECEPTUS_ACTION_DISABLE_LINKED) {
        enable_linked(&game->campaign.engine.mechanisms, action->mechanism_id, 0);
    } else if (action->effect == DECEPTUS_ACTION_OWL_EYES) {
        game->pending_action = action;
        game->pending_room = game->campaign.room_index;
        game->pending_ticks = 27;
    } else if (action->effect == DECEPTUS_ACTION_SWORD) {
        game->has_sword = 1;
    } else if (action->effect == DECEPTUS_ACTION_DOOR) {
        deceptus_mechanism_enable(&game->campaign.engine.mechanisms, action->mechanism_id,
                                  !game->campaign.engine.mechanisms.states[slot].enabled);
    }
    apply_bindings(game);
    deceptus_event_push(&game->campaign.engine, action->effect == DECEPTUS_ACTION_DOOR ? DECEPTUS_EVENT_DOOR
                                                : action->effect == DECEPTUS_ACTION_EXAMINE_BLOCKER
                                                    ? DECEPTUS_EVENT_DIALOGUE
                                                : action->grant_item ? DECEPTUS_EVENT_ITEM
                                                : action->effect     ? DECEPTUS_EVENT_LEVER_INSERT
                                                : action->cost_item  ? DECEPTUS_EVENT_DOOR
                                                                     : DECEPTUS_EVENT_DIALOGUE);
    if (action->page_count) {
        game->dialogue = action;
        game->page = 0;
        game->campaign.mode =
            (action->effect & DECEPTUS_ACTION_NOTIFICATION) ? DECEPTUS_NOTIFICATION : DECEPTUS_DIALOGUE;
    }
    return 1;
}

deceptus_u8 deceptus_adventure_tick(deceptus_adventure *game, deceptus_u16 buttons)
{
    deceptus_campaign *campaign;
    deceptus_engine *engine;
    const deceptus_adventure_room *room;
    const deceptus_interaction *action;
    const deceptus_portal *portal;
    deceptus_u16 pressed, deaths;
    deceptus_i16 x, y;
    deceptus_fixed velocity_x, velocity_y;
    deceptus_u8 index, changed, direction;
    campaign = &game->campaign;
    engine = &campaign->engine;
    engine->event_count = 0;
    engine->event_overflow = 0;
    pressed = buttons & ~campaign->previous_buttons;
    campaign->previous_buttons = buttons;
    if (engine->content_error) {
        return 0;
    }
    if (campaign->mode == DECEPTUS_CAMPAIGN_TITLE) {
        if (pressed & DECEPTUS_DOWN) {
            if (++game->selection >= 3) {
                game->selection = 0;
            }
        }
        if (pressed & DECEPTUS_UP) {
            game->selection = game->selection ? game->selection - 1 : 2;
        }
        if (pressed & (DECEPTUS_ACCEPT | DECEPTUS_PAUSE | DECEPTUS_JUMP)) {
            campaign->mode = game->selection == 0   ? DECEPTUS_CAMPAIGN_PLAYING
                             : game->selection == 1 ? DECEPTUS_MENU_OPTIONS
                                                    : DECEPTUS_MENU_EXIT;
            game->options_return = DECEPTUS_CAMPAIGN_TITLE;
            game->selection = 0;
        }
        engine->previous_buttons = buttons;
        return 0;
    }
    if (campaign->mode == DECEPTUS_MENU_EXIT) {
        if (pressed & (DECEPTUS_BACK | DECEPTUS_PAUSE | DECEPTUS_ACCEPT)) {
            campaign->mode = DECEPTUS_CAMPAIGN_TITLE;
            game->selection = 0;
        }
        return 0;
    }
    if (campaign->mode == DECEPTUS_MENU_OPTIONS) {
        if (pressed & (DECEPTUS_UP | DECEPTUS_DOWN)) {
            game->selection ^= 1;
        }
        if ((pressed & DECEPTUS_BACK) ||
            ((pressed & (DECEPTUS_ACCEPT | DECEPTUS_JUMP | DECEPTUS_PAUSE)) && game->selection)) {
            campaign->mode = game->options_return;
            game->selection = 0;
        } else if (pressed & (DECEPTUS_ACCEPT | DECEPTUS_JUMP | DECEPTUS_PAUSE)) {
            campaign->mode = DECEPTUS_MENU_CONTROLS;
            game->menu_return = DECEPTUS_MENU_OPTIONS;
        }
        engine->previous_buttons = buttons;
        return 0;
    }
    if (campaign->mode == DECEPTUS_MENU_CONFIRM) {
        if (pressed & (DECEPTUS_UP | DECEPTUS_DOWN)) {
            game->selection ^= 1;
        }
        if (pressed & DECEPTUS_BACK) {
            campaign->mode = DECEPTUS_MENU_PAUSE;
            game->selection = 0;
        } else if (pressed & (DECEPTUS_ACCEPT | DECEPTUS_JUMP | DECEPTUS_PAUSE)) {
            if (game->selection) {
                deceptus_adventure_init(game, game->definition, &campaign->video);
                game->campaign.previous_buttons = buttons;
                return 1;
            }
            campaign->mode = DECEPTUS_MENU_PAUSE;
            game->selection = 0;
        }
        engine->previous_buttons = buttons;
        return 0;
    }
    if (campaign->mode == DECEPTUS_NOTIFICATION && (pressed & (DECEPTUS_ACCEPT | DECEPTUS_JUMP | DECEPTUS_UP))) {
        if (++game->page >= game->dialogue->page_count) {
            campaign->mode = DECEPTUS_CAMPAIGN_PLAYING;
            game->dialogue = 0;
        }
    }
    if (campaign->mode == DECEPTUS_DIALOGUE) {
        if (pressed & (DECEPTUS_ACCEPT | DECEPTUS_JUMP | DECEPTUS_UP | DECEPTUS_PAUSE)) {
            if (++game->page >= game->dialogue->page_count) {
                campaign->mode = DECEPTUS_CAMPAIGN_PLAYING;
                game->dialogue = 0;
                engine->previous_buttons = buttons;
            }
        }
        return 0;
    }
    if (campaign->mode == DECEPTUS_MENU_CONTROLS || campaign->mode == DECEPTUS_MENU_INVENTORY) {
        if (campaign->mode == DECEPTUS_MENU_INVENTORY && (pressed & (DECEPTUS_UP | DECEPTUS_DOWN))) {
            for (index = 0; index < game->definition->item_count; ++index) {
                if (pressed & DECEPTUS_UP) {
                    game->selection = game->selection > 1 ? game->selection - 1 : game->definition->item_count;
                } else {
                    game->selection = game->selection < game->definition->item_count ? game->selection + 1 : 1;
                }
                if (game->inventory.counts[game->selection]) {
                    break;
                }
            }
            if (index == game->definition->item_count) {
                game->selection = 0;
            }
        }
        if (pressed & (DECEPTUS_BACK | DECEPTUS_RESTART | DECEPTUS_PAUSE | DECEPTUS_ACCEPT | DECEPTUS_JUMP)) {
            campaign->mode = game->menu_return;
            engine->previous_buttons = buttons;
        }
        return 0;
    }
    if (campaign->mode == DECEPTUS_MENU_PAUSE) {
        if (pressed & DECEPTUS_DOWN) {
            if (++game->selection >= 3) {
                game->selection = 0;
            }
        }
        if (pressed & DECEPTUS_UP) {
            game->selection = game->selection ? game->selection - 1 : 2;
        }
        if (pressed & DECEPTUS_BACK) {
            campaign->mode = DECEPTUS_CAMPAIGN_PLAYING;
        } else if (pressed & (DECEPTUS_ACCEPT | DECEPTUS_JUMP | DECEPTUS_PAUSE)) {
            if (game->selection == 0) {
                campaign->mode = DECEPTUS_CAMPAIGN_PLAYING;
            } else if (game->selection == 1) {
                campaign->mode = DECEPTUS_MENU_OPTIONS;
                game->options_return = DECEPTUS_MENU_PAUSE;
                game->selection = 0;
            } else {
                campaign->mode = DECEPTUS_MENU_CONFIRM;
                game->selection = 0;
            }
        }
        engine->previous_buttons = buttons;
        return 0;
    }
    if (pressed & DECEPTUS_PAUSE) {
        campaign->mode = DECEPTUS_MENU_PAUSE;
        game->selection = 0;
        return 0;
    }
    if (pressed & DECEPTUS_RESTART) {
        campaign->mode = DECEPTUS_MENU_INVENTORY;
        game->menu_return = DECEPTUS_CAMPAIGN_PLAYING;
        game->selection = 0;
        for (index = 1; index <= game->definition->item_count; ++index) {
            if (game->inventory.counts[index]) {
                game->selection = index;
                break;
            }
        }
        return 0;
    }
    room = &game->definition->rooms[campaign->room_index];
    if (game->room_label_ticks) {
        --game->room_label_ticks;
    }
    x = deceptus_floor(engine->entities[0].x);
    y = deceptus_floor(engine->entities[0].y);
    game->prompt = 255;
    for (index = 0; index < room->interaction_count; ++index) {
        action = &room->interactions[index];
        if (deceptus_interaction_available(game, action) && overlaps(&action->bounds, x, y)) {
            game->prompt = action->automatic ? 255 : index;
            if (action->automatic > 1) {
                if (!game->automatic_action) {
                    game->automatic_action = action;
                    game->automatic_ticks = action->automatic;
                }
                break;
            }
            if ((action->page_count || action->cost_item || action->grant_item || action->set_flags ||
                 action->effect) &&
                (action->automatic || (pressed & DECEPTUS_UP)) && interact(game, action)) {
                /* consuming interaction input prevents the newly fitted lever toggling on the same press. */
                engine->previous_buttons = buttons;
                return 0;
            }
            break;
        }
    }
    if (game->automatic_action && game->automatic_ticks) {
        --game->automatic_ticks;
    }
    deaths = engine->deaths;
    if (game->pending_ticks && !--game->pending_ticks) {
        if (game->pending_room == campaign->room_index) {
            enable_linked(&engine->mechanisms, game->pending_action->mechanism_id, 0);
        }
        game->pending_action = 0;
    }
    apply_bindings(game);
    deceptus_tick(engine, buttons & ~(DECEPTUS_PAUSE | DECEPTUS_RESTART));
    if (game->has_sword) {
        deceptus_playersword_tick(&game->sword, engine, pressed);
    }
    campaign->deaths += engine->deaths - deaths;
    if (game->automatic_action && !game->automatic_ticks) {
        action = game->automatic_action;
        game->automatic_action = 0;
        if (deceptus_interaction_available(game, action)) {
            interact(game, action);
        }
    }
    ++campaign->elapsed_ticks;
    if (++campaign->subsecond >= 60) {
        campaign->subsecond = 0;
        ++campaign->seconds;
    }
    if (game->portal_cooldown) {
        --game->portal_cooldown;
        return 0;
    }
    x = deceptus_floor(engine->entities[0].x);
    y = deceptus_floor(engine->entities[0].y);
    changed = 0;
    for (index = 0; index < room->portal_count; ++index) {
        portal = &room->portals[index];
        direction = (portal->direction == DECEPTUS_PORTAL_LEFT && (buttons & DECEPTUS_LEFT)) ||
                    (portal->direction == DECEPTUS_PORTAL_RIGHT && (buttons & DECEPTUS_RIGHT)) ||
                    (portal->direction == DECEPTUS_PORTAL_DOWN && engine->entities[0].velocity_y > 0) ||
                    portal->direction == DECEPTUS_PORTAL_TOUCH;
        if (!direction || !overlaps(&portal->bounds, x, y) ||
            ((portal->direction == DECEPTUS_PORTAL_LEFT || portal->direction == DECEPTUS_PORTAL_RIGHT) &&
             (y < portal->bounds.y || y > portal->bounds.y + portal->bounds.height))) {
            continue;
        }
        if (portal->destination >= game->definition->campaign->room_count) {
            engine->content_error = 8;
            return 0;
        }
        velocity_x = engine->entities[0].velocity_x;
        velocity_y = engine->entities[0].velocity_y;
        save_room(game);
        load_room(game, portal->destination, buttons);
        x += portal->offset_x;
        y += portal->offset_y;
        if (portal->direction == DECEPTUS_PORTAL_LEFT) {
            x = engine->room->columns * 8 - 20;
        } else if (portal->direction == DECEPTUS_PORTAL_RIGHT) {
            x = 20;
        } else {
            y = 32;
        }
        if (portal->has_arrival) {
            deceptus_place_player(engine, portal->arrival_x, portal->arrival_y);
        } else {
            deceptus_place_player(engine, x, y);
            engine->entities[0].velocity_x = velocity_x;
            engine->entities[0].velocity_y = velocity_y;
        }
        changed = 1;
        break;
    }
    return changed;
}
