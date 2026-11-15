#ifndef DECEPTUS_SMD_OPENING_VIDEO_H
#define DECEPTUS_SMD_OPENING_VIDEO_H

#include "smd_assets.h"

#define SMD_WORLD_SLOTS 1152
#define SMD_OBJECT_FIRST 1152
#define SMD_OBJECT_SLOTS 64
#define SMD_FONT_FIRST 1408

extern deceptus_u8 smd_graphics_error, smd_background_error, smd_background_full;
extern deceptus_u16 smd_background_dma_bytes, smd_background_uploads;

deceptus_u16 smd_graphics_tile(deceptus_u16 logical);
deceptus_u8 smd_decode_block(const smd_rom_blob *blob, deceptus_u16 block, deceptus_u8 *destination);
const deceptus_u8 *smd_cell(const smd_rom_blob *blob, deceptus_u16 row, deceptus_u16 column);
void smd_background_load(deceptus_u8 room);
void smd_background_set_flags(deceptus_u16 flags);
void smd_background_prepare(deceptus_i16 camera_x, deceptus_i16 camera_y);
void smd_background_commit(void);

#endif
