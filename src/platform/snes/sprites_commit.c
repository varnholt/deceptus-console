/* the S-CPU half of the sprites: OBJ video setup and the transfers sprites.c staged. the staging
 * may live in SA-1 BW-RAM, so it is reached through its 24-bit address. */
#include "sprites.h"
#include <snes.h>
#include <string.h>

#ifdef SNES_SPRITES_ASM
void snesObjPublish(void);
#endif

void snesSpritesInitVideo(void)
{
   oamInitGfxAttr(0x6000, OBJ_SIZE8_L16);
   oamClear(0, 0);
}

void snesSpritesUploadTiles(void)
{
   SnesSpriteStaging *staging;
   DeceptusU16 first, count;
   staging = &snes_sprite_staging;
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

DeceptusU16 snesSpritesCommitLines(void)
{
   const SnesSpriteStaging *staging;
   staging = &snes_sprite_staging;
   return SNES_COMMIT_FIXED_LINES + staging->tile_dma_count;
}

/* every video transfer of the sprite commit: palettes, oam and the staged tiles the oam refers to,
   all in one vblank. */
void snesSpritesCommitTransfers(void)
{
   SnesSpriteStaging *staging;
   staging = &snes_sprite_staging;
   if (staging->palette_count)
   {
      dmaCopyCGram((u8 *)staging->palette_buffer, 128, staging->palette_count * 32);
   }
   dmaCopyOAram(snes_obj_oam, 0, SNES_OBJ_OAM_BYTES);
   snesVramRunDma(staging->tile_dma, staging->tile_dma_count);
   staging->tile_dma_count = 0;
}

/* bookkeeping without video writes, which may run after vblank. */
void snesSpritesPublish(void)
{
#ifdef SNES_SPRITES_ASM
   /* the sdk's next vblank also copies this shadow to oam. */
   snesObjPublish();
#else
   DeceptusU16 index;
   SnesObjSlot *slots;
   slots = snes_obj_slots;
   memcpy(oamMemory, snes_obj_oam, SNES_OBJ_OAM_BYTES);
   for (index = 0; index < SNES_OBJ_PIECES; ++index)
   {
      slots[index].held = slots[index].used;
   }
#endif
}

DeceptusU8 snesSpritesBlank(void)
{
   const SnesSpriteStaging *staging;
   staging = &snes_sprite_staging;
   return staging->blank;
}

DeceptusU8 snesSpritesOverflow(void)
{
   const DeceptusU8 *overflow;
   overflow = &snes_sprite_overflow;
   return *overflow;
}

DeceptusU16 snesSpritesUploadBytes(void)
{
   const SnesSpriteStaging *staging;
   staging = &snes_sprite_staging;
   return staging->upload_bytes;
}

DeceptusU16 snesSpritesObjects(void)
{
   const DeceptusU16 *count;
   count = &snes_obj_count;
   return *count;
}

DeceptusU16 snesSpritesPalettes(void)
{
   const SnesSpriteStaging *staging;
   staging = &snes_sprite_staging;
   return staging->palette_count;
}
