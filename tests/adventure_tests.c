#include "deceptus/adventure.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_iron_door_key(void)
{
    static deceptus_u8 cells[32 * 28];
    static const deceptus_mechanism_def door = {425, {128, 120, 24, 72}, 0, 0, 7, DECEPTUS_MECH_DOOR, 0, 0};
    static const deceptus_interaction action = {425, {96, 120, 80, 72},    0, 0, 0, 0, 425, 0, 7,
                                                0,   DECEPTUS_ACTION_DOOR, 0};
    static const char *const names[] = {"", "", "", "", "", "", "", "Red Key"};
    static const deceptus_campaign_goal goal = {{0, 0, 1, 1}, 0};
    deceptus_room room;
    const deceptus_room *rooms[1];
    deceptus_adventure_room settings;
    deceptus_campaign_def campaign;
    deceptus_adventure_def definition;
    deceptus_adventure game;
    deceptus_video_caps video = {240, 160, 24};
    deceptus_u16 index;
    memset(&room, 0, sizeof(room));
    memset(&settings, 0, sizeof(settings));
    for (index = 0; index < 32; ++index) {
        cells[24 * 32 + index] = DECEPTUS_SOLID;
    }
    room.collision = cells;
    room.columns = 32;
    room.rows = 28;
    room.spawn_x = 112;
    room.spawn_y = 192;
    room.patrol_x = -1;
    room.door_x = -1;
    room.mechanisms = &door;
    room.mechanism_count = 1;
    rooms[0] = &room;
    settings.interactions = &action;
    settings.interaction_count = 1;
    campaign.rooms = rooms;
    campaign.goals = &goal;
    campaign.room_count = 1;
    definition.campaign = &campaign;
    definition.rooms = &settings;
    definition.item_names = names;
    definition.item_count = 7;
    deceptus_adventure_init(&game, &definition, &video);
    deceptus_adventure_tick(&game, DECEPTUS_PAUSE);
    deceptus_adventure_tick(&game, 0);
    deceptus_adventure_tick(&game, DECEPTUS_UP);
    assert(!game.campaign.engine.mechanisms.states[0].enabled);
    assert(game.campaign.engine.mechanisms.states[0].active);
    deceptus_inventory_add(&game.inventory, 7);
    deceptus_adventure_tick(&game, 0);
    deceptus_adventure_tick(&game, DECEPTUS_UP);
    assert(game.campaign.engine.mechanisms.states[0].enabled && game.inventory.counts[7] == 1);
    for (index = 0; index < 10; ++index) {
        deceptus_adventure_tick(&game, 0);
    }
    deceptus_adventure_tick(&game, DECEPTUS_UP);
    assert(game.campaign.engine.mechanisms.states[0].enabled);
    for (index = 0; index < 58; ++index) {
        deceptus_adventure_tick(&game, 0);
    }
    assert(!game.campaign.engine.mechanisms.states[0].active && game.inventory.counts[7] == 1);
    deceptus_adventure_tick(&game, DECEPTUS_UP);
    assert(!game.campaign.engine.mechanisms.states[0].enabled);
    for (index = 0; index < 69; ++index) {
        deceptus_adventure_tick(&game, 0);
    }
    assert(game.campaign.engine.mechanisms.states[0].active && game.inventory.counts[7] == 1);
}

static void test_owl_eyes_progression(void)
{
    static deceptus_u8 cells[32 * 28];
    static deceptus_room rooms[2];
    static const deceptus_room *room_pointers[2];
    static const deceptus_campaign_goal goals[2] = {{{0, 0, 1, 1}, 0}, {{0, 0, 1, 1}, 0}};
    static const char *const names[] = {"", "Rubies", "Sword"};
    static const deceptus_u16 targets[] = {1387, 1388, 1389};
    static const deceptus_mechanism_def cages[] = {{1386, {150, 100, 8, 40}, 0, 3, 0, DECEPTUS_MECH_BLOCK, 1, 0},
                                                   {1387, {158, 100, 8, 40}, 0, 0, 0, DECEPTUS_MECH_BLOCK, 1, 0},
                                                   {1388, {166, 100, 8, 40}, 0, 0, 0, DECEPTUS_MECH_BLOCK, 1, 0},
                                                   {1389, {174, 100, 8, 40}, 0, 0, 0, DECEPTUS_MECH_BLOCK, 1, 0}};
    static const deceptus_interaction shrine[] = {
        {1400, {80, 144, 40, 48}, 0, 0, 256, 256, 1386, 0, 1, 0, DECEPTUS_ACTION_OWL_EYES, 0},
        {1401, {120, 144, 40, 48}, 0, 256, 512, 512, 1386, 0, 0, 2, DECEPTUS_ACTION_SWORD, 1}};
    static const deceptus_interaction ruby = {200, {32, 144, 32, 48}, 0, 0, 128, 128, 0, 0, 0, 1, 0, 0};
    static const deceptus_adventure_binding binding = {256, 0, 1386, 0};
    /* authored entrances include feet exactly on the lower edge of their rectangle. */
    static const deceptus_portal right = {{248, 144, 8, 48}, -256, 0, 1, DECEPTUS_PORTAL_RIGHT, 0, 0, 0};
    static const deceptus_portal left = {{0, 144, 8, 48}, 256, 0, 0, DECEPTUS_PORTAL_LEFT, 0, 0, 0};
    static const deceptus_portal touch = {{0, 160, 8, 20}, 256, 0, 0, DECEPTUS_PORTAL_TOUCH, 40, 192, 1};
    deceptus_adventure_room settings[2];
    deceptus_campaign_def campaign;
    deceptus_adventure_def definition;
    deceptus_adventure game;
    deceptus_video_caps video = {240, 160, 24};
    deceptus_u16 index;
    memset(rooms, 0, sizeof(rooms));
    memset(settings, 0, sizeof(settings));
    for (index = 0; index < 32; ++index) {
        cells[24 * 32 + index] = DECEPTUS_SOLID;
    }
    for (index = 0; index < 2; ++index) {
        rooms[index].collision = cells;
        rooms[index].columns = 32;
        rooms[index].rows = 28;
        rooms[index].spawn_x = 40;
        rooms[index].spawn_y = 192;
        rooms[index].patrol_x = -1;
        rooms[index].door_x = -1;
        room_pointers[index] = &rooms[index];
    }
    rooms[0].mechanisms = cages;
    rooms[0].mechanism_count = 4;
    rooms[0].mechanism_targets = targets;
    rooms[0].mechanism_target_count = 3;
    settings[0].portals = &right;
    settings[0].portal_count = 1;
    settings[0].interactions = shrine;
    settings[0].interaction_count = 2;
    settings[0].bindings = &binding;
    settings[0].binding_count = 1;
    settings[1].portals = &left;
    settings[1].portal_count = 1;
    settings[1].interactions = &ruby;
    settings[1].interaction_count = 1;
    campaign.rooms = room_pointers;
    campaign.goals = goals;
    campaign.room_count = 2;
    definition.campaign = &campaign;
    definition.rooms = settings;
    definition.item_names = names;
    definition.item_count = 2;
    deceptus_adventure_init(&game, &definition, &video);
    deceptus_adventure_tick(&game, DECEPTUS_PAUSE);
    deceptus_adventure_tick(&game, 0);
    deceptus_place_player(&game.campaign.engine, 100, 192);
    deceptus_adventure_tick(&game, DECEPTUS_UP);
    assert(!game.flags && !game.pending_ticks);
    deceptus_place_player(&game.campaign.engine, 245, 192);
    assert(deceptus_adventure_tick(&game, DECEPTUS_RIGHT));
    for (index = 0; index < 12; ++index) {
        deceptus_adventure_tick(&game, 0);
    }
    deceptus_place_player(&game.campaign.engine, 40, 192);
    deceptus_adventure_tick(&game, DECEPTUS_UP);
    assert(game.flags == 128 && game.inventory.counts[1] == 1);
    deceptus_adventure_tick(&game, 0);
    deceptus_adventure_tick(&game, DECEPTUS_UP);
    assert(game.inventory.counts[1] == 1);
    deceptus_place_player(&game.campaign.engine, 10, 192);
    assert(deceptus_adventure_tick(&game, DECEPTUS_LEFT));
    deceptus_place_player(&game.campaign.engine, 100, 192);
    deceptus_adventure_tick(&game, DECEPTUS_UP);
    assert(game.flags == 384 && !game.inventory.counts[1] && game.pending_ticks == 27);
    assert(!deceptus_interaction_available(&game, &shrine[1]));
    for (index = 0; index < 26; ++index) {
        deceptus_adventure_tick(&game, 0);
        assert(game.campaign.engine.mechanisms.states[0].enabled);
    }
    deceptus_adventure_tick(&game, 0);
    for (index = 0; index < 4; ++index) {
        assert(!game.campaign.engine.mechanisms.states[index].enabled);
    }
    deceptus_place_player(&game.campaign.engine, 140, 192);
    deceptus_adventure_tick(&game, 0);
    assert(game.has_sword && game.inventory.counts[2] == 1 && game.flags == 896);
    /* leave and return: rubies stay consumed, the cage stays open, sword cannot duplicate. */
    deceptus_place_player(&game.campaign.engine, 245, 192);
    assert(deceptus_adventure_tick(&game, DECEPTUS_RIGHT));
    for (index = 0; index < 12; ++index) {
        deceptus_adventure_tick(&game, 0);
    }
    deceptus_place_player(&game.campaign.engine, 10, 192);
    assert(deceptus_adventure_tick(&game, DECEPTUS_LEFT));
    assert(!game.campaign.engine.mechanisms.states[0].enabled && game.has_sword);
    deceptus_place_player(&game.campaign.engine, 140, 192);
    deceptus_adventure_tick(&game, 0);
    assert(game.inventory.counts[2] == 1);
    deceptus_adventure_tick(&game, DECEPTUS_RESTART);
    assert(game.selection == 2);
    deceptus_adventure_tick(&game, 0);
    deceptus_adventure_tick(&game, DECEPTUS_UP);
    assert(game.selection == 2);

    /* leaving during the release delay cannot reopen the cage early on return. */
    deceptus_adventure_init(&game, &definition, &video);
    deceptus_adventure_tick(&game, DECEPTUS_PAUSE);
    deceptus_adventure_tick(&game, 0);
    game.flags = 128;
    deceptus_inventory_add(&game.inventory, 1);
    deceptus_place_player(&game.campaign.engine, 100, 192);
    deceptus_adventure_tick(&game, DECEPTUS_UP);
    deceptus_place_player(&game.campaign.engine, 245, 192);
    assert(deceptus_adventure_tick(&game, DECEPTUS_RIGHT));
    assert(game.pending_ticks == 26);
    for (index = 0; index < 12; ++index) {
        deceptus_adventure_tick(&game, 0);
    }
    deceptus_place_player(&game.campaign.engine, 10, 192);
    assert(deceptus_adventure_tick(&game, DECEPTUS_LEFT));
    assert(game.pending_ticks == 13 && game.campaign.engine.mechanisms.states[0].enabled);
    for (index = 0; index < 13; ++index) {
        deceptus_adventure_tick(&game, 0);
    }
    assert(!game.pending_ticks && !game.campaign.engine.mechanisms.states[0].enabled);

    /* an expired off-screen release is derived from the saved treasure on entry. */
    deceptus_adventure_init(&game, &definition, &video);
    deceptus_adventure_tick(&game, DECEPTUS_PAUSE);
    deceptus_adventure_tick(&game, 0);
    game.flags = 128;
    deceptus_inventory_add(&game.inventory, 1);
    deceptus_place_player(&game.campaign.engine, 100, 192);
    deceptus_adventure_tick(&game, DECEPTUS_UP);
    deceptus_place_player(&game.campaign.engine, 245, 192);
    assert(deceptus_adventure_tick(&game, DECEPTUS_RIGHT));
    assert(game.pending_ticks && game.saved[0][0].enabled);
    for (index = 0; index < 30; ++index) {
        deceptus_adventure_tick(&game, 0);
    }
    deceptus_place_player(&game.campaign.engine, 10, 192);
    assert(deceptus_adventure_tick(&game, DECEPTUS_LEFT));
    assert(!game.campaign.engine.mechanisms.states[0].enabled);
    settings[1].portals = &touch;
    for (index = 0; index < 12; ++index) {
        deceptus_adventure_tick(&game, 0);
    }
    deceptus_place_player(&game.campaign.engine, 245, 192);
    assert(deceptus_adventure_tick(&game, DECEPTUS_RIGHT));
    for (index = 0; index < 12; ++index) {
        deceptus_adventure_tick(&game, 0);
    }
    /* automatic source rectangles use body overlap even when the feet lie below them. */
    deceptus_place_player(&game.campaign.engine, 10, 192);
    assert(deceptus_adventure_tick(&game, 0));
    assert(game.campaign.room_index == 0);
}

int main(void)
{
    static deceptus_u8 cells[32 * 28];
    static deceptus_room rooms[2];
    static const deceptus_room *room_pointers[2];
    static const deceptus_campaign_goal goals[2] = {{{0, 0, 1, 1}, 0}, {{0, 0, 1, 1}, 0}};
    static const char *const pages[] = {"Original dialogue.", "Second page."};
    static const char *const names[] = {"", "Key", "Handle"};
    static const deceptus_mechanism_def lever = {581, {144, 144, 48, 48},  0, 0,
                                                 0,   DECEPTUS_MECH_LEVER, 1, DECEPTUS_MECH_MISSING_HANDLE};
    static const deceptus_interaction actions[] = {
        {1385, {24, 144, 32, 48}, pages, 0, 2, 2, 0, 2, 0, 1, 0, 0},
        {1432, {88, 144, 32, 48}, pages, 0, 1, 1, 0, 2, 1, 2, 0, 0},
        {581, {144, 144, 48, 48}, pages, 1, 8, 8, 581, 2, 2, 0, DECEPTUS_ACTION_HANDLE, 0},
        {945, {200, 144, 32, 48}, pages, 0, 64, 64, 0, 2, 0, 0, DECEPTUS_ACTION_NOTIFICATION, 3}};
    static const deceptus_portal right = {{248, 144, 8, 64}, -256, 0, 1, DECEPTUS_PORTAL_RIGHT, 0, 0, 0};
    static const deceptus_portal left = {{0, 144, 8, 64}, 256, 0, 0, DECEPTUS_PORTAL_LEFT, 0, 0, 0};
    deceptus_adventure_room settings[2];
    deceptus_campaign_def campaign;
    deceptus_adventure_def definition;
    deceptus_adventure game;
    deceptus_video_caps video = {240, 160, 24};
    deceptus_u16 index, tick;
    for (index = 0; index < 32; ++index) {
        cells[24 * 32 + index] = DECEPTUS_SOLID;
    }
    memset(rooms, 0, sizeof(rooms));
    memset(settings, 0, sizeof(settings));
    for (index = 0; index < 2; ++index) {
        rooms[index].collision = cells;
        rooms[index].columns = 32;
        rooms[index].rows = 28;
        rooms[index].spawn_x = 40;
        rooms[index].spawn_y = 192;
        rooms[index].patrol_x = -1;
        rooms[index].door_x = -1;
        rooms[index].max_health = 100;
        room_pointers[index] = &rooms[index];
    }
    rooms[0].mechanisms = &lever;
    rooms[0].mechanism_count = 1;
    settings[0].portals = &right;
    settings[0].portal_count = 1;
    settings[0].interactions = actions;
    settings[0].interaction_count = 4;
    settings[1].portals = &left;
    settings[1].portal_count = 1;
    settings[1].interactions = 0;
    settings[1].interaction_count = 0;
    campaign.rooms = room_pointers;
    campaign.goals = goals;
    campaign.room_count = 2;
    definition.campaign = &campaign;
    definition.rooms = settings;
    definition.item_names = names;
    definition.item_count = 2;
    deceptus_adventure_init(&game, &definition, &video);
    deceptus_adventure_tick(&game, DECEPTUS_PAUSE);
    deceptus_adventure_tick(&game, 0);
    /* an item cannot be used away from its interaction, or before acquiring it. */
    deceptus_place_player(&game.campaign.engine, 100, 192);
    deceptus_adventure_tick(&game, DECEPTUS_UP);
    assert(!game.flags && !game.inventory.counts[1]);
    deceptus_adventure_tick(&game, 0);
    deceptus_place_player(&game.campaign.engine, 40, 192);
    deceptus_adventure_tick(&game, DECEPTUS_UP);
    assert(game.flags == 2 && game.inventory.counts[1] == 1 && game.campaign.mode == DECEPTUS_DIALOGUE);
    tick = game.campaign.engine.ticks;
    deceptus_adventure_tick(&game, DECEPTUS_RIGHT);
    assert(game.campaign.engine.ticks == tick);
    deceptus_adventure_tick(&game, DECEPTUS_ACCEPT);
    deceptus_adventure_tick(&game, 0);
    deceptus_adventure_tick(&game, DECEPTUS_ACCEPT);
    assert(game.campaign.mode == DECEPTUS_CAMPAIGN_PLAYING);
    deceptus_adventure_tick(&game, 0);
    deceptus_place_player(&game.campaign.engine, 100, 192);
    deceptus_adventure_tick(&game, DECEPTUS_UP);
    assert(game.flags == 3 && !game.inventory.counts[1] && game.inventory.counts[2] == 1);
    deceptus_adventure_tick(&game, DECEPTUS_ACCEPT);
    deceptus_adventure_tick(&game, 0);
    deceptus_adventure_tick(&game, DECEPTUS_ACCEPT);
    deceptus_adventure_tick(&game, 0);
    deceptus_place_player(&game.campaign.engine, 160, 192);
    deceptus_adventure_tick(&game, DECEPTUS_UP);
    assert(game.flags == 11 && !game.inventory.counts[2]);
    assert(game.campaign.engine.mechanisms.states[0].handle_available);
    deceptus_adventure_tick(&game, DECEPTUS_ACCEPT);
    deceptus_adventure_tick(&game, 0);
    deceptus_adventure_tick(&game, DECEPTUS_ACCEPT);
    deceptus_adventure_tick(&game, 0);
    deceptus_adventure_tick(&game, DECEPTUS_UP);
    assert(!game.campaign.engine.mechanisms.states[0].enabled);
    /* entering and returning must retain the fitted handle and operated lever. */
    deceptus_place_player(&game.campaign.engine, 245, 192);
    assert(deceptus_adventure_tick(&game, DECEPTUS_RIGHT));
    assert(game.campaign.room_index == 1);
    for (index = 0; index < 12; ++index) {
        deceptus_adventure_tick(&game, 0);
    }
    deceptus_place_player(&game.campaign.engine, 10, 192);
    assert(deceptus_adventure_tick(&game, DECEPTUS_LEFT));
    assert(game.campaign.room_index == 0 && game.flags == 11);
    assert(game.campaign.engine.mechanisms.states[0].handle_available &&
           !game.campaign.engine.mechanisms.states[0].enabled);
    deceptus_adventure_tick(&game, DECEPTUS_RESTART);
    assert(game.campaign.mode == DECEPTUS_MENU_INVENTORY);
    deceptus_adventure_tick(&game, 0);
    deceptus_adventure_tick(&game, DECEPTUS_BACK);
    deceptus_adventure_tick(&game, DECEPTUS_PAUSE);
    deceptus_adventure_tick(&game, 0);
    tick = game.campaign.engine.ticks;
    deceptus_adventure_tick(&game, DECEPTUS_RIGHT);
    assert(game.campaign.engine.ticks == tick);
    assert(!deceptus_inventory_add(&game.inventory, 0));
    assert(!deceptus_inventory_remove(&game.inventory, 15));
    game.inventory.counts[1] = 255;
    assert(!deceptus_inventory_add(&game.inventory, 1));
    deceptus_adventure_init(&game, &definition, &video);
    deceptus_adventure_tick(&game, DECEPTUS_PAUSE);
    deceptus_adventure_tick(&game, 0);
    deceptus_place_player(&game.campaign.engine, 210, 192);
    for (index = 0; index < 4; ++index) {
        deceptus_adventure_tick(&game, 0);
    }
    assert(game.campaign.mode == DECEPTUS_NOTIFICATION && (game.flags & 64));
    tick = game.campaign.engine.ticks;
    deceptus_adventure_tick(&game, DECEPTUS_RIGHT);
    deceptus_adventure_tick(&game, 0);
    assert(game.campaign.engine.ticks == tick + 2);
    deceptus_adventure_tick(&game, DECEPTUS_ACCEPT);
    deceptus_adventure_tick(&game, 0);
    deceptus_adventure_tick(&game, DECEPTUS_ACCEPT);
    assert(game.campaign.mode == DECEPTUS_CAMPAIGN_PLAYING);
    test_owl_eyes_progression();
    test_iron_door_key();
    puts("adventure item use, owl eyes/sword progression, menus and persistent room state checks passed");
    return 0;
}
