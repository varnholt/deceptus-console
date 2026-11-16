#ifndef DECEPTUS_SNES_BACKGROUND_H
#define DECEPTUS_SNES_BACKGROUND_H

#include "deceptus/types.h"

extern DeceptusU8 snes_background_error, snes_background_full;
extern DeceptusU16 snes_background_uploads;
extern DeceptusU16 snes_background_dma_bytes;
extern DeceptusU16 snes_background_compactions;
extern DeceptusU16 snes_background_hash_probes, snes_background_hash_rebuilds;

/* room loads and full refreshes require forced blank until all video commits
 * finish. ordinary commits run during vblank; prepare performs no video writes. */
void snesBackgroundLoad(DeceptusU8 room);
void snesBackgroundSetFlags(DeceptusU16 flags);
void snesBackgroundPrepare(DeceptusI16 camera_x, DeceptusI16 camera_y);
void snesBackgroundCommit(void);

#endif
