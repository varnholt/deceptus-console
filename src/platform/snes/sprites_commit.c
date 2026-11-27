/* the S-CPU half of the sprites: OBJ video setup and the transfers sprites.c staged. the stagings
 * may live in SA-1 BW-RAM, so they are reached through their 24-bit address. */
#include <snes.h>
#include "ppu_timing.h"
#include "sprites.h"

/* the stagings by index from a table: indexing the array itself multiplies by the staging size in a
 * library routine, several times in every commit vblank. */
static SnesSpriteStaging* const staging_table[SNES_SPRITE_STAGINGS] = {&snes_sprite_stagings[0], &snes_sprite_stagings[1]};
typedef char SnesStagingTableComplete[(SNES_SPRITE_STAGINGS == 2) ? 1 : -1];

static SnesSpriteStaging* stagingAt(DeceptusU8 index)
{
   return staging_table[index];
}

void snesSpritesInitVideo(void)
{
   oamInitGfxAttr(0x6000, OBJ_SIZE8_L16);
   oamClear(0, 0);
}

void snesSpritesUploadTiles(DeceptusU8 index)
{
   SnesSpriteStaging* staging;
   DeceptusU16 first, count;
   staging = stagingAt(index);
   if (staging->blank)
   {
      setScreenOff();
   }
   if (staging->deferred)
   {
      return;
   }
   for (first = 0; first < staging->tile_dma_count; first += count)
   {
      count = staging->tile_dma_count - first;
      if (count > SNES_PRELOAD_PIECES * 2)
      {
         count = SNES_PRELOAD_PIECES * 2;
      }
      if (!staging->forced_blank)
      {
         snesWaitForVblank();
      }
      snesVramRunDma(&staging->tile_dma[first], count);
   }
   staging->tile_dma_count = 0;
}

DeceptusU16 snesSpritesCommitLines(DeceptusU8 index)
{
   const SnesSpriteStaging* staging;
   staging = stagingAt(index);
   return SNES_COMMIT_FIXED_LINES + staging->tile_dma_count;
}

/* every video transfer of the sprite commit: palettes, oam and the staged tiles the oam refers to,
   all in one vblank. */
void snesSpritesCommitTransfers(DeceptusU8 index)
{
   SnesSpriteStaging* staging;
   staging = stagingAt(index);
   if (staging->palette_count)
   {
      dmaCopyCGram((u8*)staging->palette_buffer, 128, staging->palette_count * 32);
   }
   dmaCopyOAram((u8*)staging->oam, 0, SNES_OBJ_OAM_BYTES);
   snesVramRunDma(staging->tile_dma, staging->tile_dma_count);
   staging->tile_dma_count = 0;
}

DeceptusU8 snesSpritesBlank(DeceptusU8 index)
{
   return stagingAt(index)->blank;
}

DeceptusU8 snesSpritesOverflow(void)
{
   const DeceptusU8* overflow;
   overflow = &snes_sprite_overflow;
   return *overflow;
}

DeceptusU16 snesSpritesUploadBytes(DeceptusU8 index)
{
   return stagingAt(index)->upload_bytes;
}

DeceptusU16 snesSpritesObjects(void)
{
   const DeceptusU16* count;
   count = &snes_obj_count;
   return *count;
}

DeceptusU16 snesSpritesPalettes(DeceptusU8 index)
{
   return stagingAt(index)->palette_count;
}
