#include "opening_video.h"
#include "amiga_opening_assets.h"
#include "deceptus/appearance.h"
#include "opening_ui.h"
#include "rastercache.h"
#include "rooms.h"
#include "spritecache.h"
#include "system.h"
#include <hardware/blit.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <hardware/intbits.h>
#include <stddef.h>
#include <string.h>

#define BASE_CACHE_WIDTH AMIGA_BASE_CACHE_WIDTH
#define BASE_CACHE_HEIGHT AMIGA_BASE_CACHE_HEIGHT
#define BASE_BUFFER_WORDS ((BASE_CACHE_WIDTH / 16) * BASE_CACHE_HEIGHT * 5)
#if (AMIGA_LARGE_CACHE_WIDTH / 8) * AMIGA_LARGE_CACHE_HEIGHT * 5 > BASE_BUFFER_WORDS * 4 ||                                                \
   (AMIGA_WIDE_CACHE_WIDTH / 8) * AMIGA_WIDE_CACHE_HEIGHT * 5 > BASE_BUFFER_WORDS * 4 ||                                                   \
   (AMIGA_TALL_CACHE_WIDTH / 8) * AMIGA_TALL_CACHE_HEIGHT * 5 > BASE_BUFFER_WORDS * 4
#error the background reference must fit the inactive compact buffers
#endif
#define SPRITE_CAPACITY 96
#define SPRITE_IDENTITY_MASK 0x0fff
#define SPRITE_FLIPPED 0x0800
#define SPRITE_REDRAW 0x8000
#if AMIGA_GRAPHICS_RESOURCES > 2048
#error sprite identities require resources below 2048
#endif
#define TILE_CACHE_SLOTS AMIGA_TILE_CACHE_GROUPS
#define SPRITE_CACHE_BYTES AMIGA_DECODED_SPRITE_BYTES
#define ROW_CACHE_WAYS 4
#define ROW_CACHE_SETS 16
#define CHIP __attribute__((section(".MEMF_CHIP")))

typedef struct AmigaRowCache
{
   DeceptusU32 map;
   DeceptusU16 row, columns, rows;
   DeceptusU16 cells[256];
   DeceptusU8 valid;
} AmigaRowCache;

typedef struct AmigaSpriteRequest
{
   AmigaGraphicsSprite graphic;
   DeceptusI16 x, y;
   DeceptusU16 identity_flags;
} AmigaSpriteRequest;

typedef struct AmigaClipState
{
   AmigaGraphicsClip clip;
   DeceptusU16 frame;
   DeceptusU8 enabled;
} AmigaClipState;

static volatile struct Custom *const hardware = (struct Custom *)0xdff000;
static DeceptusU16 base_buffers[2 * BASE_BUFFER_WORDS] CHIP;
static DeceptusU16 *buffers[2];
static DeceptusU16 *background_cache;
static DeceptusI16 background_origin_x, background_origin_y, window_origin_x, window_origin_y;
static DeceptusU8 background_valid, window_valid;
static DeceptusU16 cache_width, cache_height;
/* word-sized strides let the 68000 use native multiplication in the draw path. */
static DeceptusI16 row_bytes, row_words, plane_bytes, plane_words;
static DeceptusU16 copper[2][128] CHIP;
static DeceptusU16 blank_copper[6] CHIP = {0x0100, 0, 0x0180, 0, 0xffff, 0xfffe};
static DeceptusU16 sprite_pixels[(AMIGA_SPRITE_SCRATCH_BYTES + 1) / 2] CHIP;
static DeceptusU16 palette[32];
static AmigaSpriteCache sprite_cache;
static AmigaRasterCache raster_cache CHIP;
static DeceptusU8 *tile_cache, *tile_directory;
static DeceptusU16 tile_directory_count;
static DeceptusU16 tile_slots;
static DeceptusU16 tile_groups[TILE_CACHE_SLOTS];
static AmigaRowCache base_rows[ROW_CACHE_WAYS] CHIP;
static AmigaRowCache *row_cache = base_rows;
static DeceptusU8 row_sets, row_cursors[ROW_CACHE_SETS];
static DeceptusU8 tile_cursor;
static AmigaClipState clips[AMIGA_OPENING_CLIPS];
static DeceptusU16 previous_clip_frames[2][AMIGA_OPENING_CLIPS];
static DeceptusU8 previous_clip_enabled[2][AMIGA_OPENING_CLIPS];
static DeceptusU16 cached_clip_frames[AMIGA_OPENING_CLIPS];
static DeceptusU8 cached_clip_enabled[AMIGA_OPENING_CLIPS];
static AmigaSpriteRequest requests[SPRITE_CAPACITY];
static DeceptusU16 request_count;
static DeceptusRect previous_sprites[2][SPRITE_CAPACITY];
static DeceptusU16 previous_sprite_keys[2][SPRITE_CAPACITY] CHIP;
static DeceptusU16 previous_counts[2];
static DeceptusRect previous_ui[2][2];
static DeceptusU8 previous_ui_count[2], previous_ui_mode[2], previous_ui_layout[2];
static DeceptusI16 previous_camera_x[2], previous_camera_y[2];
static DeceptusI16 origins_x[2], origins_y[2], origin_x, origin_y;
static DeceptusU8 valid[2], active, draw_buffer, error;
static const AmigaGraphics *graphics;
static volatile DeceptusU32 video_frames;
/* the first vertical blank that may show the pending page, and the one for the page being drawn:
   the previous page can still be pending while the next one's blank is chosen. */
static volatile DeceptusU32 present_frame;
static DeceptusU32 next_present;
static volatile DeceptusU8 loading;
static volatile DeceptusI16 pending_buffer = -1;
static DeceptusPlayerAnimation animation = DECEPTUS_PLAYER_ANIMATION_INITIALIZER;
static DeceptusU16 last_ticks, last_flags;
static DeceptusU8 last_mode, last_selection, last_page, last_prompt, last_label;
static const DeceptusInteraction *last_dialogue;

DeceptusU8 amigaOpeningVideoUsesTiles(void)
{
   return 0;
}

static void __attribute__((interrupt)) verticalBlank(void)
{
   if (!loading)
   {
      ++video_frames;
   }
   /* a page waits for the blank its game time was advanced to; shown earlier, the scene would step
      by more time than the screen did and the background would judder against the player. */
   if (pending_buffer >= 0 && (loading || (DeceptusI32)(video_frames - present_frame) >= 0))
   {
      hardware->cop1lc = (ULONG)copper[pending_buffer];
      hardware->copjmp1 = 0;
      pending_buffer = -1;
   }
   hardware->intreq = INTF_VERTB;
   hardware->intreq = INTF_VERTB;
}

/* the legacy renderer keeps its static display buffers. */
DeceptusU8 amigaOpeningVideoAllocate(void)
{
   return 1;
}

void amigaOpeningVideoRelease(void)
{
}

DeceptusU32 amigaOpeningVideoFrames(void)
{
   return video_frames;
}

void amigaOpeningVideoPresentAt(DeceptusU32 frame)
{
   next_present = frame;
}

static void waitFrame(void)
{
   DeceptusU32 before;
   before = video_frames;
   while (video_frames == before)
   {
   }
}

static void waitBlitter(void)
{
   DeceptusU16 previous_priority;
   (void)hardware->dmaconr;
   previous_priority = hardware->dmaconr & DMAF_BLITHOG;
   /* give pending DMA the bus while the CPU has no independent work to do. */
   hardware->dmacon = DMAF_SETCLR | DMAF_BLITHOG;
   while (hardware->dmaconr & 0x4000)
   {
   }
   if (!previous_priority)
   {
      hardware->dmacon = DMAF_BLITHOG;
   }
}

void amigaOpeningVideoSuspend(void)
{
   if (active)
   {
      loading = 0;
      waitFrame();
      amigaSystemReleaseDisplay();
      active = 0;
   }
}

void amigaOpeningVideoClose(void)
{
   amigaOpeningVideoSuspend();
   amigaSystemClose();
}

static DeceptusU32 __attribute__((noinline, noclone, optimize("Os"))) cacheTail(DeceptusU8 **memory, DeceptusU32 size)
{
   const DeceptusU8 *regions[3];
   DeceptusU32 sizes[3], offset, end;
   DeceptusU8 index;
   regions[0] = tile_directory;
   sizes[0] = (DeceptusU32)(tile_cache - tile_directory) + (DeceptusU32)tile_slots * AMIGA_TILE_GROUP_BYTES;
   regions[1] = (const DeceptusU8 *)row_cache;
   sizes[1] = (DeceptusU32)row_sets * ROW_CACHE_WAYS * sizeof(*row_cache);
   regions[2] = sprite_cache.pixels;
   sizes[2] = sprite_cache.capacity;
   offset = 0;
   for (index = 0; index < 3; ++index)
   {
      if ((size_t)regions[index] >= (size_t)*memory && (size_t)regions[index] < (size_t)*memory + size)
      {
         end = (DeceptusU32)((size_t)regions[index] - (size_t)*memory) + sizes[index];
         if (end > offset)
         {
            offset = end;
         }
      }
   }
   if (offset > size)
   {
      return 0;
   }
   *memory += offset;
   return size - offset;
}

static DeceptusU8 __attribute__((noinline, noclone, optimize("Os"))) prepareCaches(void)
{
   DeceptusU32 size, offset, sprite_size, chip_size, unused;
   DeceptusU16 slot;
   DeceptusU8 *sprite_memory, *chip_memory, *row_memory, *base_memory;
   DeceptusU32 row_cache_bytes, base_size;
   DeceptusU8 rows_in_sprite_bank, sprite_in_chip, tile_in_chip, swap_banks;
   row_cache = base_rows;
   row_sets = 1;
   rows_in_sprite_bank = 0;
   sprite_in_chip = 1;
   tile_in_chip = 0;
   tile_directory = amigaRoomScratch(&size);
   chip_memory = amigaRoomChipScratch(&chip_size);
   sprite_memory = chip_memory;
   sprite_size = chip_size;
   if (graphics->native_only)
   {
      if (!graphics->native_foreground || (graphics->clip_count && !graphics->native_clips))
      {
         return 0;
      }
      tile_directory_count = tile_slots = 0;
      if (graphics->viewport_split && sizeof(base_buffers) - plane_bytes * 5 > chip_size)
      {
         chip_memory = (DeceptusU8 *)base_buffers + plane_bytes * 5;
         chip_size = sizeof(base_buffers) - plane_bytes * 5;
      }
      if (chip_size >= size)
      {
         amigaSpriteCacheInit(&sprite_cache, chip_memory, chip_size < 96UL * 1024 ? chip_size : 96UL * 1024, 1);
      }
      else
      {
         amigaSpriteCacheInit(&sprite_cache, tile_directory, size < 96UL * 1024 ? size : 96UL * 1024, 0);
      }
      if (sprite_cache.chip_memory)
      {
         amigaSpriteCacheSetScratch(&sprite_cache, tile_directory, size);
      }
      amigaRasterCacheInit(&raster_cache, 0, 0, 0, 0);
      return 1;
   }
   tile_directory_count = (DeceptusU16)(((DeceptusU32)graphics->pattern_count + 15) / 16);
   offset = ((DeceptusU32)tile_directory_count + 3) & ~3UL;
   swap_banks = chip_size > size;
   if (graphics->native_foreground && size >= offset + AMIGA_TILE_GROUP_BYTES && chip_size >= offset + AMIGA_TILE_GROUP_BYTES)
   {
      /* only a full room image avoids tile-cache pressure during scroll refills. */
      swap_banks = size > chip_size;
   }
   if (swap_banks)
   {
      sprite_memory = tile_directory;
      sprite_size = size;
      sprite_in_chip = 0;
      tile_in_chip = 1;
      tile_directory = chip_memory;
      size = chip_size;
   }
   if (!tile_directory || size < offset + AMIGA_TILE_GROUP_BYTES)
   {
      return 0;
   }
   tile_cache = tile_directory + offset;
   tile_slots = (DeceptusU16)((size - offset) / AMIGA_TILE_GROUP_BYTES);
   if (tile_slots > TILE_CACHE_SLOTS)
   {
      tile_slots = TILE_CACHE_SLOTS;
   }
   if (tile_slots > tile_directory_count)
   {
      tile_slots = tile_directory_count;
   }
   if (graphics->raster && tile_slots > AMIGA_RASTER_TILE_GROUPS)
   {
      tile_slots = AMIGA_RASTER_TILE_GROUPS;
   }
   unused = size - offset - (DeceptusU32)tile_slots * AMIGA_TILE_GROUP_BYTES;
   row_memory = tile_cache + (DeceptusU32)tile_slots * AMIGA_TILE_GROUP_BYTES;
   row_cache_bytes = unused;
   if ((graphics->raster || tile_slots == tile_directory_count) && unused > sprite_size)
   {
      /* retain decoded actors before enlarging the optional row lookup cache. */
      row_cache_bytes = 0;
   }
   if (sprite_size > SPRITE_CACHE_BYTES && sprite_size - SPRITE_CACHE_BYTES > row_cache_bytes)
   {
      rows_in_sprite_bank = 1;
      row_memory = sprite_memory + SPRITE_CACHE_BYTES;
      row_cache_bytes = sprite_size - SPRITE_CACHE_BYTES;
   }
   while (row_sets < ROW_CACHE_SETS && (DeceptusU32)row_sets * 2 * ROW_CACHE_WAYS * sizeof(*row_cache) <= row_cache_bytes)
   {
      row_sets *= 2;
   }
   if (row_sets > 1)
   {
      row_cache = (AmigaRowCache *)row_memory;
      row_cache_bytes = (DeceptusU32)row_sets * ROW_CACHE_WAYS * sizeof(*row_cache);
      if (rows_in_sprite_bank)
      {
         sprite_size = SPRITE_CACHE_BYTES;
      }
      else
      {
         unused -= row_cache_bytes;
      }
   }
   if (sprite_size < SPRITE_CACHE_BYTES && unused > sprite_size)
   {
      sprite_memory = tile_cache + (DeceptusU32)tile_slots * AMIGA_TILE_GROUP_BYTES + (row_cache == base_rows ? 0 : row_cache_bytes);
      sprite_size = unused;
      sprite_in_chip = tile_in_chip;
   }
   if (sprite_size > SPRITE_CACHE_BYTES)
   {
      sprite_size = SPRITE_CACHE_BYTES;
   }
   base_memory = (DeceptusU8 *)base_buffers;
   base_size = 0;
   if (graphics->viewport_pixels)
   {
      base_size = sizeof(base_buffers) - (DeceptusU32)plane_bytes * 5;
      base_memory += (DeceptusU32)plane_bytes * 5;
   }
   if (base_size > sprite_size)
   {
      sprite_memory = base_memory;
      sprite_size = base_size < SPRITE_CACHE_BYTES ? base_size : SPRITE_CACHE_BYTES;
      sprite_in_chip = 1;
   }
   amigaSpriteCacheInit(&sprite_cache, sprite_memory, sprite_size, sprite_in_chip);
   sprite_memory = amigaRoomScratch(&size);
   size = cacheTail(&sprite_memory, size);
   chip_memory = amigaRoomChipScratch(&chip_size);
   chip_size = cacheTail(&chip_memory, chip_size);
   base_size = cacheTail(&base_memory, base_size);
   if (base_size > chip_size)
   {
      chip_memory = base_memory;
      chip_size = base_size;
   }
   amigaRasterCacheInit(&raster_cache, sprite_memory, size, chip_memory, chip_size);
   memset(tile_directory, 255, tile_directory_count);
   for (slot = 0; slot < tile_slots; ++slot)
   {
      tile_groups[slot] = 65535U;
      if (tile_slots == tile_directory_count)
      {
         /* a complete tile working set never needs decompression during play. */
         if (!amigaGraphicsTileGroup(graphics, slot, tile_cache + (DeceptusU32)slot * AMIGA_TILE_GROUP_BYTES))
         {
            return 0;
         }
         tile_groups[slot] = slot;
         tile_directory[slot] = (DeceptusU8)slot;
      }
   }
   tile_cursor = 0;
   return 1;
}

void __attribute__((optimize("Os"))) amigaOpeningVideoReset(void)
{
   DeceptusU8 buffer;
   graphics = amigaRoomGraphics();
   cache_width = graphics->viewport_pixels ? graphics->viewport_width : BASE_CACHE_WIDTH;
   cache_height = graphics->viewport_pixels ? graphics->viewport_height : BASE_CACHE_HEIGHT;
   row_bytes = cache_width / 8;
   row_words = cache_width / 16;
   plane_bytes = (DeceptusU32)row_bytes * cache_height;
   plane_words = plane_bytes / 2;
   for (buffer = 0; buffer < 2; ++buffer)
   {
      if (graphics->viewport_split)
      {
         buffers[buffer] = buffer ? graphics->viewport_pixels : base_buffers;
      }
      else
      {
         buffers[buffer] = graphics->viewport_pixels ? graphics->viewport_pixels + (DeceptusU32)buffer * plane_words * 5
                                                     : base_buffers + (DeceptusU32)buffer * BASE_BUFFER_WORDS;
      }
   }
   background_cache = graphics->viewport_pixels && !graphics->viewport_split ? base_buffers : 0;
   background_valid = window_valid = 0;
   error = !prepareCaches() || !amigaGraphicsPalette(graphics, palette) || graphics->clip_count > AMIGA_OPENING_CLIPS;
   valid[0] = valid[1] = 0;
   previous_counts[0] = previous_counts[1] = 0;
   previous_ui_count[0] = previous_ui_count[1] = 0;
   memset(row_cache, 0, (DeceptusU32)row_sets * ROW_CACHE_WAYS * sizeof(*row_cache));
   memset(row_cursors, 0, sizeof(row_cursors));
   memset(requests, 0, sizeof(requests));
   animation.previous_pose = 255;
}

static const DeceptusU8 *tilePixels(DeceptusU16 pattern)
{
   DeceptusU16 group;
   DeceptusU8 slot;
   group = pattern / AMIGA_TILE_GROUP;
   if (group >= tile_directory_count)
   {
      error = 1;
      return 0;
   }
   slot = tile_directory[group];
   if (slot != 255)
   {
      return tile_cache + (DeceptusU32)slot * AMIGA_TILE_GROUP_BYTES + (pattern % AMIGA_TILE_GROUP) * AMIGA_TILE_BYTES;
   }
   slot = tile_cursor;
   if (++tile_cursor >= tile_slots)
   {
      tile_cursor = 0;
   }
   if (tile_groups[slot] != 65535U)
   {
      tile_directory[tile_groups[slot]] = 255;
   }
   if (!amigaGraphicsTileGroup(graphics, group, tile_cache + (DeceptusU32)slot * AMIGA_TILE_GROUP_BYTES))
   {
      error = 1;
      return 0;
   }
   tile_groups[slot] = group;
   tile_directory[group] = slot;
   return tile_cache + (DeceptusU32)slot * AMIGA_TILE_GROUP_BYTES + (pattern % AMIGA_TILE_GROUP) * AMIGA_TILE_BYTES;
}

static DeceptusU16 __attribute__((noinline, noclone)) mapCell(DeceptusU32 map, DeceptusU16 columns, DeceptusU16 rows, DeceptusU16 column,
                                                              DeceptusU16 row)
{
   AmigaRowCache *entry;
   DeceptusU8 slot, set, first;
   if (column >= columns || row >= rows)
   {
      return 0;
   }
   set = (DeceptusU8)((map ^ (map >> 8) ^ row) & (row_sets - 1));
   first = set * ROW_CACHE_WAYS;
   for (slot = first; slot < first + ROW_CACHE_WAYS; ++slot)
   {
      entry = &row_cache[slot];
      if (entry->valid && entry->map == map && entry->row == row && entry->columns == columns && entry->rows == rows)
      {
         return entry->cells[column];
      }
   }
   entry = &row_cache[first + (row_cursors[set]++ & (ROW_CACHE_WAYS - 1))];
   entry->valid = 0;
   if (!amigaGraphicsRow(graphics, map, columns, rows, row, entry->cells))
   {
      error = 1;
      return 0;
   }
   entry->map = map;
   entry->columns = columns;
   entry->rows = rows;
   entry->row = row;
   entry->valid = 1;
   return entry->cells[column];
}

/* one copy of the planar writer bounds code size across destination buffers. */
static void __attribute__((noinline, noclone)) paintPattern(DeceptusU16 *buffer, DeceptusI16 x, DeceptusI16 y, DeceptusU16 pattern,
                                                            DeceptusU8 masked)
{
   const DeceptusU8 *pixels, *mask;
   DeceptusU8 *destination, *plane_start;
   DeceptusU8 plane, row;
   if (!pattern && masked)
   {
      return;
   }
   pixels = tilePixels(pattern);
   if (!pixels)
   {
      return;
   }
   mask = pixels + 40;
   plane_start = (DeceptusU8 *)buffer + y * row_bytes + x / 8;
   for (plane = 0; plane < 5; ++plane)
   {
      destination = plane_start;
      if (masked)
      {
         for (row = 0; row < 8; ++row)
         {
            *destination = (DeceptusU8)((*destination & ~mask[row]) | (pixels[row] & mask[row]));
            destination += row_bytes;
         }
      }
      else
      {
         for (row = 0; row < 8; ++row)
         {
            *destination = pixels[row];
            destination += row_bytes;
         }
      }
      pixels += 8;
      plane_start += plane_bytes;
   }
}

static DeceptusU16 clipCell(const AmigaClipState *state, DeceptusI16 world_x, DeceptusI16 world_y)
{
   const AmigaGraphicsClip *clip;
   DeceptusU32 rows;
   DeceptusI16 x, y;
   clip = &state->clip;
   x = world_x - clip->bounds.x;
   y = world_y - clip->bounds.y;
   if (!state->enabled || x < 0 || y < 0 || x >= clip->bounds.width || y >= clip->bounds.height)
   {
      return 0;
   }
   rows = (DeceptusU32)(clip->bounds.height / 8) * clip->frames;
   if (rows > 65535UL)
   {
      error = 1;
      return 0;
   }
   return mapCell(clip->map, clip->bounds.width / 8, (DeceptusU16)rows, x / 8, state->frame * (clip->bounds.height / 8) + y / 8);
}

static void paintTile(DeceptusI16 x, DeceptusI16 y, DeceptusU8 foreground_only, DeceptusU8 base_ready)
{
   DeceptusI16 world_x, world_y;
   DeceptusU16 pattern;
   DeceptusU8 index;
   world_x = origin_x + x;
   world_y = origin_y + y;
   pattern = 0;
   if (!foreground_only)
   {
      if (!base_ready)
      {
         if (world_x >= 0 && world_y >= 0 && world_x < graphics->width && world_y < graphics->height)
         {
            pattern = mapCell(graphics->background, graphics->width / 8, graphics->height / 8, world_x / 8, world_y / 8);
         }
         paintPattern(buffers[draw_buffer], x, y, pattern, 0);
      }
      for (index = 0; index < graphics->clip_count; ++index)
      {
         if (!clips[index].clip.plane)
         {
            pattern = clipCell(&clips[index], world_x, world_y);
            paintPattern(buffers[draw_buffer], x, y, pattern, 1);
         }
      }
   }
   if (world_x >= 0 && world_y >= 0 && world_x < graphics->width && world_y < graphics->height)
   {
      pattern = mapCell(graphics->foreground, graphics->width / 8, graphics->height / 8, world_x / 8, world_y / 8);
      paintPattern(buffers[draw_buffer], x, y, pattern, 1);
   }
   for (index = 0; index < graphics->clip_count; ++index)
   {
      if (clips[index].clip.plane)
      {
         pattern = clipCell(&clips[index], world_x, world_y);
         paintPattern(buffers[draw_buffer], x, y, pattern, 1);
      }
   }
}

static DeceptusU8 __attribute__((noinline, noclone, optimize("Os"))) clipNativeBounds(DeceptusI16 *left, DeceptusI16 *top,
                                                                                      DeceptusI16 *right, DeceptusI16 *bottom)
{
   DeceptusI16 edge;
   if (*left < -origin_x)
   {
      *left = -origin_x;
   }
   if (*top < -origin_y)
   {
      *top = -origin_y;
   }
   edge = ((graphics->width + 15) / 16) * 16 - origin_x;
   if (*right > edge)
   {
      *right = edge;
   }
   edge = graphics->height - origin_y;
   if (*bottom > edge)
   {
      *bottom = edge;
   }
   return *left < *right && *top < *bottom;
}

static void __attribute__((noinline, noclone, optimize("Os"))) clearNativeRectangle(DeceptusI16 left, DeceptusI16 top, DeceptusI16 right,
                                                                                    DeceptusI16 bottom)
{
   DeceptusU16 plane, words;
   DeceptusU16 *destination;
   words = (right - left) / 16;
   destination = buffers[draw_buffer] + top * row_words + left / 16;
   waitBlitter();
   hardware->bltcon0 = DEST;
   hardware->bltcon1 = 0;
   hardware->bltdmod = row_bytes - words * 2;
   for (plane = 0; plane < 5; ++plane)
   {
      waitBlitter();
      hardware->bltdpt = destination;
      hardware->bltsize = ((bottom - top) << 6) | words;
      destination += plane_words;
   }
   waitBlitter();
}

static DeceptusU8 __attribute__((noinline, noclone, optimize("Os"))) drawNativeBackground(DeceptusI16 left, DeceptusI16 top,
                                                                                          DeceptusI16 right, DeceptusI16 bottom)
{
   DeceptusU16 plane, width, height, source_words, source_height;
   DeceptusI16 source_x, source_y;
   DeceptusU32 source_plane;
   const DeceptusU16 *source;
   DeceptusU16 *destination;
   if (background_valid)
   {
      source = background_cache;
      source_words = row_words;
      source_height = cache_height;
      source_x = origin_x + left - background_origin_x;
      source_y = origin_y + top - background_origin_y;
   }
   else
   {
      source = graphics->background_pixels;
      source_words = (graphics->width + 15) / 16;
      source_height = graphics->height;
      source_x = origin_x + left;
      source_y = origin_y + top;
   }
   width = (right - left) / 16;
   height = bottom - top;
   if (!source)
   {
      return 0;
   }
   if (source_x < 0 || source_y < 0 || source_x + right - left > source_words * 16 || source_y + height > source_height)
   {
      if (!graphics->native_foreground)
      {
         return 0;
      }
      /* guard words outside the room are blank; never reconstruct the whole view for them. */
      clearNativeRectangle(left, top, right, bottom);
      if (!clipNativeBounds(&left, &top, &right, &bottom))
      {
         return 1;
      }
      source_x = origin_x + left;
      source_y = origin_y + top;
      width = (right - left) / 16;
      height = bottom - top;
   }
   source_plane = (DeceptusU32)source_words * source_height;
   source += (DeceptusU32)source_y * source_words + source_x / 16;
   destination = buffers[draw_buffer] + top * row_words + left / 16;
   waitBlitter();
   hardware->bltcon0 = SRCA | DEST | A_TO_D;
   hardware->bltcon1 = 0;
   hardware->bltafwm = hardware->bltalwm = 65535U;
   hardware->bltamod = source_words * 2 - width * 2;
   hardware->bltdmod = row_bytes - width * 2;
   for (plane = 0; plane < 5; ++plane)
   {
      waitBlitter();
      hardware->bltapt = (APTR)(source + plane * source_plane);
      hardware->bltdpt = destination + plane * plane_words;
      hardware->bltsize = (height << 6) | width;
   }
   waitBlitter();
   return 1;
}

static DeceptusU8 __attribute__((noinline, noclone, optimize("Os"))) drawNativeForeground(DeceptusI16 left, DeceptusI16 top,
                                                                                          DeceptusI16 right, DeceptusI16 bottom)
{
   DeceptusI16 source_x, source_y;
   DeceptusU16 plane, width, height, source_words, source_height;
   DeceptusU32 offset, source_plane;
   const DeceptusU16 *source, *mask;
   DeceptusU16 *destination;
   source = graphics->native_foreground ? graphics->background_pixels : background_cache;
   mask = graphics->native_foreground ? graphics->native_foreground : graphics->foreground_pixels;
   source_words = graphics->native_foreground ? (graphics->width + 15) / 16 : row_words;
   source_height = graphics->native_foreground ? graphics->height : cache_height;
   source_x = origin_x + left - (graphics->native_foreground ? 0 : background_origin_x);
   source_y = origin_y + top - (graphics->native_foreground ? 0 : background_origin_y);
   if (source_x < 0 || source_y < 0 || source_x + right - left > source_words * 16 || source_y + bottom - top > source_height)
   {
      if (!graphics->native_foreground)
      {
         return 0;
      }
      if (!clipNativeBounds(&left, &top, &right, &bottom))
      {
         return 1;
      }
      source_x = origin_x + left;
      source_y = origin_y + top;
   }
   width = (right - left) / 16;
   height = bottom - top;
   offset = (DeceptusU32)source_y * source_words + source_x / 16;
   source_plane = (DeceptusU32)source_words * source_height;
   destination = buffers[draw_buffer] + top * row_words + left / 16;
   waitBlitter();
   hardware->bltcon0 = SRCA | SRCB | SRCC | DEST | 0xca;
   hardware->bltcon1 = 0;
   hardware->bltafwm = hardware->bltalwm = 65535U;
   hardware->bltamod = hardware->bltbmod = source_words * 2 - width * 2;
   hardware->bltcmod = hardware->bltdmod = row_bytes - width * 2;
   for (plane = 0; plane < 5; ++plane)
   {
      waitBlitter();
      hardware->bltapt = (APTR)(mask + offset);
      hardware->bltbpt = (APTR)(source + plane * source_plane + offset);
      hardware->bltcpt = destination + plane * plane_words;
      hardware->bltdpt = destination + plane * plane_words;
      hardware->bltsize = (height << 6) | width;
   }
   waitBlitter();
   return 1;
}

static DeceptusU8 __attribute__((noinline, noclone, optimize("Os"))) drawNativeClips(DeceptusI16 left, DeceptusI16 top, DeceptusI16 right,
                                                                                     DeceptusI16 bottom, DeceptusU8 foreground)
{
   DeceptusU16 index, plane, words, height, source_words;
   DeceptusI16 x, y, first, last, upper, lower;
   DeceptusU32 source_plane, offset;
   const DeceptusU16 *pixels, *mask;
   DeceptusU16 *destination;
   const AmigaClipState *state;
   DeceptusU8 drawn;
   drawn = 0;
   for (index = 0; index < graphics->clip_count; ++index)
   {
      state = &clips[index];
      if (!state->enabled || state->clip.plane != foreground)
      {
         continue;
      }
      x = state->clip.bounds.x / 16;
      if (state->clip.bounds.x < 0 && state->clip.bounds.x % 16)
      {
         --x;
      }
      x *= 16;
      source_words = (state->clip.bounds.x - x + state->clip.bounds.width + 15) / 16;
      x -= origin_x;
      y = state->clip.bounds.y - origin_y;
      first = left > x ? left : x;
      last = right < x + source_words * 16 ? right : x + source_words * 16;
      upper = top > y ? top : y;
      lower = bottom < y + state->clip.bounds.height ? bottom : y + state->clip.bounds.height;
      if (first >= last || upper >= lower)
      {
         continue;
      }
      pixels = amigaGraphicsNativeClip(graphics, index, state->frame);
      if (!pixels)
      {
         error = 1;
         return drawn;
      }
      words = (last - first) / 16;
      height = lower - upper;
      source_plane = (DeceptusU32)source_words * state->clip.bounds.height;
      offset = (DeceptusU32)(upper - y) * source_words + (first - x) / 16;
      pixels += offset;
      mask = pixels + source_plane * 5;
      destination = buffers[draw_buffer] + upper * row_words + first / 16;
      waitBlitter();
      hardware->bltcon0 = SRCA | SRCB | SRCC | DEST | 0xca;
      hardware->bltcon1 = 0;
      hardware->bltafwm = hardware->bltalwm = 65535U;
      hardware->bltamod = hardware->bltbmod = source_words * 2 - words * 2;
      hardware->bltcmod = hardware->bltdmod = row_bytes - words * 2;
      for (plane = 0; plane < 5; ++plane)
      {
         waitBlitter();
         hardware->bltapt = (APTR)mask;
         hardware->bltbpt = (APTR)(pixels + plane * source_plane);
         hardware->bltcpt = destination + plane * plane_words;
         hardware->bltdpt = destination + plane * plane_words;
         hardware->bltsize = (height << 6) | words;
      }
      waitBlitter();
      drawn = 1;
   }
   return drawn;
}

static void __attribute__((noinline, noclone, optimize("Os"))) paintRectangle(const DeceptusRect *rectangle, DeceptusU8 foreground_only)
{
   DeceptusI16 x, y, left, right, top, bottom;
   DeceptusU8 base_ready;
   if (rectangle->width <= 0 || rectangle->height <= 0)
   {
      return;
   }
   left = rectangle->x < 0 ? 0 : (rectangle->x / 8) * 8;
   top = rectangle->y < 0 ? 0 : (rectangle->y / 8) * 8;
   right = (rectangle->x + rectangle->width + 7) / 8 * 8;
   bottom = (rectangle->y + rectangle->height + 7) / 8 * 8;
   if (right > cache_width)
   {
      right = cache_width;
   }
   if (bottom > cache_height)
   {
      bottom = cache_height;
   }
   base_ready = 0;
   if ((!foreground_only && (graphics->background_pixels || background_valid)) || (graphics->foreground_pixels && background_valid) ||
       graphics->native_foreground)
   {
      left = left / 16 * 16;
      right = (right + 15) / 16 * 16;
   }
   if (left >= right || top >= bottom)
   {
      return;
   }
   if (!foreground_only && (graphics->background_pixels || background_valid))
   {
      base_ready = drawNativeBackground(left, top, right, bottom);
   }
   if (graphics->native_clips && (foreground_only ? drawNativeForeground(left, top, right, bottom) : base_ready))
   {
      if (!foreground_only && drawNativeClips(left, top, right, bottom, 0))
      {
         drawNativeForeground(left, top, right, bottom);
      }
      drawNativeClips(left, top, right, bottom, 1);
      return;
   }
   if ((graphics->native_foreground || (graphics->foreground_pixels && background_valid)) &&
       (foreground_only ? drawNativeForeground(left, top, right, bottom) : base_ready))
   {
      return;
   }
   for (y = top; y < bottom; y += 8)
   {
      for (x = left; x < right; x += 8)
      {
         paintTile(x, y, foreground_only, base_ready);
      }
   }
}

static void restoreOutside(const DeceptusRect *old, const DeceptusRect *covered)
{
   DeceptusRect rectangle;
   DeceptusI16 left, right, top, bottom;
   left = old->x > covered->x ? old->x : covered->x;
   top = old->y > covered->y ? old->y : covered->y;
   right = old->x + old->width < covered->x + covered->width ? old->x + old->width : covered->x + covered->width;
   bottom = old->y + old->height < covered->y + covered->height ? old->y + old->height : covered->y + covered->height;
   if (left >= right || top >= bottom)
   {
      paintRectangle(old, 0);
      return;
   }
   rectangle = *old;
   rectangle.height = top - old->y;
   paintRectangle(&rectangle, 0);
   rectangle.y = bottom;
   rectangle.height = old->y + old->height - bottom;
   paintRectangle(&rectangle, 0);
   rectangle.y = top;
   rectangle.height = bottom - top;
   rectangle.width = left - old->x;
   paintRectangle(&rectangle, 0);
   rectangle.x = right;
   rectangle.width = old->x + old->width - right;
   paintRectangle(&rectangle, 0);
}

static void updateClips(const DeceptusAdventure *game)
{
   DeceptusU16 index, frame, duration, flags;
   DeceptusU32 total, phase;
   const DeceptusU8 *bytes;
   AmigaClipState *state;
   flags = deceptusAdventurePresentationFlags(game);
   for (index = 0; index < graphics->clip_count; ++index)
   {
      state = &clips[index];
      if (!amigaGraphicsClip(graphics, index, &state->clip))
      {
         error = 1;
         return;
      }
      state->enabled = (flags & state->clip.required_flags) == state->clip.required_flags && !(flags & state->clip.forbidden_flags);
      if (!state->clip.durations)
      {
         state->frame = (game->campaign.engine.ticks / state->clip.frame_ticks) % state->clip.frames;
         continue;
      }
      bytes = graphics->world + state->clip.durations;
      total = 0;
      for (frame = 0; frame < state->clip.frames; ++frame)
      {
         duration = (DeceptusU16)((DeceptusU16)bytes[frame * 2] << 8) | bytes[frame * 2 + 1];
         if (!duration)
         {
            error = 1;
            return;
         }
         total += duration;
      }
      phase = game->campaign.engine.ticks % total;
      state->frame = 0;
      for (frame = 0; frame < state->clip.frames; ++frame)
      {
         duration = (DeceptusU16)((DeceptusU16)bytes[frame * 2] << 8) | bytes[frame * 2 + 1];
         if (phase < duration)
         {
            state->frame = frame;
            break;
         }
         phase -= duration;
      }
   }
}

static void __attribute__((noinline, noclone, optimize("Os"))) scrollBuffer(DeceptusU16 *pixels, DeceptusU8 planes, DeceptusI16 dx,
                                                                            DeceptusI16 dy)
{
   DeceptusI16 source_x, source_y, target_x, target_y, width, height;
   DeceptusU16 plane;
   DeceptusU8 reverse;
   DeceptusU16 *source, *destination;
   source_x = dx > 0 ? dx / 16 : 0;
   target_x = dx < 0 ? -dx / 16 : 0;
   source_y = dy > 0 ? dy : 0;
   target_y = dy < 0 ? -dy : 0;
   width = row_words - (dx < 0 ? -dx : dx) / 16;
   height = cache_height - (dy < 0 ? -dy : dy);
   reverse = target_y * row_words + target_x > source_y * row_words + source_x;
   waitBlitter();
   hardware->bltcon0 = SRCA | DEST | A_TO_D;
   hardware->bltcon1 = reverse ? BLITREVERSE : 0;
   hardware->bltafwm = hardware->bltalwm = 65535U;
   hardware->bltamod = hardware->bltdmod = row_bytes - width * 2;
   for (plane = 0; plane < planes; ++plane)
   {
      source = pixels + plane * plane_words + source_y * row_words + source_x;
      destination = pixels + plane * plane_words + target_y * row_words + target_x;
      if (reverse)
      {
         source += (height - 1) * row_words + width - 1;
         destination += (height - 1) * row_words + width - 1;
      }
      waitBlitter();
      hardware->bltapt = source;
      hardware->bltdpt = destination;
      hardware->bltsize = (height << 6) | width;
   }
   waitBlitter();
}

/* share the strip rebuild code to stay within the executable RAM-bank budget. */
static void __attribute__((noinline, noclone)) paintRasterStrip(DeceptusI16 left, DeceptusI16 top, DeceptusI16 width, DeceptusI16 height)
{
   DeceptusI16 x, y, row, world_x, world_y, count, source_y;
   DeceptusU8 plane, inside;
   const DeceptusU16 *source, *block;
   DeceptusU16 *destination;
   waitBlitter();
   for (x = left; x < left + width; x += 16)
   {
      world_x = origin_x + x;
      y = top;
      while (y < top + height)
      {
         world_y = origin_y + y;
         inside = world_x >= 0 && world_x < graphics->width && world_y >= 0 && world_y < graphics->height;
         source_y = inside ? world_y % AMIGA_RASTER_ROWS : 0;
         count = AMIGA_RASTER_ROWS - source_y;
         if (world_y < 0 && count > -world_y)
         {
            count = -world_y;
         }
         if (inside && count > graphics->height - world_y)
         {
            count = graphics->height - world_y;
         }
         if (count > top + height - y)
         {
            count = top + height - y;
         }
         block = graphics->raster_scratch;
         if (inside)
         {
            block = (const DeceptusU16 *)amigaRasterCachePixels(&raster_cache, graphics, world_x / 16, world_y / AMIGA_RASTER_ROWS,
                                                                (DeceptusU8 *)graphics->raster_scratch);
            if (!block)
            {
               error = 1;
               return;
            }
         }
         for (plane = 0; plane < 6; ++plane)
         {
            source = block + plane * AMIGA_RASTER_ROWS + source_y;
            destination = plane < 5 ? background_cache + plane * plane_words : graphics->foreground_pixels;
            destination += y * row_words + x / 16;
            for (row = 0; row < count; ++row)
            {
               *destination = inside ? source[row] : 0;
               destination += row_words;
            }
         }
         y += count;
      }
   }
}

static void __attribute__((noinline, noclone, optimize("Os"))) cacheForegroundTile(DeceptusI16 x, DeceptusI16 y, DeceptusU16 pattern,
                                                                                   DeceptusU8 append)
{
   const DeceptusU8 *mask;
   DeceptusU8 *destination, row;
   if (!pattern && append)
   {
      return;
   }
   paintPattern(background_cache, x, y, pattern, 1);
   mask = tilePixels(pattern);
   if (!mask)
   {
      return;
   }
   mask += 40;
   destination = (DeceptusU8 *)graphics->foreground_pixels + y * row_bytes + x / 8;
   for (row = 0; row < 8; ++row)
   {
      *destination = append ? (*destination | mask[row]) : mask[row];
      destination += row_bytes;
   }
}

static void __attribute__((noinline, noclone, optimize("Os"))) paintBackgroundStrip(DeceptusI16 left, DeceptusI16 top, DeceptusI16 width,
                                                                                    DeceptusI16 height)
{
   DeceptusI16 x, y, world_x, world_y;
   DeceptusU16 pattern;
   DeceptusU16 index;
   DeceptusU8 background_clip;
   if (graphics->raster)
   {
      paintRasterStrip(left, top, width, height);
      for (y = top; y < top + height; y += 8)
      {
         for (x = left; x < left + width; x += 8)
         {
            world_x = origin_x + x;
            world_y = origin_y + y;
            background_clip = 0;
            for (index = 0; index < graphics->clip_count; ++index)
            {
               if (!clips[index].clip.plane)
               {
                  pattern = clipCell(&clips[index], world_x, world_y);
                  if (pattern)
                  {
                     paintPattern(background_cache, x, y, pattern, 1);
                     background_clip = 1;
                  }
               }
            }
            if (background_clip && world_x >= 0 && world_y >= 0 && world_x < graphics->width && world_y < graphics->height)
            {
               pattern = mapCell(graphics->foreground, graphics->width / 8, graphics->height / 8, world_x / 8, world_y / 8);
               cacheForegroundTile(x, y, pattern, 0);
            }
            for (index = 0; index < graphics->clip_count; ++index)
            {
               if (clips[index].clip.plane)
               {
                  cacheForegroundTile(x, y, clipCell(&clips[index], world_x, world_y), 1);
               }
            }
         }
      }
      return;
   }
   for (y = top; y < top + height; y += 8)
   {
      for (x = left; x < left + width; x += 8)
      {
         world_x = origin_x + x;
         world_y = origin_y + y;
         pattern = 0;
         if (world_x >= 0 && world_y >= 0 && world_x < graphics->width && world_y < graphics->height)
         {
            pattern = mapCell(graphics->background, graphics->width / 8, graphics->height / 8, world_x / 8, world_y / 8);
         }
         paintPattern(background_cache, x, y, pattern, 0);
         if (graphics->foreground_pixels)
         {
            for (index = 0; index < graphics->clip_count; ++index)
            {
               if (!clips[index].clip.plane)
               {
                  paintPattern(background_cache, x, y, clipCell(&clips[index], world_x, world_y), 1);
               }
            }
            pattern = 0;
            if (world_x >= 0 && world_y >= 0 && world_x < graphics->width && world_y < graphics->height)
            {
               pattern = mapCell(graphics->foreground, graphics->width / 8, graphics->height / 8, world_x / 8, world_y / 8);
            }
            cacheForegroundTile(x, y, pattern, 0);
            for (index = 0; index < graphics->clip_count; ++index)
            {
               if (clips[index].clip.plane)
               {
                  cacheForegroundTile(x, y, clipCell(&clips[index], world_x, world_y), 1);
               }
            }
         }
      }
   }
}

static void __attribute__((noinline, noclone, optimize("Os"))) updateCachedClips(void)
{
   DeceptusU16 index;
   DeceptusI16 left, top, right, bottom;
   const AmigaClipState *state;
   for (index = 0; index < graphics->clip_count; ++index)
   {
      state = &clips[index];
      if (background_valid && graphics->foreground_pixels &&
          (cached_clip_frames[index] != state->frame || cached_clip_enabled[index] != state->enabled))
      {
         left = state->clip.bounds.x - origin_x;
         top = state->clip.bounds.y - origin_y;
         right = left + state->clip.bounds.width;
         bottom = top + state->clip.bounds.height;
         left = left < 0 ? 0 : left / 16 * 16;
         right = right > cache_width ? cache_width : (right + 15) / 16 * 16;
         top = top < 0 ? 0 : top;
         bottom = bottom > cache_height ? cache_height : bottom;
         if (left < right && top < bottom)
         {
            paintBackgroundStrip(left, top, right - left, bottom - top);
         }
      }
      cached_clip_frames[index] = state->frame;
      cached_clip_enabled[index] = state->enabled;
   }
}

static void updateBackgroundCache(void)
{
   DeceptusI16 dx, dy;
   if (!background_cache)
   {
      return;
   }
   dx = origin_x - background_origin_x;
   dy = origin_y - background_origin_y;
   if (!background_valid || dx <= -cache_width || dx >= cache_width || dy <= -cache_height || dy >= cache_height)
   {
      paintBackgroundStrip(0, 0, cache_width, cache_height);
   }
   else if (dx || dy)
   {
      scrollBuffer(background_cache, 5, dx, dy);
      if (graphics->foreground_pixels)
      {
         scrollBuffer(graphics->foreground_pixels, 1, dx, dy);
      }
      if (dx)
      {
         paintBackgroundStrip(dx > 0 ? cache_width - dx : 0, 0, dx > 0 ? dx : -dx, cache_height);
      }
      if (dy)
      {
         paintBackgroundStrip(0, dy > 0 ? cache_height - dy : 0, cache_width, dy > 0 ? dy : -dy);
      }
   }
   updateCachedClips();
   background_origin_x = origin_x;
   background_origin_y = origin_y;
   background_valid = 1;
}

static DeceptusU8 __attribute__((noinline, noclone, optimize("Os"))) requestSprite(void *context,
                                                                                   const DeceptusSpriteAppearance *appearance)
{
   AmigaSpriteRequest overflow, *request;
   const DeceptusEngine *engine;
   DeceptusU16 resource, identity;
   engine = (const DeceptusEngine *)context;
   if (appearance->family >= DECEPTUS_SPRITE_FAMILY_COUNT)
   {
      return 0;
   }
   resource = amiga_sprite_families[appearance->family] + appearance->frame;
   if (resource >= AMIGA_GRAPHICS_RESOURCES)
   {
      return 0;
   }
   identity = resource | (appearance->flip_x ? SPRITE_FLIPPED : 0);
   request = request_count < SPRITE_CAPACITY ? &requests[request_count] : &overflow;
   /* descriptors are immutable until reset; keep the previous slot's metadata. */
   if ((request_count >= SPRITE_CAPACITY || !request->graphic.bytes || (request->identity_flags & SPRITE_IDENTITY_MASK) != identity) &&
       !amigaGraphicsSpriteInfo(graphics, resource, appearance->flip_x, &request->graphic))
   {
      return 0;
   }
   request->identity_flags = identity | SPRITE_REDRAW;
   request->x = appearance->x + (appearance->flip_x ? request->graphic.flip_trim_x : request->graphic.trim_x);
   request->y = appearance->y + request->graphic.trim_y;
   if (request->x >= 320 || request->y >= 224 || request->x + request->graphic.width <= 0 || request->y + request->graphic.height <= 0)
   {
      return 1;
   }
   if (request_count >= SPRITE_CAPACITY)
   {
      return 0;
   }
   request->x += engine->camera_x - origin_x;
   request->y += engine->camera_y - origin_y;
   ++request_count;
   return 1;
}

static void requestBounds(const AmigaSpriteRequest *request, DeceptusRect *bounds)
{
   bounds->x = request->x;
   bounds->y = request->y;
   bounds->width = request->graphic.width;
   bounds->height = request->graphic.height;
}

static void __attribute__((noinline, noclone, optimize("Os"))) includeDamage(DeceptusRect *damage, DeceptusRect bounds, DeceptusU8 restore)
{
   DeceptusI16 right, bottom;
   right = bounds.x + bounds.width;
   bottom = bounds.y + bounds.height;
   if (restore)
   {
      bounds.x = bounds.x < 0 ? 0 : bounds.x / 16 * 16;
      bounds.y = bounds.y < 0 ? 0 : bounds.y / 8 * 8;
      right = right > cache_width ? cache_width : (right + 15) / 16 * 16;
      bottom = bottom > cache_height ? cache_height : (bottom + 7) / 8 * 8;
      bounds.width = right - bounds.x;
      bounds.height = bottom - bounds.y;
   }
   if (bounds.width <= 0 || bounds.height <= 0)
   {
      return;
   }
   if (!damage->width)
   {
      *damage = bounds;
      return;
   }
   if (right < damage->x + damage->width)
   {
      right = damage->x + damage->width;
   }
   if (bottom < damage->y + damage->height)
   {
      bottom = damage->y + damage->height;
   }
   if (bounds.x < damage->x)
   {
      damage->x = bounds.x;
   }
   if (bounds.y < damage->y)
   {
      damage->y = bounds.y;
   }
   damage->width = right - damage->x;
   damage->height = bottom - damage->y;
}

static void __attribute__((noinline, noclone, optimize("Os"))) planSpriteRedraws(DeceptusU8 reuse)
{
   DeceptusRect damage, bounds;
   const DeceptusRect *old;
   DeceptusU16 index;
   DeceptusU8 changed;
   memset(&damage, 0, sizeof(damage));
   for (index = 0; index < request_count; ++index)
   {
      requestBounds(&requests[index], &bounds);
      old = &previous_sprites[draw_buffer][index];
      if (reuse && index < previous_counts[draw_buffer] &&
          (requests[index].identity_flags & SPRITE_IDENTITY_MASK) == previous_sprite_keys[draw_buffer][index] && bounds.x == old->x &&
          bounds.y == old->y && bounds.width == old->width && bounds.height == old->height)
      {
         requests[index].identity_flags &= ~SPRITE_REDRAW;
      }
      else
      {
         includeDamage(&damage, bounds, 0);
      }
   }
   for (index = 0; index < previous_counts[draw_buffer]; ++index)
   {
      if (index >= request_count || (requests[index].identity_flags & SPRITE_REDRAW))
      {
         bounds = previous_sprites[draw_buffer][index];
         includeDamage(&damage, bounds, 1);
         paintRectangle(&bounds, 0);
      }
   }
   if (!reuse)
   {
      return;
   }
   for (index = 0; index < previous_ui_count[draw_buffer]; ++index)
   {
      includeDamage(&damage, previous_ui[draw_buffer][index], 1);
   }
   for (index = 0; index < graphics->clip_count; ++index)
   {
      if (previous_clip_frames[draw_buffer][index] != clips[index].frame ||
          previous_clip_enabled[draw_buffer][index] != clips[index].enabled)
      {
         bounds = clips[index].clip.bounds;
         bounds.x -= origin_x;
         bounds.y -= origin_y;
         includeDamage(&damage, bounds, 1);
      }
   }
   /* expand through overlapping sprites so the original draw order is retained. */
   do
   {
      changed = 0;
      for (index = 0; damage.width && index < request_count; ++index)
      {
         if (requests[index].identity_flags & SPRITE_REDRAW)
         {
            continue;
         }
         requestBounds(&requests[index], &bounds);
         if (bounds.x < damage.x + damage.width && bounds.x + bounds.width > damage.x && bounds.y < damage.y + damage.height &&
             bounds.y + bounds.height > damage.y)
         {
            requests[index].identity_flags |= SPRITE_REDRAW;
            includeDamage(&damage, bounds, 0);
            changed = 1;
         }
      }
   } while (changed);
}

static DeceptusU8 shiftedSpriteByte(const DeceptusU8 *row, DeceptusI16 column, DeceptusU8 shift)
{
   DeceptusU16 word;
   /* validated rows include a guard word; a partial leading byte starts transparent. */
   word = column < 0 ? 0 : (DeceptusU16)row[column] << 8;
   word |= row[column + 1];
   return (DeceptusU8)((DeceptusU16)(word << shift) >> 8);
}

static void clippedSprite(const AmigaSpriteRequest *request, const DeceptusU8 *pixels)
{
   const DeceptusU8 *mask, *source;
   DeceptusU8 *destination;
   DeceptusI16 left, right, top, bottom, first, last, column, source_first, source_column, y;
   DeceptusU16 plane_size, source_offset;
   DeceptusU8 shift, alpha, first_mask, last_mask, plane;
   left = request->x < 0 ? 0 : request->x;
   right = request->x + request->graphic.width;
   top = request->y < 0 ? 0 : request->y;
   bottom = request->y + request->graphic.height;
   if (right > cache_width)
   {
      right = cache_width;
   }
   if (bottom > cache_height)
   {
      bottom = cache_height;
   }
   if (left >= right || top >= bottom)
   {
      return;
   }
   first = left / 8;
   last = (right - 1) / 8;
   source_first = first * 8 - request->x;
   shift = (DeceptusU8)(source_first < 0 ? source_first + 8 : source_first & 7);
   source_first = source_first < 0 ? -1 : source_first / 8;
   first_mask = (DeceptusU8)(255U >> (left & 7));
   last_mask = (DeceptusU8)(255U << (7 - ((right - 1) & 7)));
   plane_size = request->graphic.row_bytes * request->graphic.height;
   mask = pixels + plane_size * 5;
   for (y = top; y < bottom; ++y)
   {
      source_offset = (y - request->y) * request->graphic.row_bytes;
      source_column = source_first;
      for (column = first; column <= last; ++column, ++source_column)
      {
         alpha = shiftedSpriteByte(mask + source_offset, source_column, shift);
         if (column == first)
         {
            alpha &= first_mask;
         }
         if (column == last)
         {
            alpha &= last_mask;
         }
         if (!alpha)
         {
            continue;
         }
         source = pixels + source_offset;
         destination = (DeceptusU8 *)buffers[draw_buffer] + y * row_bytes + column;
         for (plane = 0; plane < 5; ++plane)
         {
            *destination = (*destination & (DeceptusU8)~alpha) | (shiftedSpriteByte(source, source_column, shift) & alpha);
            source += plane_size;
            destination += plane_bytes;
         }
      }
   }
}

static void drawSprite(const AmigaSpriteRequest *request)
{
   DeceptusI16 first, top, height;
   DeceptusU16 plane, words, shift;
   DeceptusI16 plane_size;
   const DeceptusU8 *pixels, *mask;
   DeceptusU16 *destination;
   waitBlitter();
   pixels = amigaSpriteCachePixels(&sprite_cache, graphics, &request->graphic, (DeceptusU8 *)sprite_pixels, sizeof(sprite_pixels));
   if (!pixels)
   {
      error = 1;
      return;
   }
   first = request->x < 0 ? -((-request->x + 15) / 16) : request->x / 16;
   words = request->graphic.row_bytes / 2;
   if (first < 0 || first + words > row_words)
   {
      clippedSprite(request, pixels);
      return;
   }
   top = request->y < 0 ? -request->y : 0;
   height = request->graphic.height - top;
   if (request->y + top + height > cache_height)
   {
      height = cache_height - request->y - top;
   }
   if (height <= 0)
   {
      return;
   }
   plane_size = (DeceptusU32)request->graphic.row_bytes * request->graphic.height;
   pixels += top * request->graphic.row_bytes;
   mask = pixels + plane_size * 5;
   destination = buffers[draw_buffer] + (request->y + top) * row_words + first;
   shift = (request->x & 15) << 12;
   hardware->bltcon0 = shift | SRCA | SRCB | SRCC | DEST | 0xca;
   hardware->bltcon1 = shift;
   hardware->bltafwm = 65535U;
   hardware->bltalwm = 0;
   hardware->bltamod = hardware->bltbmod = 0;
   hardware->bltcmod = hardware->bltdmod = row_bytes - words * 2;
   for (plane = 0; plane < 5; ++plane)
   {
      waitBlitter();
      hardware->bltapt = (APTR)mask;
      hardware->bltbpt = (APTR)(pixels + plane * plane_size);
      hardware->bltcpt = destination + plane * plane_words;
      hardware->bltdpt = destination + plane * plane_words;
      hardware->bltsize = (height << 6) | words;
   }
}

static void copperWrite(DeceptusU16 **cursor, DeceptusU16 reg, DeceptusU16 value)
{
   *(*cursor)++ = reg;
   *(*cursor)++ = value;
}

static void prepareCopper(DeceptusU16 buffer, const DeceptusFrame *frame)
{
   DeceptusU16 *cursor;
   DeceptusU16 plane, delay;
   DeceptusU32 address;
   cursor = copper[buffer];
   copperWrite(&cursor, offsetof(struct Custom, diwstrt), 0x3c81);
   copperWrite(&cursor, offsetof(struct Custom, diwstop), 0x1cc1);
   copperWrite(&cursor, offsetof(struct Custom, ddfstrt), 0x0030);
   copperWrite(&cursor, offsetof(struct Custom, ddfstop), 0x00d0);
   copperWrite(&cursor, offsetof(struct Custom, bplcon0), 0x5200);
   delay = (16 - (frame->camera_x & 15)) & 15;
   copperWrite(&cursor, offsetof(struct Custom, bplcon1), delay | (delay << 4));
   copperWrite(&cursor, offsetof(struct Custom, bplcon2), 0);
   copperWrite(&cursor, offsetof(struct Custom, bpl1mod), row_bytes - 42);
   copperWrite(&cursor, offsetof(struct Custom, bpl2mod), row_bytes - 42);
   for (plane = 0; plane < 5; ++plane)
   {
      address = (DeceptusU32)(buffers[buffer] + plane * plane_words + (frame->camera_y - origins_y[buffer]) * row_words +
                              ((frame->camera_x - origins_x[buffer] + 15) / 16 - 1));
      copperWrite(&cursor, offsetof(struct Custom, bplpt) + plane * 4, address >> 16);
      copperWrite(&cursor, offsetof(struct Custom, bplpt) + plane * 4 + 2, (DeceptusU16)address);
   }
   for (plane = 0; plane < 32; ++plane)
   {
      copperWrite(&cursor, offsetof(struct Custom, color) + plane * 2, palette[plane]);
   }
   copperWrite(&cursor, 0xffff, 0xfffe);
}

static DeceptusI16 scrollOrigin(DeceptusI16 current, DeceptusI16 camera, DeceptusU16 maximum, DeceptusU8 quantum, DeceptusU8 minimum,
                                DeceptusU8 headroom, DeceptusU8 valid_origin)
{
   if (!valid_origin || camera - current < minimum || camera - current > maximum)
   {
      current = (camera / quantum) * quantum - ((minimum + (maximum - minimum) * headroom / 4) / quantum) * quantum;
      /* rounding the origin down must not expose rows beyond the backing buffer. */
      if (camera - current > maximum)
      {
         current += quantum;
      }
   }
   return current;
}

DeceptusU8 amigaOpeningVideoDraw(const DeceptusAdventure *game, const DeceptusFrame *frame)
{
   DeceptusI16 next_x, next_y, dx, dy, maximum_x, maximum_y;
   DeceptusU8 horizontal_headroom, vertical_headroom;
   DeceptusU16 index;
   DeceptusRect rectangle;
   DeceptusU8 was_valid, ui_layout;
   DeceptusRect covered;
   if (error)
   {
      return 0;
   }
   if (!active)
   {
      /* own the blitter before any room draws, while preserving a paused load clock. */
      loading = 1;
      amigaSystemTakeDisplay(verticalBlank, blank_copper);
      active = 1;
   }
   if (active && valid[draw_buffer] && game->campaign.mode != DECEPTUS_CAMPAIGN_PLAYING && last_ticks == game->campaign.engine.ticks &&
       last_mode == game->campaign.mode && last_selection == game->selection && last_page == game->page && last_flags == game->flags &&
       last_dialogue == game->dialogue && last_prompt == game->prompt && last_label == (game->room_label_ticks != 0))
   {
      waitFrame();
      return 1;
   }
   /* let simulation overlap the previous page's refresh wait, then release its buffer. */
   while (pending_buffer >= 0)
   {
   }
   __asm__ volatile("" ::: "memory");
   draw_buffer ^= 1;
   origin_x = origins_x[draw_buffer];
   origin_y = origins_y[draw_buffer];
   updateClips(game);
   was_valid = valid[draw_buffer];
   if (graphics->native_foreground)
   {
      /* native pages have their own complete source; keep a usable origin until it needs rebasing. */
      window_origin_x = origin_x;
      window_origin_y = origin_y;
      window_valid = was_valid;
   }
   ui_layout =
      (game->room_label_ticks != 0) | (game->prompt < game->definition->rooms[game->campaign.room_index].interaction_count ? 2 : 0);
   horizontal_headroom = frame->camera_x < previous_camera_x[draw_buffer] ? 3 : 1;
   next_x = scrollOrigin(window_origin_x, frame->camera_x, cache_width - 320, 16, 16, horizontal_headroom, window_valid);
   maximum_x = graphics->width > cache_width ? ((graphics->width - cache_width + 15) / 16) * 16 : 0;
   if (next_x < -16)
   {
      next_x = -16;
   }
   if (next_x > maximum_x)
   {
      next_x = maximum_x;
   }
   vertical_headroom = window_valid && frame->camera_y > previous_camera_y[draw_buffer] ? 1 : 4;
   next_y = scrollOrigin(window_origin_y, frame->camera_y, cache_height - 224, 8, 0, vertical_headroom, window_valid);
   maximum_y = graphics->height > cache_height ? graphics->height - cache_height : 0;
   if (next_y < 0)
   {
      next_y = 0;
   }
   if (next_y > maximum_y)
   {
      next_y = maximum_y;
   }
   window_origin_x = next_x;
   window_origin_y = next_y;
   window_valid = 1;
   dx = next_x - origin_x;
   dy = next_y - origin_y;
   request_count = 0;
   if (!deceptusAppearanceVisit(game, frame, &amiga_appearance_definition, &animation, requestSprite, (void *)&game->campaign.engine))
   {
      error = 1;
   }
   if (!was_valid || dx <= -cache_width || dx >= cache_width || dy <= -cache_height || dy >= cache_height)
   {
      was_valid = 0;
   }
   else if (dx || dy)
   {
      scrollBuffer(buffers[draw_buffer], 5, dx, dy);
   }
   for (index = 0; index < request_count; ++index)
   {
      requests[index].x -= dx;
      requests[index].y -= dy;
   }
   origin_x = origins_x[draw_buffer] = next_x;
   origin_y = origins_y[draw_buffer] = next_y;
   updateBackgroundCache();
   if (was_valid)
   {
      if (dx || dy)
      {
         for (index = 0; index < previous_counts[draw_buffer]; ++index)
         {
            previous_sprites[draw_buffer][index].x -= dx;
            previous_sprites[draw_buffer][index].y -= dy;
         }
         for (index = 0; index < previous_ui_count[draw_buffer]; ++index)
         {
            previous_ui[draw_buffer][index].x -= dx;
            previous_ui[draw_buffer][index].y -= dy;
         }
      }
      planSpriteRedraws(!dx && !dy);
      for (index = 0; index < previous_ui_count[draw_buffer]; ++index)
      {
         if (previous_ui_mode[draw_buffer] == game->campaign.mode && previous_ui_layout[draw_buffer] == ui_layout)
         {
            /* the next opaque UI box covers this intersection after the camera scroll. */
            covered = previous_ui[draw_buffer][index];
            covered.x += frame->camera_x - previous_camera_x[draw_buffer];
            covered.y += frame->camera_y - previous_camera_y[draw_buffer];
            restoreOutside(&previous_ui[draw_buffer][index], &covered);
         }
         else
         {
            paintRectangle(&previous_ui[draw_buffer][index], 0);
         }
      }
   }
   if (!was_valid)
   {
      rectangle.x = rectangle.y = 0;
      rectangle.width = cache_width;
      rectangle.height = cache_height;
      paintRectangle(&rectangle, 0);
   }
   else
   {
      if (dx)
      {
         rectangle.x = dx > 0 ? cache_width - dx : 0;
         rectangle.y = 0;
         rectangle.width = dx > 0 ? dx : -dx;
         rectangle.height = cache_height;
         paintRectangle(&rectangle, 0);
      }
      if (dy)
      {
         rectangle.x = 0;
         rectangle.y = dy > 0 ? cache_height - dy : 0;
         rectangle.width = cache_width;
         rectangle.height = dy > 0 ? dy : -dy;
         paintRectangle(&rectangle, 0);
      }
   }
   for (index = 0; index < graphics->clip_count; ++index)
   {
      if (was_valid && (previous_clip_frames[draw_buffer][index] != clips[index].frame ||
                        previous_clip_enabled[draw_buffer][index] != clips[index].enabled))
      {
         rectangle = clips[index].clip.bounds;
         rectangle.x -= origin_x;
         rectangle.y -= origin_y;
         paintRectangle(&rectangle, 0);
      }
      previous_clip_frames[draw_buffer][index] = clips[index].frame;
      previous_clip_enabled[draw_buffer][index] = clips[index].enabled;
   }
   for (index = request_count; index; --index)
   {
      if (requests[index - 1].identity_flags & SPRITE_REDRAW)
      {
         drawSprite(&requests[index - 1]);
      }
   }
   waitBlitter();
   for (index = 0; index < request_count; ++index)
   {
      rectangle.x = requests[index].x;
      rectangle.y = requests[index].y;
      rectangle.width = requests[index].graphic.width;
      rectangle.height = requests[index].graphic.height;
      previous_sprites[draw_buffer][index] = rectangle;
      previous_sprite_keys[draw_buffer][index] = requests[index].identity_flags & SPRITE_IDENTITY_MASK;
      if (requests[index].identity_flags & SPRITE_REDRAW)
      {
         paintRectangle(&rectangle, 1);
      }
   }
   previous_counts[draw_buffer] = request_count;
   previous_ui_count[draw_buffer] = amigaOpeningUiDraw(game, (DeceptusU8 *)buffers[draw_buffer], row_bytes, plane_bytes,
                                                       frame->camera_x - origin_x, frame->camera_y - origin_y, previous_ui[draw_buffer]);
   previous_ui_mode[draw_buffer] = game->campaign.mode;
   previous_ui_layout[draw_buffer] = ui_layout;
   previous_camera_x[draw_buffer] = frame->camera_x;
   previous_camera_y[draw_buffer] = frame->camera_y;
   valid[draw_buffer] = 1;
   loading = 0;
   if (error)
   {
      return 0;
   }
   prepareCopper(draw_buffer, frame);
   /* publish a complete list; the interrupt swaps pages at its vertical blank. */
   __asm__ volatile("" ::: "memory");
   present_frame = next_present;
   pending_buffer = draw_buffer;
   last_ticks = game->campaign.engine.ticks;
   last_flags = game->flags;
   last_mode = game->campaign.mode;
   last_selection = game->selection;
   last_page = game->page;
   last_prompt = game->prompt;
   last_dialogue = game->dialogue;
   last_label = game->room_label_ticks != 0;
   return 1;
}
