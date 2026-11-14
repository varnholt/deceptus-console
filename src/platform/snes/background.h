#ifndef DECEPTUS_SNES_BACKGROUND_H
#define DECEPTUS_SNES_BACKGROUND_H

#include "deceptus/types.h"

extern deceptus_u8 snes_background_error, snes_background_full;
extern deceptus_u16 snes_background_uploads;
extern deceptus_u16 snes_background_dma_bytes;
extern deceptus_u16 snes_background_compactions;
extern deceptus_u16 snes_background_hash_probes, snes_background_hash_rebuilds;

/* room loads and full refreshes require forced blank until all video commits
 * finish. ordinary commits run during vblank; prepare performs no video writes. */
void snes_background_load(deceptus_u8 room);
void snes_background_set_flags(deceptus_u16 flags);
void snes_background_prepare(deceptus_i16 camera_x, deceptus_i16 camera_y);
void snes_background_commit(void);

#endif
