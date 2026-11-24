#ifndef DECEPTUS_SNES_BACKGROUND_H
#define DECEPTUS_SNES_BACKGROUND_H

#include "deceptus/types.h"

extern DeceptusU16 snes_background_dma_bytes;
extern DeceptusU16 snes_background_compactions;
extern DeceptusU16 snes_background_hash_probes, snes_background_hash_rebuilds;

/* preparation (background.c): performs no video writes, so it may run on the SA-1. a room load
 * is snesBackgroundLoadVideo on the S-CPU first, then snesBackgroundLoad. a prepared frame goes
 * into one of two packets, which its commit reads while the next frame is prepared into the
 * other. */
void snesBackgroundLoad(DeceptusU8 room);
void snesBackgroundSetFlags(DeceptusU16 flags);
void snesBackgroundPrepare(DeceptusI16 camera_x, DeceptusI16 camera_y);
void snesBackgroundStage(DeceptusU8 packet);

/* the S-CPU half (background_commit.c), on a packet the preparation is done with. room loads and
 * full refreshes require forced blank until all video commits finish; ordinary commits run during
 * vblank. */
void snesBackgroundLoadVideo(DeceptusU8 room);
void snesBackgroundCommit(DeceptusU8 packet);
/* an error stops the background; uploads counts the patterns the last prepared frame transfers. */
DeceptusU8 snesBackgroundError(void);
DeceptusU16 snesBackgroundUploads(void);
/* the packet's commit needs forced blank. */
DeceptusU8 snesBackgroundFull(DeceptusU8 packet);
/* the packet's commit reads the work area, so it must finish before the next preparation. */
DeceptusU8 snesBackgroundSerial(DeceptusU8 packet);

#endif
