#include "rooms.h"
#include "amiga_opening_assets.h"
#include "deceptus/roomcontent.h"
#include "keyboard.h"
#include "opening_video.h"
#include "system.h"

#define CHIP __attribute__((section(".MEMF_CHIP")))

static DeceptusU8 resource_normal[AMIGA_RESOURCE_NORMAL_BYTES];
static DeceptusU16 resource_chip[AMIGA_RESOURCE_CHIP_BYTES / 2] CHIP;
static DeceptusU8 simulation[AMIGA_ROOM_BYTES];
static DeceptusMechanismDef mechanisms[AMIGA_ROOM_MECHANISMS];
static DeceptusMechanismTiming timings[AMIGA_ROOM_MECHANISMS];
static DeceptusU16 targets[AMIGA_ROOM_TARGETS];
static DeceptusEnemyDef enemies[AMIGA_ROOM_ENEMIES];
static DeceptusPathPoint paths[AMIGA_ROOM_PATHS];
static DeceptusRoomContent content = {
   {0}, {0}, mechanisms, timings, targets, enemies, paths, AMIGA_ROOM_MECHANISMS, AMIGA_ROOM_TARGETS, AMIGA_ROOM_ENEMIES, AMIGA_ROOM_PATHS};
static AmigaGraphics graphics;
static DeceptusU16 room_loads;
static DeceptusU8 sword_graphics;
static DeceptusU32 tile_cache_offset;

const AmigaGraphics *amigaRoomGraphics(void)
{
   return &graphics;
}

DeceptusU16 amigaRoomLoads(void)
{
   return room_loads;
}

DeceptusU8 *amigaRoomScratch(DeceptusU32 *size)
{
   *size = sizeof(resource_normal) - tile_cache_offset;
   return resource_normal + tile_cache_offset;
}

static void roomPath(char *destination, const char *prefix, DeceptusU8 room, const char *suffix, DeceptusU8 two_digits)
{
   while (*prefix)
   {
      *destination++ = *prefix++;
   }
   if (room >= 10 || two_digits)
   {
      *destination++ = (char)('0' + room / 10);
   }
   *destination++ = (char)('0' + room % 10);
   while (*suffix)
   {
      *destination++ = *suffix++;
   }
   *destination = 0;
}

DeceptusU8 amigaRoomSwordGraphics(void)
{
   return sword_graphics;
}

DeceptusU8 amigaRoomLoadGraphics(DeceptusU8 room, DeceptusU8 sword)
{
   char path[64];
   DeceptusU8 *world;
   DeceptusU32 world_size, sprite_size, split;
   if (room >= AMIGA_OPENING_ROOMS)
   {
      return 0;
   }
   amigaOpeningVideoSuspend();
   amigaKeyboardReset();
   world = (DeceptusU8 *)resource_chip;
   roomPath(path, "Build:assets/area", room, sword ? "/graphics-sword.agp" : "/graphics.agp", 0);
   world_size = amigaSystemReadFile(path, world, sizeof(resource_chip));
   if (!world_size || world_size >= sizeof(resource_chip))
   {
      return 0;
   }
   split = amigaGraphicsSpriteSplit(world, world_size, sizeof(resource_chip) - world_size);
   roomPath(path, "Build:assets/area", room, sword ? "/graphics-sword.asp" : "/graphics.asp", 0);
   sprite_size = amigaSystemReadFilePair(path, world + world_size, split, resource_normal, sizeof(resource_normal));
   if (!sprite_size || !amigaGraphicsInit(&graphics, world, world_size, world + world_size, sprite_size, split, resource_normal))
   {
      return 0;
   }
   tile_cache_offset = (sprite_size - split + 3) & ~3UL;
   if (tile_cache_offset > sizeof(resource_normal))
   {
      return 0;
   }
   sword_graphics = sword;
   amigaOpeningVideoReset();
   return 1;
}

const DeceptusRoom *amigaRoomLoad(void *context, DeceptusU8 room)
{
   const DeceptusAdventure *game;
   char path[64];
   DeceptusU32 size;
   game = (const DeceptusAdventure *)context;
   if (room >= AMIGA_OPENING_ROOMS)
   {
      return 0;
   }
   amigaOpeningVideoSuspend();
   roomPath(path, "Build:assets/rooms/room", room, ".drm", 1);
   size = amigaSystemReadFile(path, simulation, sizeof(simulation));
   if (!deceptusRoomContentLoad(&content, simulation, size) || !amigaRoomLoadGraphics(room, game && game->has_sword))
   {
      return 0;
   }
   ++room_loads;
   return &content.room;
}
