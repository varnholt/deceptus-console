/* the S-CPU half of the sprites: OBJ video setup and the transfers sprites.c staged. the stagings
 * may live in SA-1 BW-RAM, so they are reached through their 24-bit address. */
#include "sprites.h"
#include <snes.h>
#include <string.h>

static SnesSpriteStaging *stagingAt(DeceptusU8 index)
{
   SnesSpriteStaging *stagings;
   stagings = snes_sprite_stagings;
   return &stagings[index];
}

void snesSpritesInitVideo(void)
{
   oamInitGfxAttr(0x6000, OBJ_SIZE8_L16);
   oamClear(0, 0);
}

void snesSpritesUploadTiles(DeceptusU8 index)
{
   SnesSpriteStaging *staging;
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
         WaitForVBlank();
      }
      snesVramRunDma(&staging->tile_dma[first], count);
   }
   staging->tile_dma_count = 0;
}

DeceptusU16 snesSpritesCommitLines(DeceptusU8 index)
{
   const SnesSpriteStaging *staging;
   staging = stagingAt(index);
   return SNES_COMMIT_FIXED_LINES + staging->tile_dma_count;
}

/* every video transfer of the sprite commit: palettes, oam and the staged tiles the oam refers to,
   all in one vblank. */
void snesSpritesCommitTransfers(DeceptusU8 index)
{
   SnesSpriteStaging *staging;
   staging = stagingAt(index);
   if (staging->palette_count)
   {
      dmaCopyCGram((u8 *)staging->palette_buffer, 128, staging->palette_count * 32);
   }
   dmaCopyOAram((u8 *)staging->oam, 0, SNES_OBJ_OAM_BYTES);
   snesVramRunDma(staging->tile_dma, staging->tile_dma_count);
   staging->tile_dma_count = 0;
}

/* the sdk copies its oam shadow to oam at every vblank it waited for, so the shadow follows the
 * frame the commit showed. no video access, so this may run after vblank. */
#ifdef SNES_SA1
/* the frame's oam lies in BW-RAM: a dma through the work RAM port takes about three lines, a cpu
 * copy about twenty-five. it uses channel six, which snesVramRunDma sets up in full for every
 * list: the sdk's channel zero keeps its own setup. */
#define SNES_PUBLISH_DMA_CHANNEL (1 << 6)
/* the work RAM port: a dma to it fills work RAM from any address outside work RAM. */
#define SNES_WRAM_ADDRESS_LOW (*(vuint16 *)0x2181)
#define SNES_WRAM_ADDRESS_BANK (*(vuint8 *)0x2183)
#define SNES_WRAM_DATA_PORT 0x80

void snesSpritesPublish(DeceptusU8 index)
{
   dmaMemory source, target;
   source.mem.p = (void *)stagingAt(index)->oam;
   target.mem.p = (void *)oamMemory;
   SNES_WRAM_ADDRESS_LOW = target.mem.c.addr;
   SNES_WRAM_ADDRESS_BANK = target.mem.c.bank & 1;
   REG_DMAP6 = 0;
   REG_BBAD6 = SNES_WRAM_DATA_PORT;
   REG_A1T6LH = source.mem.c.addr;
   REG_A1B6 = source.mem.c.bank;
   REG_DAS6LH = SNES_OBJ_OAM_BYTES;
   REG_MDMAEN = SNES_PUBLISH_DMA_CHANNEL;
}
#else
/* the frame's oam lies in work RAM, which dma cannot copy into work RAM. */
void snesSpritesPublish(DeceptusU8 index)
{
   memcpy(oamMemory, stagingAt(index)->oam, SNES_OBJ_OAM_BYTES);
}
#endif

DeceptusU8 snesSpritesBlank(DeceptusU8 index)
{
   return stagingAt(index)->blank;
}

DeceptusU8 snesSpritesOverflow(void)
{
   const DeceptusU8 *overflow;
   overflow = &snes_sprite_overflow;
   return *overflow;
}

DeceptusU16 snesSpritesUploadBytes(DeceptusU8 index)
{
   return stagingAt(index)->upload_bytes;
}

DeceptusU16 snesSpritesObjects(void)
{
   const DeceptusU16 *count;
   count = &snes_obj_count;
   return *count;
}

DeceptusU16 snesSpritesPalettes(DeceptusU8 index)
{
   return stagingAt(index)->palette_count;
}
