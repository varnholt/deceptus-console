#include "opening_video.h"
#include "amiga_opening_assets.h"
#include "deceptus/appearance.h"
#include "opening_ui.h"
#include "rooms.h"
#include "spritecache.h"
#include "system.h"
#include <hardware/blit.h>
#include <hardware/custom.h>
#include <hardware/intbits.h>
#include <stddef.h>
#include <string.h>

#define BASE_CACHE_WIDTH 352
#define BASE_CACHE_HEIGHT 232
#define BASE_BUFFER_WORDS ((BASE_CACHE_WIDTH / 16) * BASE_CACHE_HEIGHT * 5)
#if (AMIGA_LARGE_CACHE_WIDTH / 8) * AMIGA_LARGE_CACHE_HEIGHT * 5 > BASE_BUFFER_WORDS * 4
#error the background reference must fit the inactive compact buffers
#endif
#define SPRITE_CAPACITY 96
#define TILE_CACHE_SLOTS 128
#define SPRITE_CACHE_BYTES (32UL * 1024)
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
static DeceptusU16 cache_width, cache_height, row_bytes, row_words;
static DeceptusU32 plane_bytes, plane_words;
static DeceptusU16 copper[2][128] CHIP;
static DeceptusU16 blank_copper[6] CHIP = {0x0100, 0, 0x0180, 0, 0xffff, 0xfffe};
static DeceptusU16 sprite_pixels[(AMIGA_SPRITE_SCRATCH_BYTES + 1) / 2] CHIP;
static DeceptusU16 palette[32];
static AmigaSpriteCache sprite_cache;
static DeceptusU8 *tile_cache, *tile_directory;
static DeceptusU16 tile_directory_count;
static DeceptusU16 tile_slots;
static DeceptusU16 tile_groups[TILE_CACHE_SLOTS];
static AmigaRowCache base_rows[ROW_CACHE_WAYS];
static AmigaRowCache *row_cache = base_rows;
static DeceptusU8 row_sets, row_cursors[ROW_CACHE_SETS];
static DeceptusU8 tile_cursor;
static AmigaClipState clips[AMIGA_OPENING_CLIPS];
static DeceptusU16 previous_clip_frames[2][AMIGA_OPENING_CLIPS];
static DeceptusU8 previous_clip_enabled[2][AMIGA_OPENING_CLIPS];
static AmigaSpriteRequest requests[SPRITE_CAPACITY];
static DeceptusU16 request_count;
static DeceptusRect previous_sprites[2][SPRITE_CAPACITY];
static DeceptusU16 previous_counts[2];
static DeceptusRect previous_ui[2][2];
static DeceptusU8 previous_ui_count[2], previous_ui_mode[2], previous_ui_layout[2];
static DeceptusI16 previous_camera_x[2], previous_camera_y[2];
static DeceptusI16 origins_x[2], origins_y[2], origin_x, origin_y;
static DeceptusU8 valid[2], active, draw_buffer, error;
static const AmigaGraphics *graphics;
static volatile DeceptusU32 video_frames;
static volatile DeceptusU8 loading;
static DeceptusPlayerAnimation animation = DECEPTUS_PLAYER_ANIMATION_INITIALIZER;
static DeceptusU16 last_ticks, last_flags;
static DeceptusU8 last_mode, last_selection, last_page, last_prompt, last_label;
static const DeceptusInteraction *last_dialogue;

static void __attribute__((interrupt)) verticalBlank(void)
{
   if (!loading)
   {
      ++video_frames;
   }
   hardware->intreq = INTF_VERTB;
   hardware->intreq = INTF_VERTB;
}

DeceptusU32 amigaOpeningVideoFrames(void)
{
   return video_frames;
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
   (void)hardware->dmaconr;
   while (hardware->dmaconr & 0x4000)
   {
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

static DeceptusU8 prepareCaches(void)
{
   DeceptusU32 size, offset, sprite_size, chip_size, unused;
   DeceptusU16 slot;
   DeceptusU8 *sprite_memory, *chip_memory, *row_memory;
   DeceptusU32 row_cache_bytes;
   DeceptusU8 rows_in_sprite_bank, sprite_in_chip, tile_in_chip;
   row_cache = base_rows;
   row_sets = 1;
   rows_in_sprite_bank = 0;
   sprite_in_chip = 1;
   tile_in_chip = 0;
   tile_directory = amigaRoomScratch(&size);
   chip_memory = amigaRoomChipScratch(&chip_size);
   sprite_memory = chip_memory;
   sprite_size = chip_size;
   if (chip_size > size)
   {
      sprite_memory = tile_directory;
      sprite_size = size;
      sprite_in_chip = 0;
      tile_in_chip = 1;
      tile_directory = chip_memory;
      size = chip_size;
   }
   tile_directory_count = (DeceptusU16)(((DeceptusU32)graphics->pattern_count + 15) / 16);
   offset = ((DeceptusU32)tile_directory_count + 3) & ~3UL;
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
   unused = size - offset - (DeceptusU32)tile_slots * AMIGA_TILE_GROUP_BYTES;
   row_memory = tile_cache + (DeceptusU32)tile_slots * AMIGA_TILE_GROUP_BYTES;
   row_cache_bytes = unused;
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
   amigaSpriteCacheInit(&sprite_cache, sprite_memory, sprite_size, sprite_in_chip);
   memset(tile_directory, 255, tile_directory_count);
   for (slot = 0; slot < tile_slots; ++slot)
   {
      tile_groups[slot] = 65535U;
   }
   tile_cursor = 0;
   return 1;
}

void amigaOpeningVideoReset(void)
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
      buffers[buffer] = graphics->viewport_pixels ? graphics->viewport_pixels + (DeceptusU32)buffer * plane_words * 5
                                                  : base_buffers + (DeceptusU32)buffer * BASE_BUFFER_WORDS;
   }
   background_cache = graphics->viewport_pixels ? base_buffers : 0;
   background_valid = window_valid = 0;
   error = !prepareCaches() || !amigaGraphicsPalette(graphics, palette) || graphics->clip_count > AMIGA_OPENING_CLIPS;
   valid[0] = valid[1] = 0;
   previous_counts[0] = previous_counts[1] = 0;
   previous_ui_count[0] = previous_ui_count[1] = 0;
   memset(row_cache, 0, (DeceptusU32)row_sets * ROW_CACHE_WAYS * sizeof(*row_cache));
   memset(row_cursors, 0, sizeof(row_cursors));
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

static DeceptusU16 mapCell(DeceptusU32 map, DeceptusU16 columns, DeceptusU16 rows, DeceptusU16 column, DeceptusU16 row)
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
   const DeceptusU8 *pixels;
   DeceptusU8 *destination;
   DeceptusU8 plane, row, mask;
   if (!pattern && masked)
   {
      return;
   }
   pixels = tilePixels(pattern);
   if (!pixels)
   {
      return;
   }
   for (plane = 0; plane < 5; ++plane)
   {
      destination = (DeceptusU8 *)buffer + (DeceptusU32)plane * plane_bytes + y * row_bytes + x / 8;
      for (row = 0; row < 8; ++row)
      {
         mask = pixels[40 + row];
         if (masked)
         {
            *destination = (DeceptusU8)((*destination & ~mask) | (pixels[plane * 8 + row] & mask));
         }
         else
         {
            *destination = pixels[plane * 8 + row];
         }
         destination += row_bytes;
      }
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

static DeceptusU8 drawNativeBackground(DeceptusI16 left, DeceptusI16 top, DeceptusI16 right, DeceptusI16 bottom)
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
   if (!source || source_x < 0 || source_y < 0 || source_x + right - left > source_words * 16 || source_y + height > source_height)
   {
      return 0;
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

static void paintRectangle(const DeceptusRect *rectangle, DeceptusU8 foreground_only)
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
   if (!foreground_only && (graphics->background_pixels || background_valid))
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

static void scrollBuffer(DeceptusU16 *pixels, DeceptusI16 dx, DeceptusI16 dy)
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
   for (plane = 0; plane < 5; ++plane)
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
static void __attribute__((noinline, noclone)) paintBackgroundStrip(DeceptusI16 left, DeceptusI16 top, DeceptusI16 width,
                                                                    DeceptusI16 height)
{
   DeceptusI16 x, y, world_x, world_y;
   DeceptusU16 pattern;
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
      }
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
      scrollBuffer(background_cache, dx, dy);
      if (dx)
      {
         paintBackgroundStrip(dx > 0 ? cache_width - dx : 0, 0, dx > 0 ? dx : -dx, cache_height);
      }
      if (dy)
      {
         paintBackgroundStrip(0, dy > 0 ? cache_height - dy : 0, cache_width, dy > 0 ? dy : -dy);
      }
   }
   background_origin_x = origin_x;
   background_origin_y = origin_y;
   background_valid = 1;
}

static DeceptusU8 requestSprite(void *context, const DeceptusSpriteAppearance *appearance)
{
   AmigaSpriteRequest request;
   const DeceptusEngine *engine;
   DeceptusU16 resource;
   engine = (const DeceptusEngine *)context;
   if (appearance->family >= DECEPTUS_SPRITE_FAMILY_COUNT)
   {
      return 0;
   }
   resource = amiga_sprite_families[appearance->family] + appearance->frame;
   if (!amigaGraphicsSpriteInfo(graphics, resource, appearance->flip_x, &request.graphic))
   {
      return 0;
   }
   request.x = appearance->x + (appearance->flip_x ? request.graphic.flip_trim_x : request.graphic.trim_x);
   request.y = appearance->y + request.graphic.trim_y;
   if (request.x >= 320 || request.y >= 224 || request.x + request.graphic.width <= 0 || request.y + request.graphic.height <= 0)
   {
      return 1;
   }
   if (request_count >= SPRITE_CAPACITY)
   {
      return 0;
   }
   request.x += engine->camera_x - origin_x;
   request.y += engine->camera_y - origin_y;
   requests[request_count++] = request;
   return 1;
}

static void clippedSprite(const AmigaSpriteRequest *request, const DeceptusU8 *pixels)
{
   const DeceptusU8 *mask;
   DeceptusU8 *destination;
   DeceptusU16 x, y, plane;
   DeceptusI16 px, py;
   DeceptusU32 plane_size, offset;
   DeceptusU8 bit, target_bit;
   plane_size = (DeceptusU32)request->graphic.row_bytes * request->graphic.height;
   mask = pixels + plane_size * 5;
   for (y = 0; y < request->graphic.height; ++y)
   {
      py = request->y + y;
      if (py < 0 || py >= cache_height)
      {
         continue;
      }
      for (x = 0; x < request->graphic.width; ++x)
      {
         px = request->x + x;
         if (px < 0 || px >= cache_width)
         {
            continue;
         }
         offset = y * request->graphic.row_bytes + x / 8;
         bit = (DeceptusU8)(128U >> (x & 7));
         if (!(mask[offset] & bit))
         {
            continue;
         }
         target_bit = (DeceptusU8)(128U >> (px & 7));
         for (plane = 0; plane < 5; ++plane)
         {
            destination = (DeceptusU8 *)buffers[draw_buffer] + plane * plane_bytes + py * row_bytes + px / 8;
            if (pixels[plane * plane_size + offset] & bit)
            {
               *destination |= target_bit;
            }
            else
            {
               *destination &= (DeceptusU8)~target_bit;
            }
         }
      }
   }
}

static void drawSprite(const AmigaSpriteRequest *request)
{
   DeceptusI16 first, top, height;
   DeceptusU16 plane, words, shift;
   DeceptusU32 plane_size;
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
                                DeceptusU8 valid_origin)
{
   if (!valid_origin || camera - current < minimum || camera - current > maximum)
   {
      return (camera / quantum) * quantum - ((maximum + minimum) / 2 / quantum) * quantum;
   }
   return current;
}

DeceptusU8 amigaOpeningVideoDraw(const DeceptusAdventure *game, const DeceptusFrame *frame)
{
   DeceptusI16 next_x, next_y, dx, dy;
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
   draw_buffer ^= 1;
   origin_x = origins_x[draw_buffer];
   origin_y = origins_y[draw_buffer];
   updateClips(game);
   was_valid = valid[draw_buffer];
   ui_layout =
      (game->room_label_ticks != 0) | (game->prompt < game->definition->rooms[game->campaign.room_index].interaction_count ? 2 : 0);
   if (was_valid)
   {
      for (index = 0; index < previous_counts[draw_buffer]; ++index)
      {
         paintRectangle(&previous_sprites[draw_buffer][index], 0);
      }
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
   next_x = scrollOrigin(window_origin_x, frame->camera_x, cache_width - 320, 16, 16, window_valid);
   next_y = scrollOrigin(window_origin_y, frame->camera_y, cache_height - 224, 8, 0, window_valid);
   window_origin_x = next_x;
   window_origin_y = next_y;
   window_valid = 1;
   dx = next_x - origin_x;
   dy = next_y - origin_y;
   if (!was_valid || dx <= -cache_width || dx >= cache_width || dy <= -cache_height || dy >= cache_height)
   {
      was_valid = 0;
   }
   else if (dx || dy)
   {
      scrollBuffer(buffers[draw_buffer], dx, dy);
   }
   origin_x = origins_x[draw_buffer] = next_x;
   origin_y = origins_y[draw_buffer] = next_y;
   updateBackgroundCache();
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
   request_count = 0;
   if (!deceptusAppearanceVisit(game, frame, &amiga_appearance_definition, &animation, requestSprite, (void *)&game->campaign.engine))
   {
      error = 1;
   }
   for (index = request_count; index; --index)
   {
      drawSprite(&requests[index - 1]);
   }
   waitBlitter();
   for (index = 0; index < request_count; ++index)
   {
      rectangle.x = requests[index].x;
      rectangle.y = requests[index].y;
      rectangle.width = requests[index].graphic.width;
      rectangle.height = requests[index].graphic.height;
      previous_sprites[draw_buffer][index] = rectangle;
      paintRectangle(&rectangle, 1);
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
   waitFrame();
   hardware->cop1lc = (ULONG)copper[draw_buffer];
   hardware->copjmp1 = 0;
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
