#ifndef DECEPTUS_ADVENTURE_H
#define DECEPTUS_ADVENTURE_H
#include "deceptus/campaign.h"
#include "deceptus/playersword.h"

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
DeceptusU8 deceptusInventoryAdd(DeceptusInventory *inventory, DeceptusU8 item);
DeceptusU8 deceptusInventoryRemove(DeceptusInventory *inventory, DeceptusU8 item);

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
   const char *const *pages;
   DeceptusU16 required_flags, forbidden_flags, set_flags, mechanism_id;
   DeceptusU8 page_count, cost_item, grant_item, effect, automatic;
} DeceptusInteraction;

typedef struct DeceptusAdventureRoom
{
   const DeceptusPortal *portals;
   const DeceptusInteraction *interactions;
   DeceptusU8 portal_count, interaction_count;
   const struct DeceptusAdventureBinding *bindings;
   DeceptusU8 binding_count;
} DeceptusAdventureRoom;

/* source level callbacks derived from persistent inventory/treasure flags. */
typedef struct DeceptusAdventureBinding
{
   DeceptusU16 required_flags, forbidden_flags, mechanism_id;
   DeceptusU8 enabled;
} DeceptusAdventureBinding;

typedef struct DeceptusAdventureDef
{
   const DeceptusCampaignDef *campaign;
   const DeceptusAdventureRoom *rooms;
   const char *const *item_names;
   DeceptusU8 item_count;
} DeceptusAdventureDef;

typedef struct DeceptusAdventure
{
   DeceptusCampaign campaign;
   const DeceptusAdventureDef *definition;
   DeceptusInventory inventory;
   DeceptusPlayerSword sword;
   DeceptusMechanismState saved[DECEPTUS_ADVENTURE_ROOMS][DECEPTUS_MECHANISM_CAPACITY];
   DeceptusU8 visited[DECEPTUS_ADVENTURE_ROOMS];
   DeceptusU16 flags;
   const DeceptusInteraction *dialogue;
   const DeceptusInteraction *automatic_action;
   DeceptusU8 automatic_ticks;
   const DeceptusInteraction *pending_action;
   DeceptusU8 pending_room, pending_ticks, has_sword;
   DeceptusU8 page, selection, prompt, portal_cooldown, room_label_ticks, menu_return, options_return;
} DeceptusAdventure;

void deceptusAdventureInit(DeceptusAdventure *game, const DeceptusAdventureDef *definition, const DeceptusVideoCaps *video);
DeceptusU8 deceptusAdventureTick(DeceptusAdventure *game, DeceptusU16 buttons);
DeceptusU8 deceptusInteractionAvailable(const DeceptusAdventure *game, const DeceptusInteraction *interaction);
#endif
