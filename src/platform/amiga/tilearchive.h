#ifndef DECEPTUS_AMIGA_TILEARCHIVE_H
#define DECEPTUS_AMIGA_TILEARCHIVE_H

#include "deceptus/types.h"

#define AMIGA_NATIVE_TILES_PER_BANK 16
#define AMIGA_NATIVE_TILE_BYTES 160
#define AMIGA_NATIVE_MASK_BYTES 32
#define AMIGA_NATIVE_BANK_BYTES (AMIGA_NATIVE_TILES_PER_BANK * AMIGA_NATIVE_TILE_BYTES)

typedef struct AmigaTileArchive
{
   const DeceptusU8 *bytes;
   DeceptusU32 size, color_map, mask_map, color_table, mask_table, payload;
   DeceptusU16 width, height, columns, rows, colors, masks;
} AmigaTileArchive;

/* bytes remain caller-owned for the lifetime of the archive. */
DeceptusU8 amigaTileArchiveInit(AmigaTileArchive *archive, const DeceptusU8 *bytes, DeceptusU32 size);
DeceptusU8 amigaTileArchiveCell(const AmigaTileArchive *archive, DeceptusU16 column, DeceptusU16 row, DeceptusU16 *color,
                                DeceptusU16 *mask);
/* mask_bank selects the one-plane mask dictionary instead of the five-plane colors. */
DeceptusU8 amigaTileArchiveDecode(const AmigaTileArchive *archive, DeceptusU8 mask_bank, DeceptusU16 bank, DeceptusU8 *destination,
                                  DeceptusU32 capacity);

#endif
