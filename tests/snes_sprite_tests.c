/* run after generating SNES resources; exercise the actual backend helpers. */
#include "snes_assets.h"
#include <stdio.h>
#include <string.h>

typedef DeceptusU8 u8;
typedef DeceptusU16 u16;
#define OBJ_SIZE8_L16 0
extern u8 oamMemory[544];
void dmaCopyVram(u8 *source, u16 address, u16 size);
void dmaCopyCGram(u8 *source, u16 address, u16 size);
void dmaCopyOAram(u8 *source, u16 address, u16 size);
void oamInitGfxAttr(u16 address, u8 size);
void oamClear(u16 first, u8 size);
void WaitForVBlank(void);
void setScreenOff(void);
#include "../src/platform/snes/sprites.h"

#define CHECK(value)                                                                                                                       \
   do                                                                                                                                      \
   {                                                                                                                                       \
      if (!(value))                                                                                                                        \
      {                                                                                                                                    \
         printf("line %d: %s\n", __LINE__, #value);                                                                                        \
         return 1;                                                                                                                         \
      }                                                                                                                                    \
   } while (0)

int main(void)
{
   DeceptusU8 covered[512];
   DeceptusU16 palettes[9][16], duplicate[16];
   DeceptusU16 index, tile, other;
   memset(covered, 0, sizeof(covered));
   for (index = 0; index < 128; ++index)
   {
      tile = snesObjTile(index);
      CHECK(tile + 17 < 512);
      CHECK((tile & 15) <= 14);
      CHECK(!covered[tile] && !covered[tile + 1] && !covered[tile + 16] && !covered[tile + 17]);
      covered[tile] = covered[tile + 1] = covered[tile + 16] = covered[tile + 17] = 1;
   }
   for (index = 0; index < 512; ++index)
   {
      CHECK(covered[index]);
   }
   snesObjResetCache();
   CHECK(snesObjSlotFor(99, 0) == 0);
   snes_obj_slots[0].used = 0;
   CHECK(snesObjSlotFor(100, 0) == 1);
   CHECK(snesObjSlotFor(99, 0) == 0);
   snesObjResetCache();
   for (index = 0; index < 128; ++index)
   {
      CHECK(snesObjSlotFor(index, 0) == index);
   }
   CHECK(snesObjSlotFor(17, 0) == 17);
   CHECK(snesObjSlotFor(1000, 0) == 65535);
   CHECK(snesObjSlotFor(1001, 0) == 65535);
   CHECK(snes_sprite_overflow);
   snes_obj_slots[37].used = snes_obj_slots[37].held = 0;
   snes_obj_free_cursor = 0;
   CHECK(snesObjSlotFor(1000, 0) == 37);
   CHECK(!snes_obj_heads[37]);
   snes_obj_slots[52].used = snes_obj_slots[52].held = 0;
   snes_obj_free_cursor = 0;
   CHECK(snesObjSlotFor(37, 0) == 52);
   memset(palettes, 0, sizeof(palettes));
   snes_obj_palette_count = 0;
   snes_sprite_overflow = 0;
   for (index = 0; index < 8; ++index)
   {
      palettes[index][1] = index + 1;
      CHECK(snesObjPalette(palettes[index]) == index);
   }
   memcpy(duplicate, palettes[3], sizeof(duplicate));
   CHECK(snesObjPalette(duplicate) == 3 && snes_obj_palette_count == 8);
   palettes[8][1] = 99;
   snesObjPalette(palettes[8]);
   CHECK(snes_sprite_overflow);
   snes_obj_count = 0;
   snes_sprite_overflow = 0;
   memset(snes_obj_lines, 0, sizeof(snes_obj_lines));
   memset(snes_obj_slivers, 0, sizeof(snes_obj_slivers));
   memset(snes_obj_oam, 0, sizeof(snes_obj_oam));
   for (index = 0; index < 17; ++index)
   {
      snesObjPiece(0, 40, 0, 0, 0, 2, 16);
   }
   CHECK(snes_obj_count == 17 && !snes_sprite_overflow);
   snesObjPiece(0, 40, 0, 0, 0, 2, 16);
   CHECK(snes_obj_count == 17 && snes_sprite_overflow);
   for (other = 40; other < 56; ++other)
   {
      CHECK(snes_obj_slivers[other] == 34 && snes_obj_lines[other] == 17);
   }
   snes_obj_count = 0;
   snesObjPiece(-8, 100, 256, 3, 1, 2, 16);
   CHECK(snes_obj_oam[0] == 248 && (snes_obj_oam[512] & 3) == 3);
   CHECK(snes_obj_oam[3] == (1 | (3 << 1) | (2 << 4) | 0x40));
   snes_obj_count = 0;
   snes_sprite_overflow = 0;
   memset(snes_obj_lines, 0, sizeof(snes_obj_lines));
   memset(snes_obj_slivers, 0, sizeof(snes_obj_slivers));
   memset(snes_obj_oam, 0, sizeof(snes_obj_oam));
   for (index = 0; index < 16; ++index)
   {
      snesObjPiece(0, 40, 0, 0, 0, 2, 16);
   }
   snesObjPiece(0, 40, 0, 0, 0, 2, 8);
   snesObjPiece(8, 40, 1, 0, 0, 2, 8);
   CHECK(snes_obj_count == 18 && !snes_sprite_overflow);
   CHECK((snes_obj_oam[516] & 15) == 0);
   snesObjPiece(16, 40, 2, 0, 0, 2, 8);
   CHECK(snes_obj_count == 18 && snes_sprite_overflow);
   puts("SNES tile layout, residency, palettes, scanline budget and signed OAM checks passed");
   return 0;
}
