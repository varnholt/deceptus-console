#ifndef SNES_SA1_H
#define SNES_SA1_H

#include "deceptus/types.h"

/* SA-1 cartridge registers written by the S-CPU: the 1 MiB ROM block shown in each window. */
#define SNES_SA1_CXB (*(volatile DeceptusU8*)0x2220)
#define SNES_SA1_DXB (*(volatile DeceptusU8*)0x2221)
#define SNES_SA1_EXB (*(volatile DeceptusU8*)0x2222)
#define SNES_SA1_FXB (*(volatile DeceptusU8*)0x2223)

/* releases the SA-1 into its start-up code (sa1_boot.asm). */
void snesSa1Start(void);

/* the ROM block (3-7) holding each room's resources; the build links them into the F0-FF window. */
extern const DeceptusU8 snes_stage_rom_blocks[];

/* the hardware's power-on blocks, written once so every emulator applies them: some map the LoROM
 * windows like plain LoROM until the first write, which hides the FastROM library code in bank 81. */
static void snesSa1MapDefaultBlocks(void)
{
   SNES_SA1_CXB = 0;
   SNES_SA1_DXB = 1;
   SNES_SA1_EXB = 2;
   SNES_SA1_FXB = 3;
}

#endif
