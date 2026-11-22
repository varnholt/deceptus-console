#include "rooms.h"
#include "amiga_native_assets.h"
#include "amiga_opening_assets.h"
#include "deceptus/roomcontent.h"
#include "keyboard.h"
#include "opening_video.h"
#include "system.h"
#include <string.h>

#define CHIP __attribute__((section(".MEMF_CHIP")))
/* for the debugger: the banks actually allocated (see system.c). */
extern volatile DeceptusU32 amiga_memory_debug[4];

/* the resource banks come from free memory at startup: at least the converter's budget, more when the
   machine has it. a static Chip RAM array this large forces one huge contiguous load hunk. */
#define RESERVE_CHIP_BYTES (16UL * 1024)
#define RESERVE_OTHER_BYTES (24UL * 1024)
static DeceptusU8 *resource_normal;
static DeceptusU16 *resource_chip;
static DeceptusU32 resource_normal_bytes, resource_chip_bytes;
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
static AmigaTileArchive native_tiles;
static const DeceptusU8 *tile_animations;
static DeceptusU32 tile_animation_bytes;
static const DeceptusU8 *message_boxes;
static DeceptusU32 message_box_bytes;

static DeceptusU32 bankSize(DeceptusU8 chip, DeceptusU32 minimum, DeceptusU32 reserve)
{
   DeceptusU32 largest;
   largest = amigaSystemLargest(chip) & ~3UL;
   return largest >= minimum + reserve ? largest - reserve : minimum;
}

DeceptusU8 amigaRoomsAllocate(void)
{
   resource_normal_bytes = bankSize(0, AMIGA_RESOURCE_NORMAL_BYTES, RESERVE_OTHER_BYTES);
   resource_normal = (DeceptusU8 *)amigaSystemAllocate(resource_normal_bytes, 0);
   if (!resource_normal)
   {
      amigaSystemReportMemory("Deceptus room data", resource_normal_bytes, 0);
      return 0;
   }
   resource_chip_bytes = bankSize(1, AMIGA_RESOURCE_CHIP_BYTES, RESERVE_CHIP_BYTES);
   resource_chip = (DeceptusU16 *)amigaSystemAllocate(resource_chip_bytes, 1);
   if (!resource_chip)
   {
      amigaSystemReportMemory("Deceptus room graphics", resource_chip_bytes, 1);
      amigaSystemReportMemory("Deceptus room data (allocated)", resource_normal_bytes, 0);
      amigaRoomsRelease();
      return 0;
   }
   amiga_memory_debug[2] = resource_normal_bytes;
   amiga_memory_debug[3] = resource_chip_bytes;
   return 1;
}

void amigaRoomsRelease(void)
{
   amigaSystemFree(resource_chip, resource_chip ? resource_chip_bytes : 0);
   amigaSystemFree(resource_normal, resource_normal ? resource_normal_bytes : 0);
   resource_chip = 0;
   resource_normal = 0;
}

DeceptusU32 amigaRoomBankBytes(DeceptusU8 chip)
{
   return chip ? resource_chip_bytes : resource_normal_bytes;
}

const AmigaTileArchive *amigaRoomTiles(void)
{
   return &native_tiles;
}

const DeceptusU8 *amigaRoomMessageBoxes(DeceptusU32 *size)
{
   *size = message_box_bytes;
   return message_boxes;
}

const DeceptusU8 *amigaRoomTileAnimations(DeceptusU32 *size)
{
   *size = tile_animation_bytes;
   return tile_animations;
}

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
   *size = resource_normal_bytes - tile_cache_offset;
   return resource_normal + tile_cache_offset;
}

DeceptusU8 *amigaRoomChipScratch(DeceptusU32 *size)
{
   *size = resource_chip_bytes - chip_cache_offset;
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
   chip_free = resource_chip_bytes - chip_cache_offset;
   normal_free = resource_normal_bytes - tile_cache_offset;
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
   DeceptusU32 plane_bytes, bytes, clips_size;
   DeceptusU8 *pixels;
   char path[64];
   plane_bytes = (DeceptusU32)((graphics.width + 15) / 16) * 2 * graphics.height;
   bytes = plane_bytes * 6;
   clips_size = 0;
   if (graphics.clip_count)
   {
      if (amiga_native_sizes[room][sword != 0][0] != bytes || !amiga_native_sizes[room][sword != 0][1])
      {
         return 1;
      }
      clips_size = amiga_native_sizes[room][sword != 0][1];
   }
   pixels = reserveChipScratch(bytes + clips_size);
   if (!pixels)
   {
      return 1;
   }
   roomPath(path, "Build:assets/area", room,
            graphics.clip_count ? (sword ? "/graphics-sword.acb" : "/graphics.acb") : (sword ? "/graphics-sword.acp" : "/graphics.acp"), 0);
   if (amigaSystemReadFile(path, pixels, bytes) != bytes)
   {
      return 0;
   }
   if (clips_size)
   {
      roomPath(path, "Build:assets/area", room, sword ? "/graphics-sword.aco" : "/graphics.aco", 0);
      if (amigaSystemReadFile(path, pixels + bytes, clips_size) != clips_size ||
          !amigaGraphicsSetNativeClips(&graphics, pixels + bytes, clips_size))
      {
         return 0;
      }
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
   chip_free = resource_chip_bytes - chip_cache_offset;
   in_chip = 0;
   if (reserved <= resource_normal_bytes - tile_cache_offset &&
       (resource_normal_bytes - tile_cache_offset - reserved >= minimum || chip_free >= minimum))
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
   normal_free = resource_normal_bytes - tile_cache_offset;
   mask_bytes = (DeceptusU32)(width / 8) * height;
   graphics.viewport_pixels = (DeceptusU16 *)reserveChipScratch(mask_bytes * 10);
   graphics.foreground_pixels = graphics.viewport_pixels ? (DeceptusU16 *)reserveChipScratch(mask_bytes) : 0;
   if (!graphics.foreground_pixels || resource_chip_bytes - chip_cache_offset < 16UL * 1024 ||
       (tile_bytes && resource_chip_bytes - checkpoint >= smallest_window + tile_bytes &&
        resource_chip_bytes - chip_cache_offset < tile_bytes) ||
       (tile_bytes && resource_chip_bytes - checkpoint + normal_free >= smallest_window + tile_bytes + AMIGA_DECODED_SPRITE_BYTES &&
        resource_chip_bytes - chip_cache_offset + normal_free < tile_bytes + AMIGA_DECODED_SPRITE_BYTES))
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

static DeceptusU8 reserveNativeWindow(DeceptusU16 width, DeceptusU16 height)
{
   DeceptusU32 bytes;
   /* reuse the compact buffer pair for one larger page; the other borrows a resource-bank tail. */
   bytes = (DeceptusU32)(width / 8) * height * 5;
   if (bytes > (DeceptusU32)(AMIGA_BASE_CACHE_WIDTH / 8) * AMIGA_BASE_CACHE_HEIGHT * 10)
   {
      return 0;
   }
   graphics.viewport_pixels = (DeceptusU16 *)reserveChipScratch(bytes);
   if (!graphics.viewport_pixels)
   {
      return 0;
   }
   graphics.viewport_width = width;
   graphics.viewport_height = height;
   graphics.viewport_split = 1;
   return 1;
}

static DeceptusU16 nativeWord(const DeceptusU8 *bytes)
{
   return ((DeceptusU16)bytes[0] << 8) | bytes[1];
}

static DeceptusU32 nativeLong(const DeceptusU8 *bytes)
{
   return ((DeceptusU32)nativeWord(bytes) << 16) | nativeWord(bytes + 2);
}

static DeceptusU8 loadTileGraphics(DeceptusU8 room, DeceptusU8 sword)
{
   const DeceptusU32 *sizes;
   DeceptusU8 *chip, *normal;
   DeceptusU32 metadata, atlas, animations, sprite_bytes, available, split, index, table, end, atlas_at, animation_at, sprites_at;
   char path[64];
   sizes = amiga_streamed_sizes[room][sword != 0];
   atlas = sizes[0];
   metadata = sizes[1];
   sprite_bytes = sizes[2];
   animations = sizes[3];
   chip = (DeceptusU8 *)resource_chip;
   normal = resource_normal;
   /* the cpu reads dictionaries, animations and compressed sprites, so they fill the non-chip bank
      first; only a sprite tail spills into Chip RAM, which blitter sources need. */
   atlas_at = (metadata + 3) & ~3UL;
   animation_at = (atlas_at + atlas + 3) & ~3UL;
   sprites_at = (animation_at + animations + 3) & ~3UL;
   if (!atlas || !metadata || sprites_at > resource_normal_bytes)
   {
      return 0;
   }
   roomPath(path, "Build:assets/area", room, sword ? "/graphics-sword.ans" : "/graphics.ans", 0);
   if (amigaSystemReadFile(path, normal, metadata) != metadata)
   {
      return 0;
   }
   available = resource_normal_bytes - sprites_at;
   split = sprite_bytes;
   table = nativeLong(normal + 40);
   if (table > metadata || (DeceptusU32)nativeWord(normal + 10) * 28 > metadata - table)
   {
      return 0;
   }
   if (sprite_bytes > available)
   {
      /* split only at an independent compressed block boundary. */
      split = 0;
      for (index = 0; index < nativeWord(normal + 10); ++index)
      {
         end = nativeLong(normal + table + index * 28 + 12) + nativeLong(normal + table + index * 28 + 16);
         if (end <= available && end > split)
         {
            split = end;
         }
      }
   }
   if (sprite_bytes - split > resource_chip_bytes)
   {
      return 0;
   }
   roomPath(path, "Build:assets/area", room, sword ? "/graphics-sword.atg" : "/graphics.atg", 0);
   if (amigaSystemReadFile(path, normal + atlas_at, atlas) != atlas || !amigaTileArchiveInit(&native_tiles, normal + atlas_at, atlas))
   {
      return 0;
   }
   roomPath(path, "Build:assets/area", room, sword ? "/graphics-sword.ant" : "/graphics.ant", 0);
   if (amigaSystemReadFile(path, normal + animation_at, animations) != animations)
   {
      return 0;
   }
   tile_animations = normal + animation_at;
   tile_animation_bytes = animations;
   roomPath(path, "Build:assets/area", room, sword ? "/graphics-sword.asb" : "/graphics.asb", 0);
   if (amigaSystemReadFilePair(path, normal + sprites_at, split, chip, resource_chip_bytes) != sprite_bytes ||
       !amigaGraphicsInit(&graphics, normal, metadata, normal + sprites_at, sprite_bytes, split, chip))
   {
      return 0;
   }
   tile_cache_offset = (sprites_at + split + 3) & ~3UL;
   chip_cache_offset = (sprite_bytes - split + 3) & ~3UL;
   /* the room's message boxes come with the room, while the screen is blank anyway; without them
      (no file, no room) dialogues show the plain box. */
   message_boxes = 0;
   message_box_bytes = amigaOpeningVideoMessageBoxBytes(room);
   if (message_box_bytes)
   {
      /* cpu-read like the dictionaries: non-chip scratch first, Chip scratch when a room fills it. */
      roomPath(path, "Build:assets/area", room, "/messagebox.amb", 0);
      if (tile_cache_offset + message_box_bytes <= resource_normal_bytes)
      {
         if (amigaSystemReadFile(path, normal + tile_cache_offset, message_box_bytes) == message_box_bytes)
         {
            message_boxes = normal + tile_cache_offset;
            tile_cache_offset = (tile_cache_offset + message_box_bytes + 3) & ~3UL;
         }
      }
      else if (chip_cache_offset + message_box_bytes <= resource_chip_bytes &&
               amigaSystemReadFile(path, chip + chip_cache_offset, message_box_bytes) == message_box_bytes)
      {
         message_boxes = chip + chip_cache_offset;
         chip_cache_offset = (chip_cache_offset + message_box_bytes + 3) & ~3UL;
      }
   }
   if (!message_boxes)
   {
      message_box_bytes = 0;
   }
   sword_graphics = sword;
   amigaOpeningVideoReset();
   return 1;
}

DeceptusU8 amigaRoomLoadGraphics(DeceptusU8 room, DeceptusU8 sword)
{
   char path[64];
   DeceptusU8 *world;
   DeceptusU32 world_size, sprite_size, split, native_bytes;
   DeceptusU8 native_only;
   if (room >= AMIGA_OPENING_ROOMS)
   {
      return 0;
   }
   amigaOpeningVideoSuspend();
   amigaKeyboardReset();
   if (amigaOpeningVideoUsesTiles())
   {
      return loadTileGraphics(room, sword);
   }
   world = (DeceptusU8 *)resource_chip;
   native_bytes = amiga_native_sizes[room][sword != 0][0] + amiga_native_sizes[room][sword != 0][1] +
                  amiga_native_sizes[room][sword != 0][2] + amiga_native_sizes[room][sword != 0][3] + 3;
   native_only = amiga_native_sizes[room][sword != 0][0] && amiga_native_sizes[room][sword != 0][2] &&
                 native_bytes <= resource_chip_bytes && resource_chip_bytes - native_bytes >= AMIGA_SPRITE_SCRATCH_BYTES;
   roomPath(path, "Build:assets/area", room,
            native_only ? (sword ? "/graphics-sword.agn" : "/graphics.agn") : (sword ? "/graphics-sword.agp" : "/graphics.agp"), 0);
   world_size = amigaSystemReadFile(path, world, resource_chip_bytes);
   if (!world_size || world_size >= resource_chip_bytes)
   {
      return 0;
   }
   split = native_only ? amiga_native_sizes[room][sword != 0][3]
                       : amigaGraphicsSpriteSplit(world, world_size, resource_chip_bytes - world_size);
   roomPath(path, "Build:assets/area", room, sword ? "/graphics-sword.asp" : "/graphics.asp", 0);
   sprite_size = amigaSystemReadFilePair(path, world + world_size, split, resource_normal, resource_normal_bytes);
   if (!sprite_size)
   {
      return 0;
   }
   if (split == sprite_size && sprite_size <= resource_normal_bytes)
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
   if (tile_cache_offset > resource_normal_bytes)
   {
      return 0;
   }
   if (!loadNativeComposite(room, sword) || (graphics.native_only && !graphics.native_foreground) || !loadRasterColumns(room, sword))
   {
      return 0;
   }
   if (graphics.native_only)
   {
      reserveNativeWindow(AMIGA_NATIVE_CACHE_WIDTH, AMIGA_NATIVE_CACHE_HEIGHT);
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
