#ifndef SNES_SPRITES_H
#define SNES_SPRITES_H

/* sprites in two halves: preparation (sprites.c) builds the OAM shadow and stages tile and palette
 * transfers without video access, so on the SA-1 cartridge it runs on the SA-1; the S-CPU half
 * (sprites_commit.c) sets up OBJ video and performs the transfers. */
#include "deceptus/adventure.h"
#include "vram_dma.h"

#define SNES_OBJ_PIECES 128
#define SNES_OBJ_OAM_BYTES 544
/* vblank lines the sprite commit needs, measured: palettes and oam take about six with their
   library calls, each staged 64-byte tile transfer about one; plus a margin. */
#define SNES_COMMIT_FIXED_LINES 10
/* newly needed pieces the commit vblank takes; more are uploaded in vblanks of their own. */
#define SNES_DEFERRED_PIECES 8
/* pieces one vblank of its own takes: two transfers each at about 0.7 lines, after the sdk's five
   lines of interrupt handler, within the 37 lines of vblank. */
#define SNES_PRELOAD_PIECES 16

typedef struct SnesObjSlot
{
   DeceptusU16 resource;
   DeceptusU8 piece, valid, used, held, pending, next;
} SnesObjSlot;

/* what preparation hands to the transfers of one presentation. */
typedef struct SnesSpriteStaging
{
   /* the frame's object palettes in cgram order, so one transfer carries them all. */
   DeceptusU16 palette_buffer[8][16];
   DeceptusU16 palette_count;
   /* both 64-byte halves of every newly needed piece. */
   SnesVramDmaCommand tile_dma[SNES_OBJ_PIECES * 2];
   DeceptusU16 tile_dma_count, upload_bytes;
   /* blank: tile pressure needs forced blank. forced_blank: the tiles go up without waiting for
    * vblank. deferred: few enough tiles for the commit vblank to carry them. */
   DeceptusU8 blank, forced_blank, deferred;
} SnesSpriteStaging;

/* the OAM shadow and the tile slots (shared with sprites_*.asm), the staging, the frame counters. */
extern DeceptusU8 snes_obj_oam[SNES_OBJ_OAM_BYTES];
extern SnesObjSlot snes_obj_slots[SNES_OBJ_PIECES];
extern SnesSpriteStaging snes_sprite_staging;
extern DeceptusU16 snes_obj_count;
extern DeceptusU8 snes_sprite_overflow;

/* preparation: once per room, once per presentation, then staging of its transfers. */
void snesSpritesReset(void);
void snesSpritesPrepare(const DeceptusAdventure *game, const DeceptusFrame *frame);
void snesSpritesStage(void);

/* S-CPU: OBJ video setup at room load; the tiles that do not wait for the commit vblank (forced
 * blank at tile pressure, then vblanks of their own); the commit; bookkeeping after it. */
void snesSpritesInitVideo(void);
void snesSpritesUploadTiles(void);
DeceptusU16 snesSpritesCommitLines(void);
void snesSpritesCommitTransfers(void);
void snesSpritesPublish(void);

/* the prepared presentation, for the main loop and the emulator checks. */
DeceptusU8 snesSpritesBlank(void);
DeceptusU8 snesSpritesOverflow(void);
DeceptusU16 snesSpritesUploadBytes(void);
DeceptusU16 snesSpritesObjects(void);
DeceptusU16 snesSpritesPalettes(void);

#endif
