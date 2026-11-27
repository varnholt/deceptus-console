#ifndef DECEPTUS_AMIGA_RASTER_CACHE_H
#define DECEPTUS_AMIGA_RASTER_CACHE_H
#include "graphics.h"

#define AMIGA_RASTER_CACHE_SLOTS 128

typedef struct AmigaRasterCache
{
   DeceptusU8* pixels[2];
   DeceptusU16 first_slots, slots;
   DeceptusU16 keys[AMIGA_RASTER_CACHE_SLOTS];
   DeceptusU8 cursors[AMIGA_RASTER_CACHE_SLOTS / 2];
} AmigaRasterCache;

/* both caller-owned leases and the archive remain unchanged until reset. */
void amigaRasterCacheInit(AmigaRasterCache* cache, DeceptusU8* first, DeceptusU32 first_size, DeceptusU8* second, DeceptusU32 second_size);
/* returned pixels remain valid until the next lookup; fallback holds one block. */
const DeceptusU8*
amigaRasterCachePixels(AmigaRasterCache* cache, const AmigaGraphics* graphics, DeceptusU16 column, DeceptusU16 band, DeceptusU8* fallback);
#endif
