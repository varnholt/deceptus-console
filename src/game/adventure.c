#include "deceptus/adventure.h"
#include <string.h>
#include "deceptus/game.h"

DeceptusU8 deceptusInventoryAdd(DeceptusInventory* inventory, DeceptusU8 item)
{
   if (!item || item >= DECEPTUS_INVENTORY_ITEMS || inventory->counts[item] == 255)
   {
      return 0;
   }
   ++inventory->counts[item];
   return 1;
}

DeceptusU8 deceptusInventoryRemove(DeceptusInventory* inventory, DeceptusU8 item)
{
   if ((item == 0) || (item >= DECEPTUS_INVENTORY_ITEMS) || (inventory->counts[item] == 0))
   {
      return 0;
   }
   --inventory->counts[item];
   return 1;
}

DeceptusU16 deceptusAdventurePresentationFlags(const DeceptusAdventure* game)
{
   /* the original owl ward controls the altar ring independently of inventory flags. */
   const DeceptusU16 sword_ward_source_id = 1386;
   const DeceptusU16 sword_ward_visible = 8192;
   DeceptusI16 ward;
   DeceptusU16 flags;
   flags = game->flags;
   ward = deceptusMechanismFind(&game->campaign.engine.mechanisms, sword_ward_source_id);
   if (ward >= 0 && game->campaign.engine.mechanisms.states[ward].enabled)
   {
      flags |= sword_ward_visible;
   }
   return flags;
}

static DeceptusU8 overlaps(const DeceptusRect* bounds, DeceptusI16 x, DeceptusI16 y)
{
   return x + 8 > bounds->x && x - 8 < bounds->x + bounds->width && y > bounds->y && y - 30 < bounds->y + bounds->height;
}

DeceptusU8 deceptusInteractionAvailable(const DeceptusAdventure* game, const DeceptusInteraction* interaction)
{
   DeceptusI16 slot;
   if (interaction->effect == DECEPTUS_ACTION_EXAMINE_BLOCKER)
   {
      slot = deceptusMechanismFind(&game->campaign.engine.mechanisms, interaction->mechanism_id);
      if (slot < 0)
      {
         return 0;
      }
      if (game->campaign.engine.mechanisms.definitions[slot].kind == DECEPTUS_MECH_DOOR)
      {
         if (game->campaign.engine.mechanisms.states[slot].enabled)
         {
            return 0;
         }
      }
      else if (!game->campaign.engine.mechanisms.states[slot].active || game->campaign.engine.mechanisms.states[slot].triggered)
      {
         return 0;
      }
   }
   if (interaction->effect == DECEPTUS_ACTION_SWORD && interaction->mechanism_id)
   {
      slot = deceptusMechanismFind(&game->campaign.engine.mechanisms, interaction->mechanism_id);
      if (slot < 0 || game->campaign.engine.mechanisms.states[slot].enabled)
      {
         return 0;
      }
   }
   return (game->flags & interaction->required_flags) == interaction->required_flags && !(game->flags & interaction->forbidden_flags) &&
          (!interaction->cost_item || (interaction->cost_item < DECEPTUS_INVENTORY_ITEMS && game->inventory.counts[interaction->cost_item])
          );
}

static const DeceptusRoom* requestRoom(DeceptusAdventure* game, DeceptusU8 room)
{
   if (game->room_provider.load)
   {
      return game->room_provider.load(game->room_provider.context, room);
   }
   return game->definition->campaign->rooms[room];
}

void deceptusAdventureInit(DeceptusAdventure* game, const DeceptusAdventureDef* definition, const DeceptusVideoCaps* video)
{
   deceptusAdventureInitWithRoomProvider(game, definition, video, 0);
}

void deceptusAdventureInitWithRoomProvider(
   DeceptusAdventure* game,
   const DeceptusAdventureDef* definition,
   const DeceptusVideoCaps* video,
   const DeceptusRoomProvider* provider
)
{
   DeceptusVideoCaps saved_video;
   DeceptusRoomProvider saved_provider;
   const DeceptusRoom* room;
   DeceptusU8 index;
   saved_video = *video;
   saved_provider.load = 0;
   saved_provider.context = 0;
   if (provider)
   {
      saved_provider = *provider;
   }
   memset(game, 0, sizeof(*game));
   game->definition = definition;
   game->room_provider = saved_provider;
   game->prompt = 255;
   game->room_label_ticks = 120;
   if ((definition == 0) || (definition->campaign == 0) || (definition->rooms == 0) ||
       ((saved_provider.load == 0) && (definition->campaign->rooms == 0)) || (definition->campaign->room_count == 0) ||
       (definition->campaign->room_count > DECEPTUS_ADVENTURE_ROOMS) || (definition->item_count >= DECEPTUS_INVENTORY_ITEMS))
   {
      game->campaign.engine.content_error = 8;
      return;
   }
   game->campaign.definition = definition->campaign;
   game->campaign.video = saved_video;
   for (index = 0; index < definition->campaign->room_count; ++index)
   {
      if ((!saved_provider.load && !definition->campaign->rooms[index]) ||
          (definition->rooms[index].binding_count && !definition->rooms[index].bindings) ||
          (definition->rooms[index].portal_count && !definition->rooms[index].portals) ||
          (definition->rooms[index].interaction_count && !definition->rooms[index].interactions))
      {
         game->campaign.engine.content_error = 8;
         return;
      }
   }
   room = requestRoom(game, 0);
   if (!room)
   {
      game->campaign.engine.content_error = 8;
      return;
   }
   deceptusInit(&game->campaign.engine, room, &saved_video);
   deceptusTick(&game->campaign.engine, 0);
}

static void saveRoom(DeceptusAdventure* game)
{
   DeceptusU8 room;
   room = game->campaign.room_index;
   memcpy(game->saved[room], game->campaign.engine.mechanisms.states, sizeof(game->saved[room]));
   game->visited[room] = 1;
}

static DeceptusU8 linkedValid(const DeceptusMechanismSet* set, DeceptusU16 source_id)
{
   return deceptusMechanismFind(set, source_id) >= 0;
}

static void enableLinked(DeceptusMechanismSet* set, DeceptusU16 source_id, DeceptusU8 enabled)
{
   const DeceptusMechanismDef* definition;
   DeceptusI16 slot;
   DeceptusU16 target;
   slot = deceptusMechanismFind(set, source_id);
   if (slot < 0)
   {
      return;
   }
   definition = &set->definitions[slot];
   deceptusMechanismEnable(set, source_id, enabled);
   for (target = 0; target < definition->target_count; ++target)
   {
      deceptusMechanismEnable(set, set->targets[definition->target_first + target], enabled);
   }
}

static void applyBindings(DeceptusAdventure* game)
{
   const DeceptusAdventureRoom* room;
   const DeceptusAdventureBinding* binding;
   DeceptusU8 index;
   room = &game->definition->rooms[game->campaign.room_index];
   for (index = 0; index < room->binding_count; ++index)
   {
      binding = &room->bindings[index];
      if ((game->flags & binding->required_flags) != binding->required_flags || (game->flags & binding->forbidden_flags))
      {
         continue;
      }
      /* the eyes appear immediately; the source ward releases 0.45 seconds later. */
      if (game->pending_ticks && game->pending_room == game->campaign.room_index &&
          game->pending_action->mechanism_id == binding->mechanism_id)
      {
         continue;
      }
      if (!linkedValid(&game->campaign.engine.mechanisms, binding->mechanism_id))
      {
         game->campaign.engine.content_error = 8;
         return;
      }
      enableLinked(&game->campaign.engine.mechanisms, binding->mechanism_id, binding->enabled);
   }
}

static DeceptusU8 loadRoom(DeceptusAdventure* game, DeceptusU8 room, DeceptusU16 buttons)
{
   DeceptusEngine* engine;
   const DeceptusRoom* definition;
   DeceptusU8 health;
   engine = &game->campaign.engine;
   health = engine->health;
   definition = requestRoom(game, room);
   if (!definition)
   {
      engine->content_error = 8;
      return 0;
   }
   game->campaign.room_index = room;
   deceptusInit(engine, definition, &game->campaign.video);
   deceptusPlayerSwordInit(&game->sword);
   engine->health = health;
   if (game->visited[room])
   {
      memcpy(engine->mechanisms.states, game->saved[room], sizeof(game->saved[room]));
   }
   applyBindings(game);
   engine->previous_buttons = buttons;
   game->automatic_action = 0;
   game->automatic_ticks = 0;
   game->portal_cooldown = 12;
   game->room_label_ticks = 120;
   game->prompt = 255;
   return !engine->content_error;
}

static DeceptusU8 interact(DeceptusAdventure* game, const DeceptusInteraction* action)
{
   DeceptusI16 slot;
   DeceptusU8 event;
   slot = -1;
   if (action->effect == DECEPTUS_ACTION_HANDLE)
   {
      slot = deceptusMechanismFind(&game->campaign.engine.mechanisms, action->mechanism_id);
      if (slot < 0 || game->campaign.engine.mechanisms.definitions[slot].kind != DECEPTUS_MECH_LEVER)
      {
         return 0;
      }
   }
   if (action->effect == DECEPTUS_ACTION_DOOR)
   {
      slot = deceptusMechanismFind(&game->campaign.engine.mechanisms, action->mechanism_id);
      if (slot < 0 || game->campaign.engine.mechanisms.definitions[slot].kind != DECEPTUS_MECH_DOOR ||
          game->campaign.engine.mechanisms.states[slot].cooldown)
      {
         return 0;
      }
   }
   if ((action->effect == DECEPTUS_ACTION_DISABLE_LINKED || action->effect == DECEPTUS_ACTION_OWL_EYES) &&
       (!linkedValid(&game->campaign.engine.mechanisms, action->mechanism_id) ||
        (action->effect == DECEPTUS_ACTION_OWL_EYES && game->pending_ticks)))
   {
      return 0;
   }
   if (action->grant_item && (action->grant_item >= DECEPTUS_INVENTORY_ITEMS || game->inventory.counts[action->grant_item] == 255))
   {
      return 0;
   }
   if (action->cost_item && action->effect != DECEPTUS_ACTION_DOOR && !deceptusInventoryRemove(&game->inventory, action->cost_item))
   {
      return 0;
   }
   if (action->grant_item)
   {
      deceptusInventoryAdd(&game->inventory, action->grant_item);
   }
   if (slot >= 0 && action->effect == DECEPTUS_ACTION_HANDLE)
   {
      game->campaign.engine.mechanisms.states[slot].handle_available = 1;
   }
   game->flags |= action->set_flags;
   if (action->effect == DECEPTUS_ACTION_DISABLE_LINKED)
   {
      enableLinked(&game->campaign.engine.mechanisms, action->mechanism_id, 0);
   }
   else if (action->effect == DECEPTUS_ACTION_OWL_EYES)
   {
      game->pending_action = action;
      game->pending_room = game->campaign.room_index;
      game->pending_ticks = 27;
   }
   else if (action->effect == DECEPTUS_ACTION_SWORD)
   {
      game->has_sword = 1;
   }
   else if (action->effect == DECEPTUS_ACTION_DOOR)
   {
      deceptusMechanismEnable(
         &game->campaign.engine.mechanisms, action->mechanism_id, !game->campaign.engine.mechanisms.states[slot].enabled
      );
   }
   applyBindings(game);
   event = action->effect == DECEPTUS_ACTION_DOOR              ? DECEPTUS_EVENT_DOOR
           : action->effect == DECEPTUS_ACTION_EXAMINE_BLOCKER ? DECEPTUS_EVENT_DIALOGUE
           : action->grant_item                                ? DECEPTUS_EVENT_ITEM
           : action->effect                                    ? DECEPTUS_EVENT_LEVER_INSERT
           : action->cost_item                                 ? DECEPTUS_EVENT_DOOR
                                                               : DECEPTUS_EVENT_DIALOGUE;
   deceptusEventPush(&game->campaign.engine, event);
   /* the engine clears its events when its next tick starts, before the queue would read this one. */
   soundQueueEvent(&game->sounds, event, game->definition->rooms[game->campaign.room_index].footsteps);
   if (action->page_count)
   {
      game->dialogue = action;
      game->page = 0;
      game->campaign.mode = (action->effect & DECEPTUS_ACTION_NOTIFICATION) ? DECEPTUS_NOTIFICATION : DECEPTUS_DIALOGUE;
   }
   return 1;
}

/* the room's thunderstorm runs while its flags are all set. */
static void stormTick(DeceptusAdventure* game, const DeceptusAdventureRoom* room)
{
   DeceptusU8 active;
   active = 0;
   if (room->storm_flags != 0)
   {
      if ((game->flags & room->storm_flags) == room->storm_flags)
      {
         active = 1;
      }
   }
   thunderstormTick(&game->thunderstorm, active);
}

/* the menus the original plays its menu track in: the main menu and the pause menu with their
 * screens. the inventory is shown over the game, which keeps its track. */
static DeceptusU8 isMusicMenu(DeceptusU8 mode)
{
   return (mode == DECEPTUS_CAMPAIGN_TITLE) || (mode == DECEPTUS_MENU_PAUSE) || (mode == DECEPTUS_MENU_CONTROLS) ||
          (mode == DECEPTUS_MENU_EXIT) || (mode == DECEPTUS_MENU_OPTIONS) || (mode == DECEPTUS_MENU_CONFIRM);
}

DeceptusU8 deceptusAdventureTick(DeceptusAdventure* game, DeceptusU16 buttons)
{
   DeceptusCampaign* campaign;
   DeceptusEngine* engine;
   const DeceptusAdventureRoom* room;
   const DeceptusInteraction* action;
   const DeceptusPortal* portal;
   DeceptusU16 pressed, deaths;
   DeceptusI16 x, y;
   DeceptusFixed velocity_x, velocity_y;
   DeceptusU8 index, changed;
   campaign = &game->campaign;
   engine = &campaign->engine;
   engine->event_count = 0;
   engine->event_overflow = 0;
   pressed = buttons & ~campaign->previous_buttons;
   campaign->previous_buttons = buttons;
   if (engine->content_error)
   {
      return 0;
   }
   if (isMusicMenu(campaign->mode))
   {
      deceptusMusicShowMenu(&game->music, game->definition->menu_music);
   }
   if (campaign->mode == DECEPTUS_CAMPAIGN_TITLE)
   {
      if (pressed & DECEPTUS_DOWN)
      {
         if (++game->selection >= 3)
         {
            game->selection = 0;
         }
      }
      if (pressed & DECEPTUS_UP)
      {
         game->selection = game->selection ? game->selection - 1 : 2;
      }
      if (pressed & (DECEPTUS_ACCEPT | DECEPTUS_PAUSE | DECEPTUS_JUMP))
      {
         campaign->mode = game->selection == 0   ? DECEPTUS_CAMPAIGN_PLAYING
                          : game->selection == 1 ? DECEPTUS_MENU_OPTIONS
                                                 : DECEPTUS_MENU_EXIT;
         game->options_return = DECEPTUS_CAMPAIGN_TITLE;
         game->selection = 0;
      }
      engine->previous_buttons = buttons;
      return 0;
   }
   if (campaign->mode == DECEPTUS_MENU_EXIT)
   {
      if (pressed & (DECEPTUS_BACK | DECEPTUS_PAUSE | DECEPTUS_ACCEPT))
      {
         campaign->mode = DECEPTUS_CAMPAIGN_TITLE;
         game->selection = 0;
      }
      return 0;
   }
   if (campaign->mode == DECEPTUS_MENU_OPTIONS)
   {
      if (pressed & (DECEPTUS_UP | DECEPTUS_DOWN))
      {
         game->selection ^= 1;
      }
      if ((pressed & DECEPTUS_BACK) || ((pressed & (DECEPTUS_ACCEPT | DECEPTUS_JUMP | DECEPTUS_PAUSE)) && game->selection))
      {
         campaign->mode = game->options_return;
         game->selection = 0;
      }
      else if (pressed & (DECEPTUS_ACCEPT | DECEPTUS_JUMP | DECEPTUS_PAUSE))
      {
         campaign->mode = DECEPTUS_MENU_CONTROLS;
         game->menu_return = DECEPTUS_MENU_OPTIONS;
      }
      engine->previous_buttons = buttons;
      return 0;
   }
   if (campaign->mode == DECEPTUS_MENU_CONFIRM)
   {
      if (pressed & (DECEPTUS_UP | DECEPTUS_DOWN))
      {
         game->selection ^= 1;
      }
      if (pressed & DECEPTUS_BACK)
      {
         campaign->mode = DECEPTUS_MENU_PAUSE;
         game->selection = 0;
      }
      else if (pressed & (DECEPTUS_ACCEPT | DECEPTUS_JUMP | DECEPTUS_PAUSE))
      {
         if (game->selection)
         {
            deceptusAdventureInitWithRoomProvider(game, game->definition, &campaign->video, &game->room_provider);
            game->campaign.previous_buttons = buttons;
            return 1;
         }
         campaign->mode = DECEPTUS_MENU_PAUSE;
         game->selection = 0;
      }
      engine->previous_buttons = buttons;
      return 0;
   }
   if (campaign->mode == DECEPTUS_NOTIFICATION && (pressed & (DECEPTUS_ACCEPT | DECEPTUS_JUMP | DECEPTUS_UP)))
   {
      if (++game->page >= game->dialogue->page_count)
      {
         campaign->mode = DECEPTUS_CAMPAIGN_PLAYING;
         game->dialogue = 0;
      }
   }
   if (campaign->mode == DECEPTUS_DIALOGUE)
   {
      if (pressed & (DECEPTUS_ACCEPT | DECEPTUS_JUMP | DECEPTUS_UP | DECEPTUS_PAUSE))
      {
         if (++game->page >= game->dialogue->page_count)
         {
            campaign->mode = DECEPTUS_CAMPAIGN_PLAYING;
            game->dialogue = 0;
            engine->previous_buttons = buttons;
         }
      }
      return 0;
   }
   if (campaign->mode == DECEPTUS_MENU_CONTROLS || campaign->mode == DECEPTUS_MENU_INVENTORY)
   {
      if (campaign->mode == DECEPTUS_MENU_INVENTORY && (pressed & (DECEPTUS_UP | DECEPTUS_DOWN)))
      {
         for (index = 0; index < game->definition->item_count; ++index)
         {
            if (pressed & DECEPTUS_UP)
            {
               game->selection = game->selection > 1 ? game->selection - 1 : game->definition->item_count;
            }
            else
            {
               game->selection = game->selection < game->definition->item_count ? game->selection + 1 : 1;
            }
            if (game->inventory.counts[game->selection])
            {
               break;
            }
         }
         if (index == game->definition->item_count)
         {
            game->selection = 0;
         }
      }
      if (pressed & (DECEPTUS_BACK | DECEPTUS_RESTART | DECEPTUS_PAUSE | DECEPTUS_ACCEPT | DECEPTUS_JUMP))
      {
         campaign->mode = game->menu_return;
         engine->previous_buttons = buttons;
      }
      return 0;
   }
   if (campaign->mode == DECEPTUS_MENU_PAUSE)
   {
      if (pressed & DECEPTUS_DOWN)
      {
         if (++game->selection >= 3)
         {
            game->selection = 0;
         }
      }
      if (pressed & DECEPTUS_UP)
      {
         game->selection = game->selection ? game->selection - 1 : 2;
      }
      if (pressed & DECEPTUS_BACK)
      {
         campaign->mode = DECEPTUS_CAMPAIGN_PLAYING;
      }
      else if (pressed & (DECEPTUS_ACCEPT | DECEPTUS_JUMP | DECEPTUS_PAUSE))
      {
         if (game->selection == 0)
         {
            campaign->mode = DECEPTUS_CAMPAIGN_PLAYING;
         }
         else if (game->selection == 1)
         {
            campaign->mode = DECEPTUS_MENU_OPTIONS;
            game->options_return = DECEPTUS_MENU_PAUSE;
            game->selection = 0;
         }
         else
         {
            campaign->mode = DECEPTUS_MENU_CONFIRM;
            game->selection = 0;
         }
      }
      engine->previous_buttons = buttons;
      return 0;
   }
   if (pressed & DECEPTUS_PAUSE)
   {
      campaign->mode = DECEPTUS_MENU_PAUSE;
      game->selection = 0;
      return 0;
   }
   if (pressed & DECEPTUS_RESTART)
   {
      campaign->mode = DECEPTUS_MENU_INVENTORY;
      game->menu_return = DECEPTUS_CAMPAIGN_PLAYING;
      game->selection = 0;
      for (index = 1; index <= game->definition->item_count; ++index)
      {
         if (game->inventory.counts[index])
         {
            game->selection = index;
            break;
         }
      }
      return 0;
   }
   room = &game->definition->rooms[campaign->room_index];
   if (game->room_label_ticks)
   {
      --game->room_label_ticks;
   }
   x = deceptusFloor(engine->entities[0].x);
   y = deceptusFloor(engine->entities[0].y);
   game->prompt = 255;
   for (index = 0; index < room->interaction_count; ++index)
   {
      action = &room->interactions[index];
      if (deceptusInteractionAvailable(game, action) && overlaps(&action->bounds, x, y))
      {
         game->prompt = action->automatic ? 255 : index;
         if (action->automatic > 1)
         {
            if (!game->automatic_action)
            {
               game->automatic_action = action;
               game->automatic_ticks = action->automatic;
            }
            break;
         }
         if ((action->page_count || action->cost_item || action->grant_item || action->set_flags || action->effect) &&
             (action->automatic || (pressed & DECEPTUS_UP)) && interact(game, action))
         {
            /* consuming interaction input prevents the newly fitted lever toggling on the same press. */
            engine->previous_buttons = buttons;
            return 0;
         }
         break;
      }
   }
   if (game->automatic_action && game->automatic_ticks)
   {
      --game->automatic_ticks;
   }
   deaths = engine->deaths;
   if (game->pending_ticks && !--game->pending_ticks)
   {
      if (game->pending_room == campaign->room_index)
      {
         enableLinked(&engine->mechanisms, game->pending_action->mechanism_id, 0);
      }
      game->pending_action = 0;
   }
   applyBindings(game);
   if ((game->automatic_action != 0) && game->automatic_action->locks_controls)
   {
      /* the source locks the controls while a delayed action is pending (lockPlayerControls during the
       * sinkhole's tremble in level-graveyard/level.lua), so the player stays where the ground gives way. */
      buttons = 0;
      pressed = 0;
   }
   deceptusTick(engine, buttons & ~(DECEPTUS_PAUSE | DECEPTUS_RESTART));
   stormTick(game, room);
   soundQueueEngineEvents(&game->sounds, engine, room->footsteps);
   deceptusMusicTick(&game->music, engine, room->music, room->music_zone, engine->deaths != deaths);
   if (game->has_sword)
   {
      deceptusPlayerSwordTick(&game->sword, engine, pressed);
   }
   campaign->deaths += engine->deaths - deaths;
   if (game->automatic_action && !game->automatic_ticks)
   {
      action = game->automatic_action;
      game->automatic_action = 0;
      if (deceptusInteractionAvailable(game, action))
      {
         interact(game, action);
      }
   }
   ++campaign->elapsed_ticks;
   if (++campaign->subsecond >= 60)
   {
      campaign->subsecond = 0;
      ++campaign->seconds;
   }
   if (game->portal_cooldown)
   {
      --game->portal_cooldown;
      return 0;
   }
   x = deceptusFloor(engine->entities[0].x);
   y = deceptusFloor(engine->entities[0].y);
   changed = 0;
   for (index = 0; index < room->portal_count; ++index)
   {
      portal = &room->portals[index];
      /* separate guards preserve short-circuit behavior on the 65816 compiler. */
      if (portal->direction == DECEPTUS_PORTAL_LEFT)
      {
         if (!(buttons & DECEPTUS_LEFT))
         {
            continue;
         }
         if (y < portal->bounds.y)
         {
            continue;
         }
         if (y > portal->bounds.y + portal->bounds.height)
         {
            continue;
         }
      }
      else if (portal->direction == DECEPTUS_PORTAL_RIGHT)
      {
         if (!(buttons & DECEPTUS_RIGHT))
         {
            continue;
         }
         if (y < portal->bounds.y)
         {
            continue;
         }
         if (y > portal->bounds.y + portal->bounds.height)
         {
            continue;
         }
      }
      else if (portal->direction == DECEPTUS_PORTAL_DOWN)
      {
         if (engine->entities[0].velocity_y <= 0)
         {
            continue;
         }
      }
      else if (portal->direction != DECEPTUS_PORTAL_TOUCH)
      {
         continue;
      }
      if (!overlaps(&portal->bounds, x, y))
      {
         continue;
      }
      if (portal->destination >= game->definition->campaign->room_count)
      {
         engine->content_error = 8;
         return 0;
      }
      velocity_x = engine->entities[0].velocity_x;
      velocity_y = engine->entities[0].velocity_y;
      saveRoom(game);
      if (!loadRoom(game, portal->destination, buttons))
      {
         return 0;
      }
      x += portal->offset_x;
      y += portal->offset_y;
      if (portal->direction == DECEPTUS_PORTAL_LEFT)
      {
         x = engine->room->columns * 8 - 20;
      }
      else if (portal->direction == DECEPTUS_PORTAL_RIGHT)
      {
         x = 20;
      }
      else
      {
         y = 32;
      }
      if (portal->has_arrival)
      {
         deceptusPlacePlayer(engine, portal->arrival_x, portal->arrival_y);
      }
      else
      {
         deceptusPlacePlayer(engine, x, y);
         engine->entities[0].velocity_x = velocity_x;
         engine->entities[0].velocity_y = velocity_y;
      }
      changed = 1;
      break;
   }
   return changed;
}
