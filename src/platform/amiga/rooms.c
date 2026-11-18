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
static DeceptusU32 chip_cache_offset;

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

DeceptusU8 *amigaRoomChipScratch(DeceptusU32 *size)
{
   *size = sizeof(resource_chip) - chip_cache_offset;
   return (DeceptusU8 *)resource_chip + chip_cache_offset;
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

static DeceptusU8 *reserveChipScratch(DeceptusU32 bytes)
{
   DeceptusU32 minimum, chip_free, normal_free;
   DeceptusU8 *pixels;
   minimum = ((DeceptusU32)graphics.pattern_count + 15) / 16 + 3 + AMIGA_TILE_GROUP_BYTES;
   chip_free = sizeof(resource_chip) - chip_cache_offset;
   normal_free = sizeof(resource_normal) - tile_cache_offset;
   if (bytes > chip_free || (normal_free < minimum && chip_free - bytes < minimum))
   {
      return 0;
   }
   pixels = (DeceptusU8 *)resource_chip + chip_cache_offset;
   chip_cache_offset += bytes;
   return pixels;
}

static DeceptusU8 loadNativeBackground(DeceptusU8 room, DeceptusU8 sword)
{
   DeceptusU32 bytes;
   DeceptusU8 *pixels;
   char path[64];
   bytes = (DeceptusU32)((graphics.width + 15) / 16) * 2 * graphics.height * 5;
   pixels = reserveChipScratch(bytes);
   if (!pixels)
   {
      return 1;
   }
   roomPath(path, "Build:assets/area", room, sword ? "/graphics-sword.abg" : "/graphics.abg", 0);
   if (amigaSystemReadFile(path, pixels, bytes) != bytes)
   {
      return 0;
   }
   graphics.background_pixels = (const DeceptusU16 *)pixels;
   return 1;
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
   chip_cache_offset = (world_size + split + 3) & ~3UL;
   if (tile_cache_offset > sizeof(resource_normal))
   {
      return 0;
   }
   graphics.viewport_width = AMIGA_LARGE_CACHE_WIDTH;
   graphics.viewport_height = AMIGA_LARGE_CACHE_HEIGHT;
   graphics.viewport_pixels =
      (DeceptusU16 *)reserveChipScratch((DeceptusU32)(graphics.viewport_width / 8) * graphics.viewport_height * 5 * 2);
   if (!graphics.viewport_pixels)
   {
      graphics.viewport_width = AMIGA_WIDE_CACHE_WIDTH;
      graphics.viewport_height = AMIGA_WIDE_CACHE_HEIGHT;
      graphics.viewport_pixels =
         (DeceptusU16 *)reserveChipScratch((DeceptusU32)(graphics.viewport_width / 8) * graphics.viewport_height * 5 * 2);
   }
   if (graphics.viewport_pixels && !graphics.clip_count)
   {
      graphics.foreground_pixels = (DeceptusU16 *)reserveChipScratch((DeceptusU32)(graphics.viewport_width / 8) * graphics.viewport_height);
   }
   if (!graphics.viewport_pixels)
   {
      graphics.viewport_width = graphics.viewport_height = 0;
      if (!loadNativeBackground(room, sword))
      {
         return 0;
      }
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
