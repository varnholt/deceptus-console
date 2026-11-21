#include "rastercache.h"
#include <string.h>

void amigaRasterCacheInit(AmigaRasterCache *cache, DeceptusU8 *first, DeceptusU32 first_size, DeceptusU8 *second, DeceptusU32 second_size)
{
   DeceptusU32 available, first_slots;
   memset(cache, 0, sizeof(*cache));
   first_slots = first ? first_size / AMIGA_RASTER_BYTES : 0;
   available = first_slots + (second ? second_size / AMIGA_RASTER_BYTES : 0);
   if (available < 2)
   {
      return;
   }
   cache->slots = 2;
   while (cache->slots < AMIGA_RASTER_CACHE_SLOTS && (DeceptusU32)cache->slots * 2 <= available)
   {
      cache->slots *= 2;
   }
   cache->first_slots = (DeceptusU16)(first_slots < cache->slots ? first_slots : cache->slots);
   cache->pixels[0] = first;
   cache->pixels[1] = second;
   memset(cache->keys, 255, sizeof(cache->keys));
}

const DeceptusU8 *amigaRasterCachePixels(AmigaRasterCache *cache, const AmigaGraphics *graphics, DeceptusU16 column, DeceptusU16 band,
                                         DeceptusU8 *fallback)
{
   DeceptusU16 bands, set, slot;
   DeceptusU32 key;
   DeceptusU8 *pixels;
   bands = (graphics->height + AMIGA_RASTER_ROWS - 1) / AMIGA_RASTER_ROWS;
   if (column >= (graphics->width + 15) / 16 || band >= bands)
   {
      return 0;
   }
   if (!cache->slots)
   {
      return amigaGraphicsRasterBlock(graphics, column, band, fallback, AMIGA_RASTER_BYTES) ? fallback : 0;
   }
   key = (DeceptusU32)column * bands + band;
   if (key >= 65535UL)
   {
      return 0;
   }
   set = (DeceptusU16)key & (cache->slots / 2 - 1);
   slot = set * 2;
   if (cache->keys[slot] != key && cache->keys[slot + 1] == key)
   {
      ++slot;
   }
   else if (cache->keys[slot] != key)
   {
      slot += cache->cursors[set]++ & 1;
   }
   pixels = slot < cache->first_slots ? cache->pixels[0] + (DeceptusU32)slot * AMIGA_RASTER_BYTES
                                      : cache->pixels[1] + (DeceptusU32)(slot - cache->first_slots) * AMIGA_RASTER_BYTES;
   if (cache->keys[slot] != key)
   {
      cache->keys[slot] = 65535U;
      if (!amigaGraphicsRasterBlock(graphics, column, band, pixels, AMIGA_RASTER_BYTES))
      {
         return 0;
      }
      cache->keys[slot] = (DeceptusU16)key;
   }
   return pixels;
}
