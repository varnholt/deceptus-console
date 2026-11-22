#ifndef SNES_PPU_TIMING_H
#define SNES_PPU_TIMING_H

#include "deceptus/types.h"

/* ntsc vblank: lines 225 to 261 of 262. video memory accepts writes only then (or in forced blank). */
#define SNES_VBLANK_FIRST_LINE 225
#define SNES_LINES_PER_FRAME 262

/* written by the latch reads below: the compiler drops volatile reads whose value is unused. */
static DeceptusU8 snes_ppu_latch_read;

/* the line the ppu is drawing, from the h/v counter latch. latching on a $2137 read needs bit 7 of
   $4201 set; the other bits drive the unused i/o port pins, which stay high. */
static DeceptusU16 snesCurrentLine(void)
{
   DeceptusU16 low, high;
   *(volatile DeceptusU8 *)0x4201 = 0xff;
   snes_ppu_latch_read = *(volatile DeceptusU8 *)0x213f;
   snes_ppu_latch_read = *(volatile DeceptusU8 *)0x2137;
   low = *(volatile DeceptusU8 *)0x213d;
   high = *(volatile DeceptusU8 *)0x213d & 1;
   return low | (high << 8);
}

/* whole lines of vblank still ahead; zero outside vblank. */
static DeceptusU16 snesVblankLinesLeft(void)
{
   DeceptusU16 line;
   line = snesCurrentLine();
   if (line < SNES_VBLANK_FIRST_LINE || line >= SNES_LINES_PER_FRAME)
   {
      return 0;
   }
   return SNES_LINES_PER_FRAME - line;
}

#endif
