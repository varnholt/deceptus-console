#ifndef DECEPTUS_AMIGA_SPRITE_CACHE_H
#define DECEPTUS_AMIGA_SPRITE_CACHE_H
#include "graphics.h"

#define AMIGA_SPRITE_CACHE_ENTRIES 16

typedef struct AmigaSpriteCacheEntry
{
   AmigaGraphicsSprite graphic;
   DeceptusU32 start, size;
} AmigaSpriteCacheEntry;

typedef struct AmigaSpriteCache
{
   DeceptusU8 *pixels;
   DeceptusU32 capacity, cursor;
   DeceptusU8 next;
   AmigaSpriteCacheEntry entries[AMIGA_SPRITE_CACHE_ENTRIES];
} AmigaSpriteCache;

/* storage is borrowed until the next graphics load; reset invalidates every frame. */
void amigaSpriteCacheInit(AmigaSpriteCache *cache, DeceptusU8 *storage, DeceptusU32 capacity);
/* accepts validated descriptors from the resident graphics; copies into Chip RAM staging.
 * undersized caches fall back to direct decoding. */
DeceptusU8 amigaSpriteCacheDecode(AmigaSpriteCache *cache, const AmigaGraphics *graphics, const AmigaGraphicsSprite *sprite,
                                  DeceptusU8 *destination, DeceptusU32 capacity);
#endif
