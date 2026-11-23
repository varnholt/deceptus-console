#ifndef SNES_BACKGROUND_WORK_H
#define SNES_BACKGROUND_WORK_H

/* the background work area shared by preparation (background.c, on the SA-1 when the cartridge
 * has one) and the VBlank commit (background_commit.c, always on the S-CPU). both reach it through
 * its 24-bit address, never through a data bank. */

#include "vram_dma.h"
#include "snes_assets.h"

#define SNES_BG_SLOTS 1072
#define SNES_BG_HASH 2048
#define SNES_BG_WIDTH 33
#define SNES_BG_HEIGHT 29
#define SNES_BG_CLIPS 8
#define SNES_BG_EMPTY 65535U
#define SNES_BG_DELETED 65534U
#define SNES_BG_DMA_COMMANDS 192
#define SNES_BG_ROW_TABLE 1024

/* what the commit and the main loop need to know about the prepared frame. */
typedef struct SnesBackgroundStatus
{
   /* nonzero after malformed resources (1) or an exhausted cache (2); nothing is committed. */
   DeceptusU8 error;
   /* the next commit needs forced blank. */
   DeceptusU8 full;
   /* patterns the next commit uploads. */
   DeceptusU16 uploads;
} SnesBackgroundStatus;

typedef struct SnesBackgroundWork
{
   DeceptusU16 resident[SNES_BG_SLOTS], references[SNES_BG_SLOTS], free_slots[SNES_BG_SLOTS];
   DeceptusU16 hash_keys[SNES_BG_HASH], hash_slots[SNES_BG_HASH];
   DeceptusU16 maps[2][2048], uploads[SNES_BG_SLOTS], column[32];
   DeceptusU16 staged_columns[2][32], dma_count;
   SnesVramDmaCommand dma[SNES_BG_DMA_COMMANDS];
   DeceptusU16 clip_frame[SNES_BG_CLIPS], clip_elapsed[SNES_BG_CLIPS], clip_cells[SNES_BG_CLIPS];
   DeceptusU8 pending[SNES_BG_SLOTS], domains[SNES_BG_SLOTS], dirty_column[2];
   DeceptusU8 refresh_changed[SNES_BG_WIDTH * SNES_BG_HEIGHT];
   DeceptusU16 dirty_rows[2][2], dma_bytes, free_count, upload_count, flags, room_cells, hash_tombstones;
   DeceptusI16 left, top;
   DeceptusU8 loaded;
   /* first source cell of each room row. the compiler multiplies through a
    * library routine, which cost a noticeable share of every streamed cell. */
   DeceptusU16 row_offsets[SNES_BG_ROW_TABLE];
   /* the loaded room and the scroll position the next commit publishes. */
   const SnesStage *stage;
   DeceptusI16 scroll_x, scroll_y;
   SnesBackgroundStatus status;
} SnesBackgroundWork;

typedef char SnesBackgroundStorageFits[(sizeof(SnesBackgroundWork) <= 32768) ? 1 : -1];
extern DeceptusU8 snes_background_storage[];
#define work ((SnesBackgroundWork *)snes_background_storage)
#define DECEPTUS_BG_OFFSET(member) ((unsigned long)&((SnesBackgroundWork *)0)->member)

/* the VRAM word address of a pattern slot: the shared cache, then the foreground overflow. */
#define SNES_BG_SLOT_ADDRESS(slot) (((slot) < 1024) ? (0x2000 + (slot) * 16) : (0x1d00 + ((slot) - 1024) * 16))

#endif
