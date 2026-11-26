#ifndef DECEPTUS_AMIGA_GRAPHICS_H
#define DECEPTUS_AMIGA_GRAPHICS_H
#include "deceptus/mechanisms.h"

#define AMIGA_GRAPHICS_RESOURCES 1066
#define AMIGA_WIDE_CACHE_WIDTH 448
#define AMIGA_WIDE_CACHE_HEIGHT 304
#define AMIGA_LARGE_CACHE_WIDTH 512
#define AMIGA_LARGE_CACHE_HEIGHT 312
#define AMIGA_TALL_CACHE_WIDTH 400
#define AMIGA_TALL_CACHE_HEIGHT 312
#define AMIGA_SMALL_CACHE_WIDTH 384
#define AMIGA_SMALL_CACHE_HEIGHT 256
#define AMIGA_NATIVE_CACHE_WIDTH 432
#define AMIGA_NATIVE_CACHE_HEIGHT 304
#define AMIGA_BASE_CACHE_WIDTH 368
#define AMIGA_BASE_CACHE_HEIGHT 232
#define AMIGA_TILE_CACHE_GROUPS 254
#define AMIGA_RASTER_TILE_GROUPS 16
#define AMIGA_DECODED_SPRITE_BYTES (64UL * 1024)
#define AMIGA_TILE_GROUP 16
#define AMIGA_TILE_BYTES 48
#define AMIGA_TILE_GROUP_BYTES (AMIGA_TILE_GROUP * AMIGA_TILE_BYTES)
#define AMIGA_RASTER_ROWS 32
#define AMIGA_RASTER_BYTES (AMIGA_RASTER_ROWS * 12)

typedef struct AmigaGraphics
{
   const DeceptusU8 *world, *sprites, *sprite_tail;
   const DeceptusU8 *raster;
   DeceptusU16 *raster_scratch;
   DeceptusU32 raster_size;
   const DeceptusU16 *background_pixels;
   const DeceptusU16 *native_foreground;
   const DeceptusU8 *native_clips;
   /* split pages borrow the compact display region and one resource-bank tail. */
   /* compact_masks: interleaved sprites store one mask plane, repeated per plane when decoded. */
   DeceptusU8 native_only, viewport_split, interleaved_sprites, compact_masks;
   DeceptusU16 *viewport_pixels, *foreground_pixels;
   DeceptusU16 viewport_width, viewport_height;
   DeceptusU32 world_size, sprite_size, sprite_first_size;
   DeceptusU32 palette, tile_table, tile_data, background, foreground, clips, sprite_table;
   DeceptusU16 width, height, pattern_count, sprite_count, clip_count;
   DeceptusU16 resources[AMIGA_GRAPHICS_RESOURCES];
} AmigaGraphics;

typedef struct AmigaGraphicsClip
{
   DeceptusRect bounds;
   DeceptusU16 frames, frame_ticks, required_flags, forbidden_flags;
   DeceptusU8 plane;
   DeceptusU32 durations, map;
} AmigaGraphicsClip;

typedef struct AmigaGraphicsSprite
{
   DeceptusI16 trim_x, flip_trim_x, trim_y;
   DeceptusU16 width, height, row_bytes;
   DeceptusU32 offset, bytes;
   /* missing offline orientation: mirror once when populating the decoded cache. */
   DeceptusU8 flip, interleaved;
   /* colour planes; interleaved frames hold as many mask rows after them. */
   DeceptusU8 planes;
   /* drawn by sprite DMA: pixels hold hardware sprite colours 1-15 (0 transparent). */
   DeceptusU8 hardware;
} AmigaGraphicsSprite;

DeceptusU8 amigaGraphicsInit(AmigaGraphics *graphics, const DeceptusU8 *world, DeceptusU32 world_size, const DeceptusU8 *sprites,
                             DeceptusU32 sprite_size, DeceptusU32 first_size, const DeceptusU8 *tail);
/* select a compressed-block boundary for two fixed physical RAM banks. */
DeceptusU32 amigaGraphicsSpriteSplit(const DeceptusU8 *world, DeceptusU32 size, DeceptusU32 available);
DeceptusU8 amigaGraphicsPalette(const AmigaGraphics *graphics, DeceptusU16 *palette);
DeceptusU8 amigaGraphicsRow(const AmigaGraphics *graphics, DeceptusU32 map, DeceptusU16 columns, DeceptusU16 rows, DeceptusU16 row,
                            DeceptusU16 *cells);
DeceptusU8 amigaGraphicsTileGroup(const AmigaGraphics *graphics, DeceptusU16 group, DeceptusU8 *destination);
DeceptusU8 amigaGraphicsClip(const AmigaGraphics *graphics, DeceptusU16 index, AmigaGraphicsClip *clip);
/* selects an offline orientation when available; trim offsets retain both facings. */
DeceptusU8 amigaGraphicsSpriteInfo(const AmigaGraphics *graphics, DeceptusU16 resource, DeceptusU8 flip, AmigaGraphicsSprite *sprite);
DeceptusU8 amigaGraphicsSpriteDecode(const AmigaGraphics *graphics, const AmigaGraphicsSprite *sprite, DeceptusU8 *destination,
                                     DeceptusU32 capacity);
void amigaGraphicsSpriteFlip(DeceptusU8 *pixels, const AmigaGraphicsSprite *sprite);
/* a compact native frame as stored: `planes` colour rows per line for every line, then one mask
   row per line, each (row_bytes - 2) bytes, unmirrored. returns its byte size, 0 on failure. */
DeceptusU32 amigaGraphicsSpriteCompact(const AmigaGraphics *graphics, const AmigaGraphicsSprite *sprite, DeceptusU8 *destination,
                                       DeceptusU32 capacity);
DeceptusU8 amigaGraphicsSetRaster(AmigaGraphics *graphics, const DeceptusU8 *pixels, DeceptusU32 size);
DeceptusU8 amigaGraphicsSetNativeClips(AmigaGraphics *graphics, const DeceptusU8 *pixels, DeceptusU32 size);
const DeceptusU16 *amigaGraphicsNativeClip(const AmigaGraphics *graphics, DeceptusU16 index, DeceptusU16 frame);
DeceptusU8 amigaGraphicsRasterBlock(const AmigaGraphics *graphics, DeceptusU16 column, DeceptusU16 band, DeceptusU8 *pixels,
                                    DeceptusU32 capacity);
#endif
