#include "rooms.h"
#include "amiga_opening_assets.h"
#include "deceptus/roomcontent.h"
#include "keyboard.h"
#include "opening_video.h"
#include "system.h"
#include <string.h>

#define CHIP __attribute__((section(".MEMF_CHIP")))

static DeceptusU8 resource_normal[AMIGA_RESOURCE_NORMAL_BYTES] __attribute__((aligned(4)));
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

static DeceptusU8 loadNativeComposite(DeceptusU8 room, DeceptusU8 sword)
{
   DeceptusU32 plane_bytes, bytes;
   DeceptusU8 *pixels;
   char path[64];
   if (graphics.clip_count)
   {
      return 1;
   }
   plane_bytes = (DeceptusU32)((graphics.width + 15) / 16) * 2 * graphics.height;
   bytes = plane_bytes * 6;
   pixels = reserveChipScratch(bytes);
   if (!pixels)
   {
      return 1;
   }
   roomPath(path, "Build:assets/area", room, sword ? "/graphics-sword.acp" : "/graphics.acp", 0);
   if (amigaSystemReadFile(path, pixels, bytes) != bytes)
   {
      return 0;
   }
   graphics.background_pixels = (const DeceptusU16 *)pixels;
   graphics.native_foreground = (const DeceptusU16 *)(pixels + plane_bytes * 5);
   return 1;
}

static DeceptusU8 loadRasterColumns(DeceptusU8 room, DeceptusU8 sword)
{
   DeceptusU32 bytes, reserved, minimum, chip_free;
   DeceptusU8 *pixels, in_chip;
   char path[64];
   bytes = amiga_raster_sizes[room][sword != 0];
   if (!bytes || graphics.native_foreground)
   {
      return 1;
   }
   reserved = ((bytes + 3) & ~3UL) + AMIGA_RASTER_BYTES;
   minimum = ((DeceptusU32)graphics.pattern_count + 15) / 16 + 3 + AMIGA_TILE_GROUP_BYTES;
   chip_free = sizeof(resource_chip) - chip_cache_offset;
   in_chip = 0;
   if (reserved <= sizeof(resource_normal) - tile_cache_offset &&
       (sizeof(resource_normal) - tile_cache_offset - reserved >= minimum || chip_free >= minimum))
   {
      pixels = resource_normal + tile_cache_offset;
   }
   else if (chip_free >= reserved + (DeceptusU32)(AMIGA_BASE_CACHE_WIDTH / 8) * AMIGA_BASE_CACHE_HEIGHT * 11 + 16UL * 1024)
   {
      pixels = (DeceptusU8 *)resource_chip + chip_cache_offset;
      in_chip = 1;
   }
   else
   {
      return 1;
   }
   roomPath(path, "Build:assets/area", room, sword ? "/graphics-sword.acr" : "/graphics.acr", 0);
   if (amigaSystemReadFile(path, pixels, bytes) != bytes || !amigaGraphicsSetRaster(&graphics, pixels, bytes))
   {
      return 0;
   }
   if (in_chip)
   {
      chip_cache_offset += reserved;
   }
   else
   {
      tile_cache_offset += reserved;
   }
   graphics.raster_scratch = (DeceptusU16 *)(pixels + ((bytes + 3) & ~3UL));
   return 1;
}

static DeceptusU8 reserveWindow(DeceptusU16 width, DeceptusU16 height)
{
   DeceptusU32 checkpoint, mask_bytes, tile_bytes, smallest_window, normal_free;
   checkpoint = chip_cache_offset;
   tile_bytes = ((DeceptusU32)graphics.pattern_count + 15) / 16;
   if (graphics.raster && tile_bytes > AMIGA_RASTER_TILE_GROUPS)
   {
      tile_bytes = AMIGA_RASTER_TILE_GROUPS;
   }
   tile_bytes = tile_bytes <= AMIGA_TILE_CACHE_GROUPS ? ((tile_bytes + 3) & ~3UL) + tile_bytes * AMIGA_TILE_GROUP_BYTES : 0;
   smallest_window = (DeceptusU32)(AMIGA_BASE_CACHE_WIDTH / 8) * AMIGA_BASE_CACHE_HEIGHT * 11;
   normal_free = sizeof(resource_normal) - tile_cache_offset;
   mask_bytes = (DeceptusU32)(width / 8) * height;
   graphics.viewport_pixels = (DeceptusU16 *)reserveChipScratch(mask_bytes * 10);
   graphics.foreground_pixels = graphics.viewport_pixels ? (DeceptusU16 *)reserveChipScratch(mask_bytes) : 0;
   if (!graphics.foreground_pixels || sizeof(resource_chip) - chip_cache_offset < 16UL * 1024 ||
       (tile_bytes && sizeof(resource_chip) - checkpoint >= smallest_window + tile_bytes &&
        sizeof(resource_chip) - chip_cache_offset < tile_bytes) ||
       (tile_bytes && sizeof(resource_chip) - checkpoint + normal_free >= smallest_window + tile_bytes + AMIGA_DECODED_SPRITE_BYTES &&
        sizeof(resource_chip) - chip_cache_offset + normal_free < tile_bytes + AMIGA_DECODED_SPRITE_BYTES))
   {
      chip_cache_offset = checkpoint;
      graphics.viewport_pixels = 0;
      graphics.foreground_pixels = 0;
      return 0;
   }
   graphics.viewport_width = width;
   graphics.viewport_height = height;
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
   if (!sprite_size)
   {
      return 0;
   }
   if (split == sprite_size && sprite_size <= sizeof(resource_normal))
   {
      /* compressed sprites need CPU access; leave Chip RAM for native graphics. */
      memcpy(resource_normal, world + world_size, sprite_size);
      split = 0;
   }
   if (!amigaGraphicsInit(&graphics, world, world_size, world + world_size, sprite_size, split, resource_normal))
   {
      return 0;
   }
   tile_cache_offset = (sprite_size - split + 3) & ~3UL;
   chip_cache_offset = (world_size + split + 3) & ~3UL;
   if (tile_cache_offset > sizeof(resource_normal))
   {
      return 0;
   }
   if (!loadNativeComposite(room, sword) || !loadRasterColumns(room, sword))
   {
      return 0;
   }
   if (!graphics.native_foreground && !reserveWindow(AMIGA_LARGE_CACHE_WIDTH, AMIGA_LARGE_CACHE_HEIGHT) &&
       !reserveWindow(AMIGA_WIDE_CACHE_WIDTH, AMIGA_WIDE_CACHE_HEIGHT) && !reserveWindow(AMIGA_TALL_CACHE_WIDTH, AMIGA_TALL_CACHE_HEIGHT) &&
       !reserveWindow(AMIGA_SMALL_CACHE_WIDTH, AMIGA_SMALL_CACHE_HEIGHT) && !reserveWindow(AMIGA_BASE_CACHE_WIDTH, AMIGA_BASE_CACHE_HEIGHT))
   {
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
   }
   if (!graphics.viewport_pixels)
   {
      graphics.viewport_width = graphics.viewport_height = 0;
      if (!graphics.native_foreground && !loadNativeBackground(room, sword))
      {
         return 0;
      }
   }
   if (!graphics.foreground_pixels)
   {
      graphics.raster = 0;
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
