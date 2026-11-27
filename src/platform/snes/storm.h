#ifndef SNES_STORM_H
#define SNES_STORM_H

/* the thunderstorm in two halves, as the sprites: preparation (storm.c, on the SA-1 when the
 * cartridge has one) stages the room's colours with the lit sky entries brightened; the S-CPU
 * (storm_commit.c) uploads them to CGRAM in vblank. */
#include "deceptus/adventure.h"

/* the room's colours in CGRAM, after the UI bank. */
#define SNES_STORM_FIRST_COLOUR 16
#define SNES_STORM_COLOURS 112
/* vblank lines the upload needs: the library call and 224 bytes of dma, with a margin. */
#define SNES_STORM_COMMIT_LINES 4

/* one per presentation buffer, as the sprite stagings. */
#define SNES_STORM_STAGINGS 2
typedef struct SnesStormStaging
{
   DeceptusU16 colours[SNES_STORM_COLOURS];
   DeceptusU8 pending;
} SnesStormStaging;

extern SnesStormStaging snes_storm_stagings[SNES_STORM_STAGINGS];

/* the room's colours were just uploaded unlit. */
void snesStormReset(void);
/* stages the room's colours into the buffer when the flash level changed. */
void snesStormStage(const DeceptusAdventure *game, DeceptusU8 buffer);
/* vblank lines the buffer's upload takes. */
DeceptusU16 snesStormCommitLines(DeceptusU8 buffer);
/* uploads the buffer's staged colours, if any. */
void snesStormCommit(DeceptusU8 buffer);

#endif
