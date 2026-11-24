#ifndef SNES_BACKGROUND_WORK_H
#define SNES_BACKGROUND_WORK_H

/* the background work area shared by preparation (background.c, on the SA-1 when the cartridge
 * has one) and the VBlank commit (background_commit.c, always on the S-CPU). both reach it through
 * its 24-bit address, never through a data bank. */

#include "vram_dma.h"
#include "snes_assets.h"

/* slots 0-1023 are VRAM 2000-5fff; the foreground also addresses 120 overflow patterns at 1880-1fff,
 * tile 136 of its character base 1000, below the interface font. */
#define SNES_BG_SLOTS 1144
/* slots both planes address: the background's first 768, the foreground's tiles 256-1023. */
#define SNES_BG_SHARED_SLOTS 768
/* a source pattern word with this bit names a static cell: the converter fixed its slot, and the
 * low ten bits of its attribute word hold the slot's tile on the cell's plane. */
#define SNES_BG_STATIC_CELL 0x8000U
#define SNES_BG_PATTERN_BITS 0x3fffU
#define SNES_BG_OVERFLOW_TILE 136
#define SNES_BG_OVERFLOW_ADDRESS 0x1880
#define SNES_BG_HASH 2048
#define SNES_BG_WIDTH 33
#define SNES_BG_HEIGHT 29
#define SNES_BG_CLIPS 8
#define SNES_BG_EMPTY 65535U
#define SNES_BG_DELETED 65534U
#define SNES_BG_DMA_COMMANDS 192
/* vblanks one commit may spread over before it falls back to forced blank. */
#define SNES_BG_DMA_BATCHES 4
/* what a batch may cost in master cycles: 768 per command and 8 per byte, which leaves time for
 * the sdk nmi and the oam transfer in the ntsc vblank. */
#define SNES_BG_DMA_BUDGET 36000U
#define SNES_BG_DMA_COMMAND_COST 768U
#define SNES_BG_NONE 255
/* rows and columns the window may scroll in one presentation before it reloads in forced blank: a
 * presentation that catches up with three ticks moves the camera up to three cells. */
#define SNES_BG_SCROLL_CELLS 3
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
   DeceptusU16 dma_count;
   SnesVramDmaCommand dma[SNES_BG_DMA_COMMANDS];
   DeceptusU16 clip_frame[SNES_BG_CLIPS], clip_elapsed[SNES_BG_CLIPS], clip_cells[SNES_BG_CLIPS];
   DeceptusU8 pending[SNES_BG_SLOTS], domains[SNES_BG_SLOTS];
   /* per plane, the count of map columns (0-63) that changed, then those columns. */
   DeceptusU8 dirty_columns[2][SNES_BG_SCROLL_CELLS + 1];
   /* scratch: the cells a clip refresh changes while preparing, then the map columns the commit
    * transfers, changed ones and those that left the window. */
   union
   {
      DeceptusU8 refresh_changed[SNES_BG_WIDTH * SNES_BG_HEIGHT];
      struct
      {
         DeceptusU16 changed[2][SNES_BG_SCROLL_CELLS][32], released[2][SNES_BG_SCROLL_CELLS][32];
      } columns;
   } scratch;
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
   /* the count of map rows (0-31) and columns (0-63) whose cells left the window this
    * presentation, then those rows and columns. */
   DeceptusU8 released_rows[SNES_BG_SCROLL_CELLS + 1], released_columns[SNES_BG_SCROLL_CELLS + 1];
   /* a commit too long for one vblank runs its commands up to each end in a vblank of its own. */
   DeceptusU8 dma_batch_count;
   DeceptusU16 dma_batch_ends[SNES_BG_DMA_BATCHES];
} SnesBackgroundWork;

typedef char SnesBackgroundStorageFits[(sizeof(SnesBackgroundWork) <= 32768) ? 1 : -1];
extern DeceptusU8 snes_background_storage[];
#define work ((SnesBackgroundWork *)snes_background_storage)
#define DECEPTUS_BG_OFFSET(member) ((unsigned long)&((SnesBackgroundWork *)0)->member)

/* map words a packet carries: the rows and columns of a window that moves three cells on both
 * axes, entering and leaving ones, on both planes. */
#define SNES_BG_PACKET_WORDS (2 * 2 * SNES_BG_SCROLL_CELLS * (64 + 32))
/* the presentation buffers: preparation fills one packet while the S-CPU commits the other. */
#define SNES_BG_PACKETS 2

/* what the commit of one presentation reads. with a second cpu the commands and the map words they
 * send are copied here, so the next preparation may change the work area meanwhile; without one
 * the commit reads the list staged in the work area. */
typedef struct SnesBackgroundPacket
{
   SnesVramDmaCommand dma[SNES_BG_DMA_COMMANDS];
   const SnesVramDmaCommand *commands;
   DeceptusU16 dma_count;
   DeceptusU16 dma_batch_ends[SNES_BG_DMA_BATCHES];
   DeceptusU8 dma_batch_count;
   /* the commit needs forced blank. */
   DeceptusU8 full;
   /* some commands still read the work area: the commit finishes before the next preparation. */
   DeceptusU8 serial;
   DeceptusI16 scroll_x, scroll_y;
   DeceptusU16 map_words[SNES_BG_PACKET_WORDS];
} SnesBackgroundPacket;

extern SnesBackgroundPacket snes_background_packets[SNES_BG_PACKETS];
/* the bytes background_ram.asm reserves for the packets. */
#define SNES_BG_PACKET_STORAGE 8192
typedef char SnesBackgroundPacketsFit[(sizeof(SnesBackgroundPacket) * SNES_BG_PACKETS <= SNES_BG_PACKET_STORAGE) ? 1 : -1];

/* the VRAM word address of a pattern slot: the shared cache, then the foreground overflow. */
#define SNES_BG_SLOT_ADDRESS(slot) (((slot) < 1024) ? (0x2000 + (slot) * 16) : (SNES_BG_OVERFLOW_ADDRESS + ((slot) - 1024) * 16))

#endif
