#include "graphics.h"
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
   return deceptusLz4Decode(graphics->raster + start + first, last - first, pixels, AMIGA_RASTER_BYTES);
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
   if (!world || !sprites || world_size < 48 || world[0] != 'A' || world[1] != 'G' || world[2] != '0' || world[3] != '3' ||
       first_size > sprite_size || (first_size < sprite_size && !tail) || readWord(world + 14) != AMIGA_TILE_GROUP ||
       readLong(world + 44) != sprite_size)
   {
      return 0;
   }
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
       (graphics->height & 7) || !graphics->pattern_count || graphics->sprite_count > AMIGA_GRAPHICS_RESOURCES ||
       !worldRange(graphics, graphics->palette, 64) ||
       !worldRange(graphics, graphics->tile_table, (((DeceptusU32)graphics->pattern_count + 15) / 16 + 1) * 4) ||
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
   return deceptusLz4Decode(graphics->world + offset, size, destination, AMIGA_TILE_GROUP_BYTES);
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
   if (!readLong(bytes + 24) && readLong(bytes + 20))
   {
      return 0;
   }
   sprite->flip = flip && !readLong(bytes + 24);
   if (flip && !sprite->flip)
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
   DeceptusU32 size, offset;
   const DeceptusU8 *source;
   size = (DeceptusU32)sprite->row_bytes * sprite->height * 6;
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
   return deceptusLz4Decode(source, sprite->bytes, destination, size);
}

static DeceptusU8 reverseByte(DeceptusU8 value)
{
   value = (DeceptusU8)((value >> 4) | (value << 4));
   value = (DeceptusU8)(((value & 204) >> 2) | ((value & 51) << 2));
   return (DeceptusU8)(((value & 170) >> 1) | ((value & 85) << 1));
}

void amigaGraphicsSpriteFlip(DeceptusU8 *pixels, const AmigaGraphicsSprite *sprite)
{
   DeceptusU16 row, left, right, bytes;
   DeceptusU8 first, last, shift;
   bytes = (sprite->width + 7) / 8;
   shift = (DeceptusU8)(bytes * 8 - sprite->width);
   for (row = 0; row < sprite->height * 6; ++row)
   {
      for (left = 0; left < (bytes + 1) / 2; ++left)
      {
         right = bytes - left - 1;
         first = pixels[left];
         last = pixels[right];
         pixels[left] = reverseByte(last);
         pixels[right] = reverseByte(first);
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
