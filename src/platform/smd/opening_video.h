#ifndef DECEPTUS_SMD_OPENING_VIDEO_H
#define DECEPTUS_SMD_OPENING_VIDEO_H

#include "smd_assets.h"

#define SMD_WORLD_SLOTS 1152
#define SMD_OBJECT_FIRST 1152
#define SMD_OBJECT_SLOTS 64
#define SMD_FONT_FIRST 1408

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
