#ifndef DECEPTUS_AMIGA_TILEARCHIVE_H
#define DECEPTUS_AMIGA_TILEARCHIVE_H

#include "amiga_native_assets.h"
#include "deceptus/types.h"

/* each 16x16 cell refers to four 8x8 dictionary tiles: rows outermost, one byte per plane. */
#define AMIGA_NATIVE_TILES_PER_BANK 32
#define AMIGA_NATIVE_TILE_BYTES (8 * AMIGA_NATIVE_PLANES)
#define AMIGA_NATIVE_MASK_BYTES 8
#define AMIGA_NATIVE_BANK_BYTES (AMIGA_NATIVE_TILES_PER_BANK * AMIGA_NATIVE_TILE_BYTES)

typedef struct AmigaTileArchive
{
   const DeceptusU8* bytes;
   DeceptusU32 size, color_map, mask_map, quad_table, color_table, mask_table, payload;
   DeceptusU16 width, height, columns, rows, colors, masks, quads;
} AmigaTileArchive;

/* bytes remain caller-owned for the lifetime of the archive. */
DeceptusU8 amigaTileArchiveInit(AmigaTileArchive* archive, const DeceptusU8* bytes, DeceptusU32 size);
/* the four quarters of a 16x16 cell, top-left first; mask id 0 is the empty mask. */
DeceptusU8
amigaTileArchiveCell(const AmigaTileArchive* archive, DeceptusU16 column, DeceptusU16 row, DeceptusU16* colors, DeceptusU16* masks);
/* mask_bank selects the one-plane mask dictionary instead of the five-plane colors. */
const DeceptusU8* amigaTileArchiveRawBank(const AmigaTileArchive* archive, DeceptusU8 mask_bank, DeceptusU16 bank);
DeceptusU8 amigaTileArchiveDecode(
   const AmigaTileArchive* archive,
   DeceptusU8 mask_bank,
   DeceptusU16 bank,
   DeceptusU8* destination,
   DeceptusU32 capacity
);

#endif
