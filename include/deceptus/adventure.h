#ifndef DECEPTUS_ADVENTURE_H
#define DECEPTUS_ADVENTURE_H
#include "deceptus/campaign.h"
#include "deceptus/playersword.h"
#include "deceptus/soundcue.h"
#include "deceptus/thunderstorm.h"

#define DECEPTUS_ADVENTURE_ROOMS 32
#define DECEPTUS_INVENTORY_ITEMS 16
#define DECEPTUS_DIALOGUE 4
#define DECEPTUS_MENU_PAUSE 5
#define DECEPTUS_MENU_INVENTORY 6
#define DECEPTUS_MENU_CONTROLS 7
#define DECEPTUS_MENU_EXIT 8
#define DECEPTUS_MENU_OPTIONS 9
#define DECEPTUS_MENU_CONFIRM 10
#define DECEPTUS_NOTIFICATION 11
#define DECEPTUS_PORTAL_LEFT 1
#define DECEPTUS_PORTAL_RIGHT 2
#define DECEPTUS_PORTAL_DOWN 3
#define DECEPTUS_PORTAL_TOUCH 4
#define DECEPTUS_ACTION_HANDLE 1
#define DECEPTUS_ACTION_DISABLE_LINKED 2
#define DECEPTUS_ACTION_SWORD 3
#define DECEPTUS_ACTION_OWL_EYES 4
#define DECEPTUS_ACTION_DOOR 5
#define DECEPTUS_ACTION_EXAMINE_BLOCKER 6
#define DECEPTUS_ACTION_NOTIFICATION 128

typedef struct DeceptusInventory
{
   DeceptusU8 counts[DECEPTUS_INVENTORY_ITEMS];
} DeceptusInventory;
DeceptusU8 deceptusInventoryAdd(DeceptusInventory* inventory, DeceptusU8 item);
DeceptusU8 deceptusInventoryRemove(DeceptusInventory* inventory, DeceptusU8 item);

typedef struct DeceptusPortal
{
   DeceptusRect bounds;
   DeceptusI16 offset_x, offset_y;
   DeceptusU8 destination, direction;
   DeceptusI16 arrival_x, arrival_y;
   DeceptusU8 has_arrival;
} DeceptusPortal;

typedef struct DeceptusInteraction
{
   DeceptusU16 source_id;
   DeceptusRect bounds;
   const char* const* pages;
   DeceptusU16 required_flags, forbidden_flags, set_flags, mechanism_id;
   DeceptusU8 page_count, cost_item, grant_item, effect, automatic;
   /* nonzero: the player's controls stay locked while this automatic action counts down. */
   DeceptusU8 locks_controls;
} DeceptusInteraction;

typedef struct DeceptusAdventureRoom
{
   const DeceptusPortal* portals;
   const DeceptusInteraction* interactions;
   DeceptusU8 portal_count, interaction_count;
   const struct DeceptusAdventureBinding* bindings;
   DeceptusU8 binding_count;
   /* the flags that turn the room's thunderstorm on; 0 for a room without one. */
   DeceptusU16 storm_flags;
   /* the floor its footsteps sound on: SOUND_SURFACE_STONE or SOUND_SURFACE_GRASS. */
   DeceptusU8 footsteps;
} DeceptusAdventureRoom;

/* source level callbacks derived from persistent inventory/treasure flags. */
typedef struct DeceptusAdventureBinding
{
   DeceptusU16 required_flags, forbidden_flags, mechanism_id;
   DeceptusU8 enabled;
} DeceptusAdventureBinding;

typedef struct DeceptusAdventureDef
{
   const DeceptusCampaignDef* campaign;
   const DeceptusAdventureRoom* rooms;
   const char* const* item_names;
   DeceptusU8 item_count;
} DeceptusAdventureDef;

/* returned room storage remains valid until the next load; null reports a load failure. */
typedef struct DeceptusRoomProvider
{
   const DeceptusRoom* (*load)(void* context, DeceptusU8 room);
   void* context;
} DeceptusRoomProvider;

typedef struct DeceptusAdventure
{
   DeceptusCampaign campaign;
   DeceptusRoomProvider room_provider;
   const DeceptusAdventureDef* definition;
   DeceptusInventory inventory;
   DeceptusPlayerSword sword;
   DeceptusMechanismState saved[DECEPTUS_ADVENTURE_ROOMS][DECEPTUS_MECHANISM_CAPACITY];
   DeceptusU8 visited[DECEPTUS_ADVENTURE_ROOMS];
   DeceptusU16 flags;
   Thunderstorm thunderstorm;
   /* the sounds of the ticks since the platform last emptied it. */
   SoundQueue sounds;
   const DeceptusInteraction* dialogue;
   const DeceptusInteraction* automatic_action;
   DeceptusU8 automatic_ticks;
   const DeceptusInteraction* pending_action;
   DeceptusU8 pending_room, pending_ticks, has_sword;
   DeceptusU8 page, selection, prompt, portal_cooldown, room_label_ticks, menu_return, options_return;
} DeceptusAdventure;

void deceptusAdventureInit(DeceptusAdventure* game, const DeceptusAdventureDef* definition, const DeceptusVideoCaps* video);
void deceptusAdventureInitWithRoomProvider(
   DeceptusAdventure* game,
   const DeceptusAdventureDef* definition,
   const DeceptusVideoCaps* video,
   const DeceptusRoomProvider* provider
);
DeceptusU8 deceptusAdventureTick(DeceptusAdventure* game, DeceptusU16 buttons);
DeceptusU16 deceptusAdventurePresentationFlags(const DeceptusAdventure* game);
DeceptusU8 deceptusInteractionAvailable(const DeceptusAdventure* game, const DeceptusInteraction* interaction);
#endif
