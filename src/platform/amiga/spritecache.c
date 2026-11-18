#include "spritecache.h"
#include <string.h>

void amigaSpriteCacheInit(AmigaSpriteCache *cache, DeceptusU8 *storage, DeceptusU32 capacity)
{
   memset(cache, 0, sizeof(*cache));
   cache->pixels = storage;
   cache->capacity = storage ? capacity : 0;
}

static DeceptusU8 decodeSprite(const AmigaGraphics *graphics, const AmigaGraphicsSprite *sprite, DeceptusU8 *destination,
                               DeceptusU32 capacity)
{
   if (!amigaGraphicsSpriteDecode(graphics, sprite, destination, capacity))
   {
      return 0;
   }
   if (sprite->flip)
   {
      amigaGraphicsSpriteFlip(destination, sprite);
   }
   return 1;
}

DeceptusU8 amigaSpriteCacheDecode(AmigaSpriteCache *cache, const AmigaGraphics *graphics, const AmigaGraphicsSprite *sprite,
                                  DeceptusU8 *destination, DeceptusU32 capacity)
{
   DeceptusU32 size;
   DeceptusU8 slot;
   AmigaSpriteCacheEntry *entry;
   size = (DeceptusU32)sprite->row_bytes * sprite->height * 6;
   if (size > capacity || !destination)
   {
      return 0;
   }
   if (size > cache->capacity)
   {
      return decodeSprite(graphics, sprite, destination, capacity);
   }
   for (slot = 0; slot < AMIGA_SPRITE_CACHE_ENTRIES; ++slot)
   {
      entry = &cache->entries[slot];
      if (entry->size == size && entry->graphic.offset == sprite->offset && entry->graphic.bytes == sprite->bytes &&
          entry->graphic.width == sprite->width && entry->graphic.height == sprite->height &&
          entry->graphic.row_bytes == sprite->row_bytes && entry->graphic.flip == sprite->flip)
      {
         memcpy(destination, cache->pixels + entry->start, size);
         return 1;
      }
   }
   if (size > cache->capacity - cache->cursor)
   {
      cache->cursor = 0;
   }
   for (slot = 0; slot < AMIGA_SPRITE_CACHE_ENTRIES; ++slot)
   {
      entry = &cache->entries[slot];
      if (entry->start < cache->cursor + size && entry->start + entry->size > cache->cursor)
      {
         entry->size = 0;
      }
   }
   if (!decodeSprite(graphics, sprite, cache->pixels + cache->cursor, size))
   {
      return 0;
   }
   entry = &cache->entries[cache->next++ % AMIGA_SPRITE_CACHE_ENTRIES];
   entry->graphic = *sprite;
   entry->start = cache->cursor;
   entry->size = size;
   memcpy(destination, cache->pixels + cache->cursor, size);
   cache->cursor += size;
   return 1;
}
