#ifndef DECEPTUS_SMD_OPENING_VIDEO_H
#define DECEPTUS_SMD_OPENING_VIDEO_H

#include "smd_assets.h"

/* 16x16 sprite pieces take four tiles each. the crypt's crusher alone needs about 40 pieces, so
   with the player, head torch and beam 64 pieces ran out; the background cache gives up 64 tiles. */
#define SMD_OBJECT_TILES_PER_SLOT 4
#define SMD_WORLD_SLOTS 1088
#define SMD_OBJECT_FIRST SMD_WORLD_SLOTS
#define SMD_OBJECT_SLOTS 80
#define SMD_FONT_FIRST 1408
#if SMD_OBJECT_FIRST + SMD_OBJECT_SLOTS * SMD_OBJECT_TILES_PER_SLOT != SMD_FONT_FIRST
#error the sprite piece slots must end where the font begins
#endif

extern DeceptusU8 smd_graphics_error, smd_background_error, smd_background_full;
extern DeceptusU16 smd_background_dma_bytes, smd_background_uploads;

DeceptusU16 smdGraphicsTile(DeceptusU16 logical);
DeceptusU8 smdDecodeBlock(const SmdRomBlob *blob, DeceptusU16 block, DeceptusU8 *destination);
const DeceptusU8 *smdCell(const SmdRomBlob *blob, DeceptusU16 row, DeceptusU16 column);
void smdBackgroundLoad(DeceptusU8 room);
void smdBackgroundSetFlags(DeceptusU16 flags);
void smdBackgroundPrepare(DeceptusI16 camera_x, DeceptusI16 camera_y);
void smdBackgroundCommit(void);

#endif
