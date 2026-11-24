/* the S-CPU half of the background: video registers at room load, and the VBlank commit of what
 * snesBackgroundPrepare staged in the work area. */
#include "background.h"
#include "background_work.h"
#include <snes.h>
#ifdef SNES_SA1
#include "sa1.h"
#endif

void snesBackgroundLoadVideo(DeceptusU8 room)
{
   if (room >= SNES_STAGE_COUNT)
   {
      room = 0;
   }
#ifdef SNES_SA1
   /* before the room's resources are read: they live in the block the F0-FF window shows. */
   SNES_SA1_FXB = snes_stage_rom_blocks[room];
#endif
   bgSetGfxPtr(0, 0x2000);
   bgSetGfxPtr(1, 0x1000);
   bgSetMapPtr(0, 0x0000, SC_64x32);
   bgSetMapPtr(1, 0x0800, SC_64x32);
   dmaCopyCGram((u8 *)snes_stages[room].palette + 32, 16, 224);
}

DeceptusU8 snesBackgroundError(void)
{
   return work->status.error;
}

DeceptusU8 snesBackgroundFull(void)
{
   return work->status.full;
}

DeceptusU16 snesBackgroundUploads(void)
{
   return work->status.uploads;
}

static void copyColumn(const DeceptusU16 *source, DeceptusU16 address)
{
   dmaMemory memory;
   memory.mem.p = (void *)source;
   REG_VMAIN = 0x81;
   REG_VMADDLH = address;
   REG_DMAP0 = 1;
   REG_BBAD0 = 0x18;
   REG_A1T0LH = memory.mem.c.addr;
   REG_A1B0 = memory.mem.c.bank;
   REG_DAS0LH = 64;
   REG_MDMAEN = 1;
   REG_VMAIN = 0x80;
}

void snesBackgroundCommit(void)
{
   DeceptusU16 index, slot, pattern, plane, row, column, base, mask, first, batch;
   const DeceptusU8 *bytes;
   if (work->status.error)
   {
      return;
   }
   if (work->dma_count != SNES_BG_EMPTY)
   {
      if (work->status.full || (work->dma_batch_count <= 1))
      {
         snesVramRunDma(work->dma, work->dma_count);
      }
      else
      {
         /* the frame on screen stays until the last batch, then the scroll moves to the new one. */
         first = 0;
         for (batch = 0; batch < work->dma_batch_count; ++batch)
         {
            if (batch)
            {
               WaitForVBlank();
            }
            snesVramRunDma(&work->dma[first], work->dma_batch_ends[batch] - first);
            first = work->dma_batch_ends[batch];
         }
      }
      bgSetScroll(0, work->scroll_x, work->scroll_y);
      bgSetScroll(1, work->scroll_x, work->scroll_y);
      work->status.full = 0;
      return;
   }
   for (index = 0; index < work->upload_count; ++index)
   {
      slot = work->uploads[index];
      pattern = work->resident[slot];
      if (pattern != SNES_BG_EMPTY)
      {
         bytes = work->stage->patterns.chunks[pattern >> 10] + (pattern & 1023) * 32;
         dmaCopyVram((u8 *)bytes, SNES_BG_SLOT_ADDRESS(slot), 32);
      }
      work->pending[slot] = 0;
   }
   work->upload_count = 0;
   for (plane = 0; plane < 2; ++plane)
   {
      base = plane ? 0x0800 : 0;
      mask = work->dirty_rows[plane][0];
      for (row = 0; row < 32; ++row)
      {
         if (row == 16)
         {
            mask = work->dirty_rows[plane][1];
         }
         if (mask & 1)
         {
            dmaCopyVram((u8 *)&work->maps[plane][row * 32], base + row * 32, 64);
            dmaCopyVram((u8 *)&work->maps[plane][1024 + row * 32], base + 1024 + row * 32, 64);
         }
         mask >>= 1;
      }
      work->dirty_rows[plane][0] = 0;
      work->dirty_rows[plane][1] = 0;
      column = work->dirty_column[plane];
      if (column < 64)
      {
         for (row = 0; row < 32; ++row)
         {
            index = (column & 31) + row * 32 + (column >= 32 ? 1024 : 0);
            slot = work->maps[plane][index];
            work->column[row] = slot;
         }
         copyColumn(work->column, base + (column & 31) + (column >= 32 ? 1024 : 0));
         work->dirty_column[plane] = 255;
      }
   }
   bgSetScroll(0, work->scroll_x, work->scroll_y);
   bgSetScroll(1, work->scroll_x, work->scroll_y);
   work->status.full = 0;
}
