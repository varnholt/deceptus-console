#ifndef DECEPTUS_AMIGA_SPRITE_CACHE_H
#define DECEPTUS_AMIGA_SPRITE_CACHE_H
#include "graphics.h"

#define AMIGA_SPRITE_CACHE_ENTRIES 32

typedef struct AmigaSpriteCacheEntry
{
   AmigaGraphicsSprite graphic;
   DeceptusU32 start, size;
} AmigaSpriteCacheEntry;

typedef struct AmigaSpriteCache
{
   DeceptusU8 *pixels;
   DeceptusU32 capacity, cursor;
   DeceptusU8 next, chip_memory;
   AmigaSpriteCacheEntry entries[AMIGA_SPRITE_CACHE_ENTRIES];
} AmigaSpriteCache;

/* storage is borrowed until the next graphics load; reset invalidates every frame.
 * chip_memory requires DMA-accessible, word-aligned storage. */
void amigaSpriteCacheInit(AmigaSpriteCache *cache, DeceptusU8 *storage, DeceptusU32 capacity, DeceptusU8 chip_memory);
/* accepts validated resident descriptors; Chip cache hits need no staging copy.
 * the returned pixels remain valid until the next cache call or graphics load.
 * wait for DMA before mutating the cache again; other memory uses Chip staging. */
const DeceptusU8 *amigaSpriteCachePixels(AmigaSpriteCache *cache, const AmigaGraphics *graphics, const AmigaGraphicsSprite *sprite,
                                         DeceptusU8 *destination, DeceptusU32 capacity);
#endif
