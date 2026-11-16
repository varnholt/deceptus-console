#include "deceptus/adventure.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void testIronDoorKey(void)
{
   static DeceptusU8 cells[32 * 28];
   static const DeceptusMechanismDef door = {425, {128, 120, 24, 72}, 0, 0, 7, DECEPTUS_MECH_DOOR, 0, 0};
   static const DeceptusInteraction action = {425, {96, 120, 80, 72}, 0, 0, 0, 0, 425, 0, 7, 0, DECEPTUS_ACTION_DOOR, 0};
   static const char *const names[] = {"", "", "", "", "", "", "", "Red Key"};
   static const DeceptusCampaignGoal goal = {{0, 0, 1, 1}, 0};
   DeceptusRoom room;
   const DeceptusRoom *rooms[1];
   DeceptusAdventureRoom settings;
   DeceptusCampaignDef campaign;
   DeceptusAdventureDef definition;
   DeceptusAdventure game;
   DeceptusVideoCaps video = {240, 160, 24};
   DeceptusU16 index;
   memset(&room, 0, sizeof(room));
   memset(&settings, 0, sizeof(settings));
   for (index = 0; index < 32; ++index)
   {
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
   deceptusAdventureInit(&game, &definition, &video);
   deceptusAdventureTick(&game, DECEPTUS_PAUSE);
   deceptusAdventureTick(&game, 0);
   deceptusAdventureTick(&game, DECEPTUS_UP);
   assert(!game.campaign.engine.mechanisms.states[0].enabled);
   assert(game.campaign.engine.mechanisms.states[0].active);
   deceptusInventoryAdd(&game.inventory, 7);
   deceptusAdventureTick(&game, 0);
   deceptusAdventureTick(&game, DECEPTUS_UP);
   assert(game.campaign.engine.mechanisms.states[0].enabled && game.inventory.counts[7] == 1);
   for (index = 0; index < 10; ++index)
   {
      deceptusAdventureTick(&game, 0);
   }
   deceptusAdventureTick(&game, DECEPTUS_UP);
   assert(game.campaign.engine.mechanisms.states[0].enabled);
   for (index = 0; index < 58; ++index)
   {
      deceptusAdventureTick(&game, 0);
   }
   assert(!game.campaign.engine.mechanisms.states[0].active && game.inventory.counts[7] == 1);
   deceptusAdventureTick(&game, DECEPTUS_UP);
   assert(!game.campaign.engine.mechanisms.states[0].enabled);
   for (index = 0; index < 69; ++index)
   {
      deceptusAdventureTick(&game, 0);
   }
   assert(game.campaign.engine.mechanisms.states[0].active && game.inventory.counts[7] == 1);
}

static void testOwlEyesProgression(void)
{
   static DeceptusU8 cells[32 * 28];
   static DeceptusRoom rooms[2];
   static const DeceptusRoom *room_pointers[2];
   static const DeceptusCampaignGoal goals[2] = {{{0, 0, 1, 1}, 0}, {{0, 0, 1, 1}, 0}};
   static const char *const names[] = {"", "Rubies", "Sword"};
   static const DeceptusU16 targets[] = {1387, 1388, 1389};
   static const DeceptusMechanismDef cages[] = {{1386, {150, 100, 8, 40}, 0, 3, 0, DECEPTUS_MECH_BLOCK, 1, 0},
                                                {1387, {158, 100, 8, 40}, 0, 0, 0, DECEPTUS_MECH_BLOCK, 1, 0},
                                                {1388, {166, 100, 8, 40}, 0, 0, 0, DECEPTUS_MECH_BLOCK, 1, 0},
                                                {1389, {174, 100, 8, 40}, 0, 0, 0, DECEPTUS_MECH_BLOCK, 1, 0}};
   static const DeceptusInteraction shrine[] = {{1400, {80, 144, 40, 48}, 0, 0, 256, 256, 1386, 0, 1, 0, DECEPTUS_ACTION_OWL_EYES, 0},
                                                {1401, {120, 144, 40, 48}, 0, 256, 512, 512, 1386, 0, 0, 2, DECEPTUS_ACTION_SWORD, 1}};
   static const DeceptusInteraction ruby = {200, {32, 144, 32, 48}, 0, 0, 128, 128, 0, 0, 0, 1, 0, 0};
   static const DeceptusAdventureBinding binding = {256, 0, 1386, 0};
   /* authored entrances include feet exactly on the lower edge of their rectangle. */
   static const DeceptusPortal right = {{248, 144, 8, 48}, -256, 0, 1, DECEPTUS_PORTAL_RIGHT, 0, 0, 0};
   static const DeceptusPortal left = {{0, 144, 8, 48}, 256, 0, 0, DECEPTUS_PORTAL_LEFT, 0, 0, 0};
   static const DeceptusPortal touch = {{0, 160, 8, 20}, 256, 0, 0, DECEPTUS_PORTAL_TOUCH, 40, 192, 1};
   DeceptusAdventureRoom settings[2];
   DeceptusCampaignDef campaign;
   DeceptusAdventureDef definition;
   DeceptusAdventure game;
   DeceptusVideoCaps video = {240, 160, 24};
   DeceptusU16 index;
   memset(rooms, 0, sizeof(rooms));
   memset(settings, 0, sizeof(settings));
   for (index = 0; index < 32; ++index)
   {
      cells[24 * 32 + index] = DECEPTUS_SOLID;
   }
   for (index = 0; index < 2; ++index)
   {
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
   deceptusAdventureInit(&game, &definition, &video);
   deceptusAdventureTick(&game, DECEPTUS_PAUSE);
   deceptusAdventureTick(&game, 0);
   deceptusPlacePlayer(&game.campaign.engine, 100, 192);
   deceptusAdventureTick(&game, DECEPTUS_UP);
   assert(!game.flags && !game.pending_ticks);
   /* directional portals require both authored overlap and the matching input. */
   assert(!deceptusAdventureTick(&game, DECEPTUS_RIGHT));
   assert(game.campaign.room_index == 0);
   deceptusPlacePlayer(&game.campaign.engine, 245, 192);
   assert(!deceptusAdventureTick(&game, 0));
   assert(game.campaign.room_index == 0);
   assert(!deceptusAdventureTick(&game, DECEPTUS_LEFT));
   assert(game.campaign.room_index == 0);
   deceptusPlacePlayer(&game.campaign.engine, 245, 192);
   assert(deceptusAdventureTick(&game, DECEPTUS_RIGHT));
   for (index = 0; index < 12; ++index)
   {
      deceptusAdventureTick(&game, 0);
   }
   deceptusPlacePlayer(&game.campaign.engine, 40, 192);
   deceptusAdventureTick(&game, DECEPTUS_UP);
   assert(game.flags == 128 && game.inventory.counts[1] == 1);
   deceptusAdventureTick(&game, 0);
   deceptusAdventureTick(&game, DECEPTUS_UP);
   assert(game.inventory.counts[1] == 1);
   deceptusPlacePlayer(&game.campaign.engine, 10, 192);
   assert(deceptusAdventureTick(&game, DECEPTUS_LEFT));
   deceptusPlacePlayer(&game.campaign.engine, 100, 192);
   deceptusAdventureTick(&game, DECEPTUS_UP);
   assert(game.flags == 384 && !game.inventory.counts[1] && game.pending_ticks == 27);
   assert(!deceptusInteractionAvailable(&game, &shrine[1]));
   for (index = 0; index < 26; ++index)
   {
      deceptusAdventureTick(&game, 0);
      assert(game.campaign.engine.mechanisms.states[0].enabled);
   }
   deceptusAdventureTick(&game, 0);
   for (index = 0; index < 4; ++index)
   {
      assert(!game.campaign.engine.mechanisms.states[index].enabled);
   }
   deceptusPlacePlayer(&game.campaign.engine, 140, 192);
   deceptusAdventureTick(&game, 0);
   assert(game.has_sword && game.inventory.counts[2] == 1 && game.flags == 896);
   /* leave and return: rubies stay consumed, the cage stays open, sword cannot duplicate. */
   deceptusPlacePlayer(&game.campaign.engine, 245, 192);
   assert(deceptusAdventureTick(&game, DECEPTUS_RIGHT));
   for (index = 0; index < 12; ++index)
   {
      deceptusAdventureTick(&game, 0);
   }
   deceptusPlacePlayer(&game.campaign.engine, 10, 192);
   assert(deceptusAdventureTick(&game, DECEPTUS_LEFT));
   assert(!game.campaign.engine.mechanisms.states[0].enabled && game.has_sword);
   deceptusPlacePlayer(&game.campaign.engine, 140, 192);
   deceptusAdventureTick(&game, 0);
   assert(game.inventory.counts[2] == 1);
   deceptusAdventureTick(&game, DECEPTUS_RESTART);
   assert(game.selection == 2);
   deceptusAdventureTick(&game, 0);
   deceptusAdventureTick(&game, DECEPTUS_UP);
   assert(game.selection == 2);

   /* leaving during the release delay cannot reopen the cage early on return. */
   deceptusAdventureInit(&game, &definition, &video);
   deceptusAdventureTick(&game, DECEPTUS_PAUSE);
   deceptusAdventureTick(&game, 0);
   game.flags = 128;
   deceptusInventoryAdd(&game.inventory, 1);
   deceptusPlacePlayer(&game.campaign.engine, 100, 192);
   deceptusAdventureTick(&game, DECEPTUS_UP);
   deceptusPlacePlayer(&game.campaign.engine, 245, 192);
   assert(deceptusAdventureTick(&game, DECEPTUS_RIGHT));
   assert(game.pending_ticks == 26);
   for (index = 0; index < 12; ++index)
   {
      deceptusAdventureTick(&game, 0);
   }
   deceptusPlacePlayer(&game.campaign.engine, 10, 192);
   assert(deceptusAdventureTick(&game, DECEPTUS_LEFT));
   assert(game.pending_ticks == 13 && game.campaign.engine.mechanisms.states[0].enabled);
   for (index = 0; index < 13; ++index)
   {
      deceptusAdventureTick(&game, 0);
   }
   assert(!game.pending_ticks && !game.campaign.engine.mechanisms.states[0].enabled);

   /* an expired off-screen release is derived from the saved treasure on entry. */
   deceptusAdventureInit(&game, &definition, &video);
   deceptusAdventureTick(&game, DECEPTUS_PAUSE);
   deceptusAdventureTick(&game, 0);
   game.flags = 128;
   deceptusInventoryAdd(&game.inventory, 1);
   deceptusPlacePlayer(&game.campaign.engine, 100, 192);
   deceptusAdventureTick(&game, DECEPTUS_UP);
   deceptusPlacePlayer(&game.campaign.engine, 245, 192);
   assert(deceptusAdventureTick(&game, DECEPTUS_RIGHT));
   assert(game.pending_ticks && game.saved[0][0].enabled);
   for (index = 0; index < 30; ++index)
   {
      deceptusAdventureTick(&game, 0);
   }
   deceptusPlacePlayer(&game.campaign.engine, 10, 192);
   assert(deceptusAdventureTick(&game, DECEPTUS_LEFT));
   assert(!game.campaign.engine.mechanisms.states[0].enabled);
   settings[1].portals = &touch;
   for (index = 0; index < 12; ++index)
   {
      deceptusAdventureTick(&game, 0);
   }
   deceptusPlacePlayer(&game.campaign.engine, 245, 192);
   assert(deceptusAdventureTick(&game, DECEPTUS_RIGHT));
   for (index = 0; index < 12; ++index)
   {
      deceptusAdventureTick(&game, 0);
   }
   /* automatic source rectangles use body overlap even when the feet lie below them. */
   deceptusPlacePlayer(&game.campaign.engine, 10, 192);
   assert(deceptusAdventureTick(&game, 0));
   assert(game.campaign.room_index == 0);
}

int main(void)
{
   static DeceptusU8 cells[32 * 28];
   static DeceptusRoom rooms[2];
   static const DeceptusRoom *room_pointers[2];
   static const DeceptusCampaignGoal goals[2] = {{{0, 0, 1, 1}, 0}, {{0, 0, 1, 1}, 0}};
   static const char *const pages[] = {"Original dialogue.", "Second page."};
   static const char *const names[] = {"", "Key", "Handle"};
   static const DeceptusMechanismDef lever = {581, {144, 144, 48, 48}, 0, 0, 0, DECEPTUS_MECH_LEVER, 1, DECEPTUS_MECH_MISSING_HANDLE};
   static const DeceptusInteraction actions[] = {{1385, {24, 144, 32, 48}, pages, 0, 2, 2, 0, 2, 0, 1, 0, 0},
                                                 {1432, {88, 144, 32, 48}, pages, 0, 1, 1, 0, 2, 1, 2, 0, 0},
                                                 {581, {144, 144, 48, 48}, pages, 1, 8, 8, 581, 2, 2, 0, DECEPTUS_ACTION_HANDLE, 0},
                                                 {945, {200, 144, 32, 48}, pages, 0, 64, 64, 0, 2, 0, 0, DECEPTUS_ACTION_NOTIFICATION, 3}};
   static const DeceptusPortal right = {{248, 144, 8, 64}, -256, 0, 1, DECEPTUS_PORTAL_RIGHT, 0, 0, 0};
   static const DeceptusPortal left = {{0, 144, 8, 64}, 256, 0, 0, DECEPTUS_PORTAL_LEFT, 0, 0, 0};
   DeceptusAdventureRoom settings[2];
   DeceptusCampaignDef campaign;
   DeceptusAdventureDef definition;
   DeceptusAdventure game;
   DeceptusVideoCaps video = {240, 160, 24};
   DeceptusU16 index, tick;
   for (index = 0; index < 32; ++index)
   {
      cells[24 * 32 + index] = DECEPTUS_SOLID;
   }
   memset(rooms, 0, sizeof(rooms));
   memset(settings, 0, sizeof(settings));
   for (index = 0; index < 2; ++index)
   {
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
   deceptusAdventureInit(&game, &definition, &video);
   deceptusAdventureTick(&game, DECEPTUS_PAUSE);
   deceptusAdventureTick(&game, 0);
   /* an item cannot be used away from its interaction, or before acquiring it. */
   deceptusPlacePlayer(&game.campaign.engine, 100, 192);
   deceptusAdventureTick(&game, DECEPTUS_UP);
   assert(!game.flags && !game.inventory.counts[1]);
   deceptusAdventureTick(&game, 0);
   deceptusPlacePlayer(&game.campaign.engine, 40, 192);
   deceptusAdventureTick(&game, DECEPTUS_UP);
   assert(game.flags == 2 && game.inventory.counts[1] == 1 && game.campaign.mode == DECEPTUS_DIALOGUE);
   tick = game.campaign.engine.ticks;
   deceptusAdventureTick(&game, DECEPTUS_RIGHT);
   assert(game.campaign.engine.ticks == tick);
   deceptusAdventureTick(&game, DECEPTUS_ACCEPT);
   deceptusAdventureTick(&game, 0);
   deceptusAdventureTick(&game, DECEPTUS_ACCEPT);
   assert(game.campaign.mode == DECEPTUS_CAMPAIGN_PLAYING);
   deceptusAdventureTick(&game, 0);
   deceptusPlacePlayer(&game.campaign.engine, 100, 192);
   deceptusAdventureTick(&game, DECEPTUS_UP);
   assert(game.flags == 3 && !game.inventory.counts[1] && game.inventory.counts[2] == 1);
   deceptusAdventureTick(&game, DECEPTUS_ACCEPT);
   deceptusAdventureTick(&game, 0);
   deceptusAdventureTick(&game, DECEPTUS_ACCEPT);
   deceptusAdventureTick(&game, 0);
   deceptusPlacePlayer(&game.campaign.engine, 160, 192);
   deceptusAdventureTick(&game, DECEPTUS_UP);
   assert(game.flags == 11 && !game.inventory.counts[2]);
   assert(game.campaign.engine.mechanisms.states[0].handle_available);
   deceptusAdventureTick(&game, DECEPTUS_ACCEPT);
   deceptusAdventureTick(&game, 0);
   deceptusAdventureTick(&game, DECEPTUS_ACCEPT);
   deceptusAdventureTick(&game, 0);
   deceptusAdventureTick(&game, DECEPTUS_UP);
   assert(!game.campaign.engine.mechanisms.states[0].enabled);
   /* entering and returning must retain the fitted handle and operated lever. */
   deceptusPlacePlayer(&game.campaign.engine, 245, 192);
   assert(deceptusAdventureTick(&game, DECEPTUS_RIGHT));
   assert(game.campaign.room_index == 1);
   for (index = 0; index < 12; ++index)
   {
      deceptusAdventureTick(&game, 0);
   }
   deceptusPlacePlayer(&game.campaign.engine, 10, 192);
   assert(deceptusAdventureTick(&game, DECEPTUS_LEFT));
   assert(game.campaign.room_index == 0 && game.flags == 11);
   assert(game.campaign.engine.mechanisms.states[0].handle_available && !game.campaign.engine.mechanisms.states[0].enabled);
   deceptusAdventureTick(&game, DECEPTUS_RESTART);
   assert(game.campaign.mode == DECEPTUS_MENU_INVENTORY);
   deceptusAdventureTick(&game, 0);
   deceptusAdventureTick(&game, DECEPTUS_BACK);
   deceptusAdventureTick(&game, DECEPTUS_PAUSE);
   deceptusAdventureTick(&game, 0);
   tick = game.campaign.engine.ticks;
   deceptusAdventureTick(&game, DECEPTUS_RIGHT);
   assert(game.campaign.engine.ticks == tick);
   assert(!deceptusInventoryAdd(&game.inventory, 0));
   assert(!deceptusInventoryRemove(&game.inventory, 15));
   game.inventory.counts[1] = 255;
   assert(!deceptusInventoryAdd(&game.inventory, 1));
   deceptusAdventureInit(&game, &definition, &video);
   deceptusAdventureTick(&game, DECEPTUS_PAUSE);
   deceptusAdventureTick(&game, 0);
   deceptusPlacePlayer(&game.campaign.engine, 210, 192);
   for (index = 0; index < 4; ++index)
   {
      deceptusAdventureTick(&game, 0);
   }
   assert(game.campaign.mode == DECEPTUS_NOTIFICATION && (game.flags & 64));
   tick = game.campaign.engine.ticks;
   deceptusAdventureTick(&game, DECEPTUS_RIGHT);
   deceptusAdventureTick(&game, 0);
   assert(game.campaign.engine.ticks == tick + 2);
   deceptusAdventureTick(&game, DECEPTUS_ACCEPT);
   deceptusAdventureTick(&game, 0);
   deceptusAdventureTick(&game, DECEPTUS_ACCEPT);
   assert(game.campaign.mode == DECEPTUS_CAMPAIGN_PLAYING);
   testOwlEyesProgression();
   testIronDoorKey();
   puts("adventure item use, owl eyes/sword progression, menus and persistent room state checks passed");
   return 0;
}
