#include "graphics.h"
#include "lz4.h"
#include "amiga_native_assets.h"
#include "deceptus/compression.h"
#include <string.h>

static DeceptusU16 readWord(const DeceptusU8 *bytes)
{
   return (DeceptusU16)((DeceptusU16)bytes[0] << 8) | bytes[1];
}

static DeceptusU32 readLong(const DeceptusU8 *bytes)
{
   return ((DeceptusU32)readWord(bytes) << 16) | readWord(bytes + 2);
}

DeceptusU8 amigaGraphicsSetRaster(AmigaGraphics *graphics, const DeceptusU8 *pixels, DeceptusU32 size)
{
   DeceptusU16 columns;
   DeceptusU32 blocks, block;
   DeceptusU32 start, previous, next;
   graphics->raster = 0;
   graphics->raster_size = 0;
   columns = (graphics->width + 15) / 16;
   blocks = (DeceptusU32)columns * ((graphics->height + AMIGA_RASTER_ROWS - 1) / AMIGA_RASTER_ROWS);
   start = 12 + (blocks + 1) * 4;
   if (!pixels || size < start || pixels[0] != 'A' || pixels[1] != 'C' || pixels[2] != '0' || pixels[3] != '2' ||
       readWord(pixels + 4) != graphics->width || readWord(pixels + 6) != graphics->height || readWord(pixels + 8) != 16 ||
       readWord(pixels + 10) != columns || readLong(pixels + 12))
   {
      return 0;
   }
   previous = 0;
   for (block = 0; block < blocks; ++block)
   {
      next = readLong(pixels + 16 + block * 4);
      if (next <= previous || next > size - start)
      {
         return 0;
      }
      previous = next;
   }
   if (previous != size - start)
   {
      return 0;
   }
   graphics->raster = pixels;
   graphics->raster_size = size;
   return 1;
}

DeceptusU8 amigaGraphicsRasterBlock(const AmigaGraphics *graphics, DeceptusU16 column, DeceptusU16 band, DeceptusU8 *pixels,
                                    DeceptusU32 capacity)
{
   DeceptusU16 columns, bands;
   DeceptusU32 start, first, last, block;
   columns = (graphics->width + 15) / 16;
   bands = (graphics->height + AMIGA_RASTER_ROWS - 1) / AMIGA_RASTER_ROWS;
   start = 12 + ((DeceptusU32)columns * bands + 1) * 4;
   if (!graphics->raster || column >= columns || band >= bands || capacity < AMIGA_RASTER_BYTES || graphics->raster_size < start)
   {
      return 0;
   }
   block = (DeceptusU32)column * bands + band;
   first = readLong(graphics->raster + 12 + block * 4);
   last = readLong(graphics->raster + 16 + block * 4);
   if (first >= last || last > graphics->raster_size - start)
   {
      return 0;
   }
   return amigaLz4Decode(graphics->raster + start + first, last - first, pixels, AMIGA_RASTER_BYTES);
}

static DeceptusU8 worldRange(const AmigaGraphics *graphics, DeceptusU32 offset, DeceptusU32 size)
{
   return offset <= graphics->world_size && size <= graphics->world_size - offset;
}

static DeceptusU8 blockRange(const AmigaGraphics *graphics, DeceptusU32 table, DeceptusU16 index, DeceptusU32 base, DeceptusU32 *offset,
                             DeceptusU32 *size)
{
   DeceptusU32 first, last;
   if (!worldRange(graphics, table, ((DeceptusU32)index + 2) * 4))
   {
      return 0;
   }
   first = readLong(graphics->world + table + (DeceptusU32)index * 4);
   last = readLong(graphics->world + table + ((DeceptusU32)index + 1) * 4);
   if (first > last || base > graphics->world_size || !worldRange(graphics, base, last))
   {
      return 0;
   }
   *offset = base + first;
   *size = last - first;
   return 1;
}

DeceptusU8 amigaGraphicsInit(AmigaGraphics *graphics, const DeceptusU8 *world, DeceptusU32 world_size, const DeceptusU8 *sprites,
                             DeceptusU32 sprite_size, DeceptusU32 first_size, const DeceptusU8 *tail)
{
   DeceptusU16 index, resource;
   AmigaGraphicsSprite sprite;
   memset(graphics, 0, sizeof(*graphics));
   if (!world || !sprites || world_size < 48 || world[0] != 'A' || world[2] != '0' ||
       !((world[1] == 'G' && world[3] == '3') || (world[1] == 'N' && (world[3] == '1' || world[3] == '2' || world[3] == '3'))) || first_size > sprite_size ||
       (first_size < sprite_size && !tail) || readWord(world + 14) != AMIGA_TILE_GROUP || readLong(world + 44) != sprite_size)
   {
      return 0;
   }
   graphics->native_only = world[1] == 'N';
   graphics->interleaved_sprites = graphics->native_only && (world[3] == '2' || world[3] == '3');
   graphics->compact_masks = graphics->native_only && world[3] == '3';
   graphics->world = world;
   graphics->sprites = sprites;
   graphics->world_size = world_size;
   graphics->sprite_size = sprite_size;
   graphics->sprite_first_size = first_size;
   graphics->sprite_tail = tail;
   graphics->width = readWord(world + 4);
   graphics->height = readWord(world + 6);
   graphics->pattern_count = readWord(world + 8);
   graphics->sprite_count = readWord(world + 10);
   graphics->clip_count = readWord(world + 12);
   graphics->palette = readLong(world + 16);
   graphics->tile_table = readLong(world + 20);
   graphics->tile_data = readLong(world + 24);
   graphics->background = readLong(world + 28);
   graphics->foreground = readLong(world + 32);
   graphics->clips = readLong(world + 36);
   graphics->sprite_table = readLong(world + 40);
   if (!graphics->width || !graphics->height || graphics->width > 2040 || graphics->height > 2040 || (graphics->width & 7) ||
       (graphics->height & 7) || (!graphics->native_only && !graphics->pattern_count) ||
       graphics->sprite_count > AMIGA_GRAPHICS_RESOURCES || !worldRange(graphics, graphics->palette, 64) ||
       (!graphics->native_only &&
        !worldRange(graphics, graphics->tile_table, (((DeceptusU32)graphics->pattern_count + 15) / 16 + 1) * 4)) ||
       (graphics->native_only &&
        (graphics->pattern_count || graphics->tile_table || graphics->tile_data || graphics->background || graphics->foreground)) ||
       !worldRange(graphics, graphics->sprite_table, (DeceptusU32)graphics->sprite_count * 28) ||
       !worldRange(graphics, graphics->clips, (DeceptusU32)graphics->clip_count * 26))
   {
      return 0;
   }
   memset(graphics->resources, 255, sizeof(graphics->resources));
   for (index = 0; index < graphics->sprite_count; ++index)
   {
      resource = readWord(world + graphics->sprite_table + (DeceptusU32)index * 28);
      if (resource >= AMIGA_GRAPHICS_RESOURCES || graphics->resources[resource] != 65535U)
      {
         return 0;
      }
      graphics->resources[resource] = index;
      if (!amigaGraphicsSpriteInfo(graphics, resource, 0, &sprite) || !amigaGraphicsSpriteInfo(graphics, resource, 1, &sprite))
      {
         return 0;
      }
   }
   return 1;
}

DeceptusU32 amigaGraphicsSpriteSplit(const DeceptusU8 *world, DeceptusU32 size, DeceptusU32 available)
{
   DeceptusU32 table, offset, bytes, split;
   DeceptusU16 index, count;
   if (!world || size < 48)
   {
      return 0;
   }
   count = readWord(world + 10);
   table = readLong(world + 40);
   if (table > size || (DeceptusU32)count * 28 > size - table)
   {
      return 0;
   }
   split = 0;
   for (index = 0; index < count; ++index)
   {
      DeceptusU8 orientation;
      for (orientation = 0; orientation < 2; ++orientation)
      {
         offset = readLong(world + table + (DeceptusU32)index * 28 + 12 + orientation * 8);
         bytes = readLong(world + table + (DeceptusU32)index * 28 + 16 + orientation * 8);
         if (bytes && offset <= available && bytes <= available - offset && offset + bytes > split)
         {
            split = offset + bytes;
         }
      }
   }
   return split;
}

DeceptusU8 amigaGraphicsPalette(const AmigaGraphics *graphics, DeceptusU16 *palette)
{
   DeceptusU8 index;
   if (!worldRange(graphics, graphics->palette, 64))
   {
      return 0;
   }
   for (index = 0; index < 32; ++index)
   {
      palette[index] = readWord(graphics->world + graphics->palette + index * 2);
      if (palette[index] > 4095)
      {
         return 0;
      }
   }
   return 1;
}

DeceptusU8 amigaGraphicsRow(const AmigaGraphics *graphics, DeceptusU32 map, DeceptusU16 columns, DeceptusU16 rows, DeceptusU16 row,
                            DeceptusU16 *cells)
{
   DeceptusU16 index;
   DeceptusU32 offset, size, table_size;
   table_size = ((DeceptusU32)rows + 1) * 4;
   if (!columns || columns > 256 || row >= rows || !worldRange(graphics, map, table_size) ||
       !blockRange(graphics, map, row, map + table_size, &offset, &size) ||
       !deceptusRleDecodeWords(graphics->world + offset, size, cells, columns))
   {
      return 0;
   }
   for (index = 0; index < columns; ++index)
   {
      if (cells[index] >= graphics->pattern_count)
      {
         return 0;
      }
   }
   return 1;
}

DeceptusU8 amigaGraphicsTileGroup(const AmigaGraphics *graphics, DeceptusU16 group, DeceptusU8 *destination)
{
   DeceptusU32 offset, size;
   if ((DeceptusU32)group * AMIGA_TILE_GROUP >= graphics->pattern_count ||
       !blockRange(graphics, graphics->tile_table, group, graphics->tile_data, &offset, &size))
   {
      return 0;
   }
   return amigaLz4Decode(graphics->world + offset, size, destination, AMIGA_TILE_GROUP_BYTES);
}

DeceptusU8 amigaGraphicsClip(const AmigaGraphics *graphics, DeceptusU16 index, AmigaGraphicsClip *clip)
{
   const DeceptusU8 *bytes;
   if (index >= graphics->clip_count || !worldRange(graphics, graphics->clips, ((DeceptusU32)index + 1) * 26))
   {
      return 0;
   }
   bytes = graphics->world + graphics->clips + (DeceptusU32)index * 26;
   clip->bounds.x = (DeceptusI16)readWord(bytes);
   clip->bounds.y = (DeceptusI16)readWord(bytes + 2);
   clip->bounds.width = (DeceptusI16)readWord(bytes + 4);
   clip->bounds.height = (DeceptusI16)readWord(bytes + 6);
   clip->frames = readWord(bytes + 8);
   clip->frame_ticks = readWord(bytes + 10);
   clip->required_flags = readWord(bytes + 12);
   clip->forbidden_flags = readWord(bytes + 14);
   clip->plane = bytes[16];
   clip->durations = readLong(bytes + 18);
   clip->map = readLong(bytes + 22);
   return clip->bounds.width > 0 && clip->bounds.height > 0 && !(clip->bounds.x & 7) && !(clip->bounds.y & 7) &&
          !(clip->bounds.width & 7) && !(clip->bounds.height & 7) && clip->frames && clip->plane <= 1 &&
          (clip->frame_ticks || clip->durations) &&
          (!clip->durations || worldRange(graphics, clip->durations, (DeceptusU32)clip->frames * 2));
}

DeceptusU8 amigaGraphicsSpriteInfo(const AmigaGraphics *graphics, DeceptusU16 resource, DeceptusU8 flip, AmigaGraphicsSprite *sprite)
{
   const DeceptusU8 *bytes;
   DeceptusU16 index;
   if (resource >= AMIGA_GRAPHICS_RESOURCES)
   {
      return 0;
   }
   index = graphics->resources[resource];
   if (index >= graphics->sprite_count || !worldRange(graphics, graphics->sprite_table, ((DeceptusU32)index + 1) * 28))
   {
      return 0;
   }
   bytes = graphics->world + graphics->sprite_table + (DeceptusU32)index * 28;
   sprite->trim_x = (DeceptusI16)readWord(bytes + 2);
   sprite->flip_trim_x = (DeceptusI16)readWord(bytes + 4);
   sprite->trim_y = (DeceptusI16)readWord(bytes + 6);
   sprite->width = readWord(bytes + 8);
   sprite->height = readWord(bytes + 10);
   sprite->row_bytes = ((sprite->width + 15) / 16 + 1) * 2;
   sprite->hardware = 0;
   if (graphics->compact_masks)
   {
      /* compact frames are mirrored when cached; their third word holds flags instead. */
      if (readLong(bytes + 24) || (readLong(bytes + 20) & ~1UL))
      {
         return 0;
      }
      sprite->hardware = (DeceptusU8)(readLong(bytes + 20) & 1);
   }
   else if (!readLong(bytes + 24) && readLong(bytes + 20))
   {
      return 0;
   }
   sprite->flip = flip && (graphics->compact_masks || !readLong(bytes + 24));
   sprite->interleaved = graphics->interleaved_sprites;
   /* compact native frames carry the build's playfield plane count; older formats have five. */
   sprite->planes = graphics->compact_masks ? AMIGA_NATIVE_PLANES : 5;
   if (flip && !sprite->flip && !graphics->compact_masks)
   {
      sprite->offset = readLong(bytes + 20);
      sprite->bytes = readLong(bytes + 24);
   }
   else
   {
      sprite->offset = readLong(bytes + 12);
      sprite->bytes = readLong(bytes + 16);
   }
   return sprite->bytes && sprite->width && sprite->height && sprite->width <= 256 && sprite->height <= 256 &&
          sprite->offset <= graphics->sprite_size && sprite->bytes <= graphics->sprite_size - sprite->offset;
}

DeceptusU8 amigaGraphicsSpriteDecode(const AmigaGraphics *graphics, const AmigaGraphicsSprite *sprite, DeceptusU8 *destination,
                                     DeceptusU32 capacity)
{
   DeceptusU32 size, offset, compact;
   DeceptusU16 row, plane, stored, words, index, line[20], *output;
   DeceptusU8 *tail;
   const DeceptusU8 *input;
   const DeceptusU8 *source;
   size = (DeceptusU32)sprite->row_bytes * sprite->height * (graphics->interleaved_sprites ? sprite->planes * 2 : 6);
   if (size > capacity || sprite->offset > graphics->sprite_size || sprite->bytes > graphics->sprite_size - sprite->offset)
   {
      return 0;
   }
   offset = sprite->offset;
   if (offset < graphics->sprite_first_size)
   {
      if (sprite->bytes > graphics->sprite_first_size - offset)
      {
         return 0;
      }
      source = graphics->sprites + offset;
   }
   else
   {
      offset -= graphics->sprite_first_size;
      if (!graphics->sprite_tail)
      {
         return 0;
      }
      source = graphics->sprite_tail + offset;
   }
   if (!graphics->compact_masks)
   {
      return amigaLz4Decode(source, sprite->bytes, destination, size);
   }
   /* compact frames omit the blitter's zero shift word and store one mask plane. decode them into
      the tail of the frame, then widen rows front to back: every write stays below the next
      unread compact row, and a row is staged in a buffer before its own copies are written. */
   stored = sprite->row_bytes - 2;
   compact = (DeceptusU32)stored * sprite->height * (sprite->planes + 1);
   tail = destination + size - compact;
   if (sprite->row_bytes > sizeof(line) || (sprite->row_bytes & 1) || !amigaLz4Decode(source, sprite->bytes, tail, compact))
   {
      return 0;
   }
   /* rows are whole words (row_bytes is even), so a row moves as words; the staging buffer keeps a
      row's own source intact while its widened copy overwrites the bytes just below it. */
   words = sprite->row_bytes / 2;
   line[words - 1] = 0;
   input = tail;
   output = (DeceptusU16 *)destination;
   for (row = 0; row < sprite->height * sprite->planes; ++row)
   {
      for (index = 0; index < words - 1; ++index, input += 2)
      {
         line[index] = ((DeceptusU16)input[0] << 8) | input[1];
      }
      for (index = 0; index < words; ++index)
      {
         *output++ = line[index];
      }
   }
   for (row = 0; row < sprite->height; ++row)
   {
      for (index = 0; index < words - 1; ++index, input += 2)
      {
         line[index] = ((DeceptusU16)input[0] << 8) | input[1];
      }
      for (plane = 0; plane < sprite->planes; ++plane)
      {
         for (index = 0; index < words; ++index)
         {
            *output++ = line[index];
         }
      }
   }
   return 1;
}

DeceptusU32 amigaGraphicsSpriteCompact(const AmigaGraphics *graphics, const AmigaGraphicsSprite *sprite, DeceptusU8 *destination,
                                       DeceptusU32 capacity)
{
   DeceptusU32 compact, offset;
   const DeceptusU8 *source;
   compact = (DeceptusU32)(sprite->row_bytes - 2) * sprite->height * (sprite->planes + 1);
   if (!graphics->compact_masks || compact > capacity || sprite->offset > graphics->sprite_size ||
       sprite->bytes > graphics->sprite_size - sprite->offset)
   {
      return 0;
   }
   offset = sprite->offset;
   if (offset < graphics->sprite_first_size)
   {
      if (sprite->bytes > graphics->sprite_first_size - offset)
      {
         return 0;
      }
      source = graphics->sprites + offset;
   }
   else
   {
      if (!graphics->sprite_tail)
      {
         return 0;
      }
      source = graphics->sprite_tail + (offset - graphics->sprite_first_size);
   }
   return amigaLz4Decode(source, sprite->bytes, destination, compact) ? compact : 0;
}

static DeceptusU8 reverseByte(DeceptusU8 value)
{
   value = (DeceptusU8)((value >> 4) | (value << 4));
   value = (DeceptusU8)(((value & 204) >> 2) | ((value & 51) << 2));
   return (DeceptusU8)(((value & 170) >> 1) | ((value & 85) << 1));
}

/* mirrored frames are flipped once when decoded; a table keeps that to one load per byte. */
static DeceptusU8 reversed[256];
static DeceptusU8 reversed_ready;

void amigaGraphicsSpriteFlip(DeceptusU8 *pixels, const AmigaGraphicsSprite *sprite)
{
   DeceptusU16 row, left, right, bytes;
   DeceptusU8 first, last, shift;
   if (!reversed_ready)
   {
      for (row = 0; row < 256; ++row)
      {
         reversed[row] = reverseByte((DeceptusU8)row);
      }
      reversed_ready = 1;
   }
   bytes = (sprite->width + 7) / 8;
   shift = (DeceptusU8)(bytes * 8 - sprite->width);
   for (row = 0; row < sprite->height * (sprite->interleaved ? sprite->planes * 2 : 6); ++row)
   {
      for (left = 0; left < (bytes + 1) / 2; ++left)
      {
         right = bytes - left - 1;
         first = pixels[left];
         last = pixels[right];
         pixels[left] = reversed[last];
         pixels[right] = reversed[first];
      }
      if (shift)
      {
         for (left = 0; left < bytes; ++left)
         {
            pixels[left] = (DeceptusU8)((pixels[left] << shift) | (pixels[left + 1] >> (8 - shift)));
         }
      }
      pixels += sprite->row_bytes;
   }
}

static DeceptusU32 nativeClipFrameBytes(const AmigaGraphicsClip *clip)
{
   DeceptusI16 left;
   left = clip->bounds.x / 16;
   if (clip->bounds.x < 0 && clip->bounds.x % 16)
   {
      --left;
   }
   return (DeceptusU32)((clip->bounds.x - left * 16 + clip->bounds.width + 15) / 16) * 2 * clip->bounds.height * 6;
}

DeceptusU8 amigaGraphicsSetNativeClips(AmigaGraphics *graphics, const DeceptusU8 *pixels, DeceptusU32 size)
{
   DeceptusU16 index;
   DeceptusU32 start, previous, next, bytes;
   AmigaGraphicsClip clip;
   graphics->native_clips = 0;
   start = 8 + ((DeceptusU32)graphics->clip_count + 1) * 4;
   if (!pixels || size < start || pixels[0] != 'A' || pixels[1] != 'L' || pixels[2] != '0' || pixels[3] != '1' ||
       readWord(pixels + 4) != graphics->clip_count || readWord(pixels + 6) || readLong(pixels + 8))
   {
      return 0;
   }
   previous = 0;
   for (index = 0; index < graphics->clip_count; ++index)
   {
      if (!amigaGraphicsClip(graphics, index, &clip))
      {
         return 0;
      }
      bytes = nativeClipFrameBytes(&clip);
      if (!bytes || clip.frames > (size - start - previous) / bytes)
      {
         return 0;
      }
      bytes *= clip.frames;
      next = readLong(pixels + 12 + (DeceptusU32)index * 4);
      if (next < previous || next > size - start || next - previous != bytes)
      {
         return 0;
      }
      previous = next;
   }
   if (previous != size - start)
   {
      return 0;
   }
   graphics->native_clips = pixels;
   return 1;
}

const DeceptusU16 *amigaGraphicsNativeClip(const AmigaGraphics *graphics, DeceptusU16 index, DeceptusU16 frame)
{
   AmigaGraphicsClip clip;
   DeceptusU32 start, offset;
   if (!graphics->native_clips || index >= graphics->clip_count || !amigaGraphicsClip(graphics, index, &clip) || frame >= clip.frames)
   {
      return 0;
   }
   start = 8 + ((DeceptusU32)graphics->clip_count + 1) * 4;
   offset = readLong(graphics->native_clips + 8 + (DeceptusU32)index * 4);
   return (const DeceptusU16 *)(graphics->native_clips + start + offset + (DeceptusU32)frame * nativeClipFrameBytes(&clip));
}

DeceptusU8 amigaGraphicsStormEntries(const AmigaGraphics *graphics, DeceptusU8 *entries, DeceptusU8 *count)
{
   DeceptusU32 list;
   DeceptusU16 length;
   DeceptusU8 index;
   list = graphics->palette + 64;
   if (!worldRange(graphics, list, 2))
   {
      return 0;
   }
   length = readWord(graphics->world + list);
   if ((length > 32) || !worldRange(graphics, list + 2, length))
   {
      return 0;
   }
   for (index = 0; index < length; ++index)
   {
      entries[index] = graphics->world[list + 2 + index];
      if (entries[index] > 31)
      {
         return 0;
      }
   }
   *count = (DeceptusU8)length;
   return 1;
}
