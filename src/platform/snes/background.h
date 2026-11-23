#ifndef DECEPTUS_SNES_BACKGROUND_H
#define DECEPTUS_SNES_BACKGROUND_H

#include "deceptus/types.h"

extern DeceptusU16 snes_background_dma_bytes;
extern DeceptusU16 snes_background_compactions;
extern DeceptusU16 snes_background_hash_probes, snes_background_hash_rebuilds;

/* preparation (background.c): performs no video writes, so it may run on the SA-1. a room load
 * is snesBackgroundLoadVideo on the S-CPU first, then snesBackgroundLoad. */
void snesBackgroundLoad(DeceptusU8 room);
void snesBackgroundSetFlags(DeceptusU16 flags);
void snesBackgroundPrepare(DeceptusI16 camera_x, DeceptusI16 camera_y);

/* the S-CPU half (background_commit.c). room loads and full refreshes require forced blank until
 * all video commits finish; ordinary commits run during vblank. */
void snesBackgroundLoadVideo(DeceptusU8 room);
void snesBackgroundCommit(void);
/* the prepared frame: an error stops the background, full needs forced blank, uploads counts
 * the patterns the next commit transfers. */
DeceptusU8 snesBackgroundError(void);
DeceptusU8 snesBackgroundFull(void);
DeceptusU16 snesBackgroundUploads(void);

#endif
