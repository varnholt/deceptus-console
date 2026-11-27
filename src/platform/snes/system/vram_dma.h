#ifndef SNES_VRAM_DMA_H
#define SNES_VRAM_DMA_H

#include "deceptus/types.h"

/* one vram transfer of a list prepared outside vblank and run inside it by snesVramRunDma
   (vram_dma.asm), which reads these eight bytes per command. */
typedef struct SnesVramDmaCommand
{
   DeceptusU16 source;
   DeceptusU8 bank, increment;
   DeceptusU16 target, bytes;
} SnesVramDmaCommand;

typedef char SnesVramDmaCommandSize[(sizeof(SnesVramDmaCommand) == 8) ? 1 : -1];

void snesVramRunDma(const SnesVramDmaCommand* commands, DeceptusU16 count);

#endif
