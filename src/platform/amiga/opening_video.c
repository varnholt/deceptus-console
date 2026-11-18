#include "opening_video.h"
#include "amiga_opening_assets.h"
#include "deceptus/appearance.h"
#include "opening_ui.h"
#include "rooms.h"
#include "system.h"
#include <hardware/blit.h>
#include <hardware/custom.h>
#include <hardware/intbits.h>
#include <stddef.h>
#include <string.h>

#define CACHE_WIDTH 352
#define CACHE_HEIGHT 232
#define ROW_BYTES (CACHE_WIDTH / 8)
#define ROW_WORDS (ROW_BYTES / 2)
#define PLANE_BYTES ((DeceptusU32)ROW_BYTES * CACHE_HEIGHT)
#define PLANE_WORDS (PLANE_BYTES / 2)
#define BUFFER_WORDS (PLANE_WORDS * 5)
#define SPRITE_CAPACITY 96
#define TILE_CACHE_SLOTS 4
#define ROW_CACHE_SLOTS 4
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
   DeceptusU8 flip;
} AmigaSpriteRequest;

typedef struct AmigaClipState
{
   AmigaGraphicsClip clip;
   DeceptusU16 frame;
   DeceptusU8 enabled;
} AmigaClipState;

static volatile struct Custom *const hardware = (struct Custom *)0xdff000;
static DeceptusU16 buffers[2][BUFFER_WORDS] CHIP;
static DeceptusU16 copper[2][128] CHIP;
static DeceptusU16 sprite_pixels[(AMIGA_SPRITE_SCRATCH_BYTES + 1) / 2] CHIP;
static DeceptusU16 palette[32];
static DeceptusU8 tile_cache[TILE_CACHE_SLOTS][AMIGA_TILE_GROUP_BYTES];
static DeceptusU16 tile_groups[TILE_CACHE_SLOTS];
static AmigaRowCache row_cache[ROW_CACHE_SLOTS];
static DeceptusU8 tile_cursor, row_cursor;
static AmigaClipState clips[AMIGA_OPENING_CLIPS];
static DeceptusU16 previous_clip_frames[2][AMIGA_OPENING_CLIPS];
static DeceptusU8 previous_clip_enabled[2][AMIGA_OPENING_CLIPS];
static AmigaSpriteRequest requests[SPRITE_CAPACITY];
static DeceptusU16 request_count;
static DeceptusRect previous_sprites[2][SPRITE_CAPACITY];
static DeceptusU16 previous_counts[2];
static DeceptusRect previous_ui[2][2];
static DeceptusU8 previous_ui_count[2];
static DeceptusI16 origins_x[2], origins_y[2], origin_x, origin_y;
static DeceptusU8 valid[2], active, draw_buffer, error;
static const AmigaGraphics *graphics;
static volatile DeceptusU32 video_frames;
static DeceptusPlayerAnimation animation = DECEPTUS_PLAYER_ANIMATION_INITIALIZER;
static DeceptusU16 last_ticks, last_flags;
static DeceptusU8 last_mode, last_selection, last_page, last_prompt, last_label;
static const DeceptusInteraction *last_dialogue;

static void __attribute__((interrupt)) verticalBlank(void)
{
   ++video_frames;
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

void amigaOpeningVideoReset(void)
{
   DeceptusU8 index;
   graphics = amigaRoomGraphics();
   error = !amigaGraphicsPalette(graphics, palette) || graphics->clip_count > AMIGA_OPENING_CLIPS;
   valid[0] = valid[1] = 0;
   previous_counts[0] = previous_counts[1] = 0;
   previous_ui_count[0] = previous_ui_count[1] = 0;
   memset(row_cache, 0, sizeof(row_cache));
   for (index = 0; index < TILE_CACHE_SLOTS; ++index)
   {
      tile_groups[index] = 65535U;
   }
   animation.previous_pose = 255;
}

static const DeceptusU8 *tilePixels(DeceptusU16 pattern)
{
   DeceptusU16 group;
   DeceptusU8 slot;
   group = pattern / AMIGA_TILE_GROUP;
   for (slot = 0; slot < TILE_CACHE_SLOTS; ++slot)
   {
      if (tile_groups[slot] == group)
      {
         return tile_cache[slot] + (pattern % AMIGA_TILE_GROUP) * AMIGA_TILE_BYTES;
      }
   }
   slot = tile_cursor++ % TILE_CACHE_SLOTS;
   if (!amigaGraphicsTileGroup(graphics, group, tile_cache[slot]))
   {
      error = 1;
      return 0;
   }
   tile_groups[slot] = group;
   return tile_cache[slot] + (pattern % AMIGA_TILE_GROUP) * AMIGA_TILE_BYTES;
}

static DeceptusU16 mapCell(DeceptusU32 map, DeceptusU16 columns, DeceptusU16 rows, DeceptusU16 column, DeceptusU16 row)
{
   AmigaRowCache *entry;
   DeceptusU8 slot;
   if (column >= columns || row >= rows)
   {
      return 0;
   }
   for (slot = 0; slot < ROW_CACHE_SLOTS; ++slot)
   {
      entry = &row_cache[slot];
      if (entry->valid && entry->map == map && entry->row == row && entry->columns == columns && entry->rows == rows)
      {
         return entry->cells[column];
      }
   }
   entry = &row_cache[row_cursor++ % ROW_CACHE_SLOTS];
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

static void paintPattern(DeceptusI16 x, DeceptusI16 y, DeceptusU16 pattern, DeceptusU8 masked)
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
      destination = (DeceptusU8 *)buffers[draw_buffer] + (DeceptusU32)plane * PLANE_BYTES + y * ROW_BYTES + x / 8;
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
         destination += ROW_BYTES;
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

static void paintTile(DeceptusI16 x, DeceptusI16 y, DeceptusU8 foreground_only)
{
   DeceptusI16 world_x, world_y;
   DeceptusU16 pattern;
   DeceptusU8 index;
   world_x = origin_x + x;
   world_y = origin_y + y;
   pattern = 0;
   if (!foreground_only)
   {
      if (world_x >= 0 && world_y >= 0 && world_x < graphics->width && world_y < graphics->height)
      {
         pattern = mapCell(graphics->background, graphics->width / 8, graphics->height / 8, world_x / 8, world_y / 8);
      }
      paintPattern(x, y, pattern, 0);
      for (index = 0; index < graphics->clip_count; ++index)
      {
         if (!clips[index].clip.plane)
         {
            pattern = clipCell(&clips[index], world_x, world_y);
            paintPattern(x, y, pattern, 1);
         }
      }
   }
   if (world_x >= 0 && world_y >= 0 && world_x < graphics->width && world_y < graphics->height)
   {
      pattern = mapCell(graphics->foreground, graphics->width / 8, graphics->height / 8, world_x / 8, world_y / 8);
      paintPattern(x, y, pattern, 1);
   }
   for (index = 0; index < graphics->clip_count; ++index)
   {
      if (clips[index].clip.plane)
      {
         pattern = clipCell(&clips[index], world_x, world_y);
         paintPattern(x, y, pattern, 1);
      }
   }
}

static void paintRectangle(const DeceptusRect *rectangle, DeceptusU8 foreground_only)
{
   DeceptusI16 x, y, left, right, top, bottom;
   left = rectangle->x < 0 ? 0 : (rectangle->x / 8) * 8;
   top = rectangle->y < 0 ? 0 : (rectangle->y / 8) * 8;
   right = (rectangle->x + rectangle->width + 7) / 8 * 8;
   bottom = (rectangle->y + rectangle->height + 7) / 8 * 8;
   if (right > CACHE_WIDTH)
   {
      right = CACHE_WIDTH;
   }
   if (bottom > CACHE_HEIGHT)
   {
      bottom = CACHE_HEIGHT;
   }
   for (y = top; y < bottom; y += 8)
   {
      for (x = left; x < right; x += 8)
      {
         paintTile(x, y, foreground_only);
      }
   }
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

static void scrollBuffer(DeceptusI16 dx, DeceptusI16 dy)
{
   DeceptusI16 source_x, source_y, target_x, target_y, width, height;
   DeceptusU16 plane;
   DeceptusU8 reverse;
   DeceptusU16 *source, *destination;
   source_x = dx > 0 ? dx / 16 : 0;
   target_x = dx < 0 ? -dx / 16 : 0;
   source_y = dy > 0 ? dy : 0;
   target_y = dy < 0 ? -dy : 0;
   width = ROW_WORDS - (dx < 0 ? -dx : dx) / 16;
   height = CACHE_HEIGHT - (dy < 0 ? -dy : dy);
   reverse = target_y * ROW_WORDS + target_x > source_y * ROW_WORDS + source_x;
   waitBlitter();
   hardware->bltcon0 = SRCA | DEST | A_TO_D;
   hardware->bltcon1 = reverse ? BLITREVERSE : 0;
   hardware->bltafwm = hardware->bltalwm = 65535U;
   hardware->bltamod = hardware->bltdmod = ROW_BYTES - width * 2;
   for (plane = 0; plane < 5; ++plane)
   {
      source = buffers[draw_buffer] + plane * PLANE_WORDS + source_y * ROW_WORDS + source_x;
      destination = buffers[draw_buffer] + plane * PLANE_WORDS + target_y * ROW_WORDS + target_x;
      if (reverse)
      {
         source += (height - 1) * ROW_WORDS + width - 1;
         destination += (height - 1) * ROW_WORDS + width - 1;
      }
      waitBlitter();
      hardware->bltapt = source;
      hardware->bltdpt = destination;
      hardware->bltsize = (height << 6) | width;
   }
   waitBlitter();
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
   if (!amigaGraphicsSpriteInfo(graphics, resource, &request.graphic))
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
   request.flip = appearance->flip_x;
   requests[request_count++] = request;
   return 1;
}

static void clippedSprite(const AmigaSpriteRequest *request)
{
   const DeceptusU8 *pixels, *mask;
   DeceptusU8 *destination;
   DeceptusU16 x, y, plane;
   DeceptusI16 px, py;
   DeceptusU32 plane_size, offset;
   DeceptusU8 bit, target_bit;
   plane_size = (DeceptusU32)request->graphic.row_bytes * request->graphic.height;
   pixels = (const DeceptusU8 *)sprite_pixels;
   mask = pixels + plane_size * 5;
   for (y = 0; y < request->graphic.height; ++y)
   {
      py = request->y + y;
      if (py < 0 || py >= CACHE_HEIGHT)
      {
         continue;
      }
      for (x = 0; x < request->graphic.width; ++x)
      {
         px = request->x + x;
         if (px < 0 || px >= CACHE_WIDTH)
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
            destination = (DeceptusU8 *)buffers[draw_buffer] + plane * PLANE_BYTES + py * ROW_BYTES + px / 8;
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
   if (!amigaGraphicsSpriteDecode(graphics, &request->graphic, (DeceptusU8 *)sprite_pixels, sizeof(sprite_pixels)))
   {
      error = 1;
      return;
   }
   if (request->flip)
   {
      amigaGraphicsSpriteFlip((DeceptusU8 *)sprite_pixels, &request->graphic);
   }
   first = request->x < 0 ? -((-request->x + 15) / 16) : request->x / 16;
   words = request->graphic.row_bytes / 2;
   if (first < 0 || first + words > ROW_WORDS)
   {
      clippedSprite(request);
      return;
   }
   top = request->y < 0 ? -request->y : 0;
   height = request->graphic.height - top;
   if (request->y + top + height > CACHE_HEIGHT)
   {
      height = CACHE_HEIGHT - request->y - top;
   }
   if (height <= 0)
   {
      return;
   }
   plane_size = (DeceptusU32)request->graphic.row_bytes * request->graphic.height;
   pixels = (const DeceptusU8 *)sprite_pixels + top * request->graphic.row_bytes;
   mask = pixels + plane_size * 5;
   destination = buffers[draw_buffer] + (request->y + top) * ROW_WORDS + first;
   shift = (request->x & 15) << 12;
   hardware->bltcon0 = shift | SRCA | SRCB | SRCC | DEST | 0xca;
   hardware->bltcon1 = shift;
   hardware->bltafwm = 65535U;
   hardware->bltalwm = 0;
   hardware->bltamod = hardware->bltbmod = 0;
   hardware->bltcmod = hardware->bltdmod = ROW_BYTES - words * 2;
   for (plane = 0; plane < 5; ++plane)
   {
      waitBlitter();
      hardware->bltapt = (APTR)mask;
      hardware->bltbpt = (APTR)(pixels + plane * plane_size);
      hardware->bltcpt = destination + plane * PLANE_WORDS;
      hardware->bltdpt = destination + plane * PLANE_WORDS;
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
   copperWrite(&cursor, offsetof(struct Custom, bpl1mod), ROW_BYTES - 42);
   copperWrite(&cursor, offsetof(struct Custom, bpl2mod), ROW_BYTES - 42);
   for (plane = 0; plane < 5; ++plane)
   {
      address = (DeceptusU32)(buffers[buffer] + plane * PLANE_WORDS + (frame->camera_y - origins_y[buffer]) * ROW_WORDS +
                              ((frame->camera_x & 15) + 15) / 16);
      copperWrite(&cursor, offsetof(struct Custom, bplpt) + plane * 4, address >> 16);
      copperWrite(&cursor, offsetof(struct Custom, bplpt) + plane * 4 + 2, (DeceptusU16)address);
   }
   for (plane = 0; plane < 32; ++plane)
   {
      copperWrite(&cursor, offsetof(struct Custom, color) + plane * 2, palette[plane]);
   }
   copperWrite(&cursor, 0xffff, 0xfffe);
}

DeceptusU8 amigaOpeningVideoDraw(const DeceptusAdventure *game, const DeceptusFrame *frame)
{
   DeceptusI16 x, y, next_x, next_y, dx, dy;
   DeceptusU16 index;
   DeceptusRect rectangle;
   DeceptusU8 was_valid;
   if (error)
   {
      return 0;
   }
   if (active && game->campaign.mode != DECEPTUS_CAMPAIGN_PLAYING && last_ticks == game->campaign.engine.ticks &&
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
   if (was_valid)
   {
      for (index = 0; index < previous_counts[draw_buffer]; ++index)
      {
         paintRectangle(&previous_sprites[draw_buffer][index], 0);
      }
      for (index = 0; index < previous_ui_count[draw_buffer]; ++index)
      {
         paintRectangle(&previous_ui[draw_buffer][index], 0);
      }
   }
   next_x = (frame->camera_x / 16) * 16 - 16;
   next_y = (frame->camera_y / 8) * 8;
   dx = next_x - origin_x;
   dy = next_y - origin_y;
   if (!was_valid || dx <= -CACHE_WIDTH || dx >= CACHE_WIDTH || dy <= -CACHE_HEIGHT || dy >= CACHE_HEIGHT)
   {
      was_valid = 0;
   }
   else if (dx || dy)
   {
      scrollBuffer(dx, dy);
   }
   origin_x = origins_x[draw_buffer] = next_x;
   origin_y = origins_y[draw_buffer] = next_y;
   if (!was_valid || dx || dy)
   {
      for (y = 0; y < CACHE_HEIGHT; y += 8)
      {
         for (x = 0; x < CACHE_WIDTH; x += 8)
         {
            if (!was_valid || x + dx < 0 || x + dx >= CACHE_WIDTH || y + dy < 0 || y + dy >= CACHE_HEIGHT)
            {
               paintTile(x, y, 0);
            }
         }
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
   previous_ui_count[draw_buffer] = amigaOpeningUiDraw(game, (DeceptusU8 *)buffers[draw_buffer], ROW_BYTES, PLANE_BYTES,
                                                       frame->camera_x - origin_x, frame->camera_y - origin_y, previous_ui[draw_buffer]);
   valid[draw_buffer] = 1;
   if (error)
   {
      return 0;
   }
   prepareCopper(draw_buffer, frame);
   if (active)
   {
      waitFrame();
      hardware->cop1lc = (ULONG)copper[draw_buffer];
      hardware->copjmp1 = 0;
   }
   else
   {
      amigaSystemTakeDisplay(verticalBlank, copper[draw_buffer]);
      active = 1;
   }
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
