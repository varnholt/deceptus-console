#include "tilearchive.h"
#include "deceptus/compression.h"
#include <string.h>

static DeceptusU16 readWord(const DeceptusU8 *bytes)
{
   return ((DeceptusU16)bytes[0] << 8) | bytes[1];
}

static DeceptusU32 readLong(const DeceptusU8 *bytes)
{
   return ((DeceptusU32)readWord(bytes) << 16) | readWord(bytes + 2);
}

static DeceptusU16 bankCount(DeceptusU16 tiles)
{
   return (DeceptusU16)(((DeceptusU32)tiles + AMIGA_NATIVE_TILES_PER_BANK - 1) / AMIGA_NATIVE_TILES_PER_BANK);
}

static DeceptusU8 validateBanks(const AmigaTileArchive *archive, DeceptusU32 table, DeceptusU16 count, DeceptusU32 *previous)
{
   DeceptusU16 bank;
   DeceptusU32 next;
   if (readLong(archive->bytes + table) != *previous)
   {
      return 0;
   }
   for (bank = 0; bank < count; ++bank)
   {
      next = readLong(archive->bytes + table + ((DeceptusU32)bank + 1) * 4);
      if (next <= *previous || next > archive->size - archive->payload)
      {
         return 0;
      }
      *previous = next;
   }
   return 1;
}

DeceptusU8 amigaTileArchiveInit(AmigaTileArchive *archive, const DeceptusU8 *bytes, DeceptusU32 size)
{
   AmigaTileArchive candidate;
   DeceptusU32 cells, index, previous;
   memset(archive, 0, sizeof(*archive));
   if (!bytes || ((DeceptusU32)bytes & 1) || size < 32 || bytes[0] != 'A' || bytes[1] != 'T' || bytes[2] != '0' || bytes[3] != '4')
   {
      return 0;
   }
   memset(&candidate, 0, sizeof(candidate));
   candidate.bytes = bytes;
   candidate.size = size;
   candidate.width = readWord(bytes + 4);
   candidate.height = readWord(bytes + 6);
   candidate.columns = readWord(bytes + 8);
   candidate.rows = readWord(bytes + 10);
   candidate.colors = readWord(bytes + 12);
   candidate.masks = readWord(bytes + 14);
   candidate.color_map = readLong(bytes + 16);
   candidate.mask_map = readLong(bytes + 20);
   candidate.color_table = readLong(bytes + 24);
   candidate.mask_table = readLong(bytes + 28);
   cells = (DeceptusU32)candidate.columns * candidate.rows;
   if (!candidate.width || !candidate.height || !candidate.colors || !candidate.masks ||
       candidate.columns != ((DeceptusU32)candidate.width + 15) / 16 || candidate.rows != ((DeceptusU32)candidate.height + 15) / 16 ||
       candidate.color_map != 32 || cells > (size - 32) / 10 || candidate.mask_map != 32 + cells * 8 ||
       candidate.color_table < 32 + cells * 10 + 8 || (candidate.color_table - 32 - cells * 10) % 8 ||
       candidate.mask_table != candidate.color_table + ((DeceptusU32)bankCount(candidate.colors) + 1) * 4)
   {
      return 0;
   }
   candidate.payload = candidate.mask_table + ((DeceptusU32)bankCount(candidate.masks) + 1) * 4;
   if (candidate.payload > size)
   {
      return 0;
   }
   previous = 0;
   if (!validateBanks(&candidate, candidate.color_table, bankCount(candidate.colors), &previous) ||
       !validateBanks(&candidate, candidate.mask_table, bankCount(candidate.masks), &previous) || previous != size - candidate.payload)
   {
      return 0;
   }
   candidate.quad_table = candidate.mask_map + cells * 2;
   candidate.quads = (DeceptusU16)((candidate.color_table - candidate.quad_table) / 8);
   for (index = 0; index < cells * 4; ++index)
   {
      if (readWord(bytes + candidate.color_map + index * 2) >= candidate.colors)
      {
         return 0;
      }
   }
   for (index = 0; index < cells; ++index)
   {
      if (readWord(bytes + candidate.mask_map + index * 2) >= candidate.quads)
      {
         return 0;
      }
   }
   for (index = 0; index < (DeceptusU32)candidate.quads * 4; ++index)
   {
      if (readWord(bytes + candidate.quad_table + index * 2) >= candidate.masks)
      {
         return 0;
      }
   }
   *archive = candidate;
   return 1;
}

DeceptusU8 amigaTileArchiveCell(const AmigaTileArchive *archive, DeceptusU16 column, DeceptusU16 row, DeceptusU16 *colors,
                                DeceptusU16 *masks)
{
   DeceptusU32 cell;
   const DeceptusU16 *words;
   if (!archive->bytes || column >= archive->columns || row >= archive->rows)
   {
      return 0;
   }
   cell = (DeceptusU32)(DeceptusU16)row * archive->columns + column;
   /* the 68000 is big-endian and the archive word-aligned: its tables are read as native words. */
   words = (const DeceptusU16 *)(archive->bytes + archive->color_map) + cell * 4;
   colors[0] = words[0];
   colors[1] = words[1];
   colors[2] = words[2];
   colors[3] = words[3];
   /* cells name an interned mask quad; quad 0 is empty. */
   words = (const DeceptusU16 *)(archive->bytes + archive->quad_table) +
           (DeceptusU32)((const DeceptusU16 *)(archive->bytes + archive->mask_map))[cell] * 4;
   masks[0] = words[0];
   masks[1] = words[1];
   masks[2] = words[2];
   masks[3] = words[3];
   return 1;
}

/* a bank stored with exactly its decoded size is raw and is read in place. */
const DeceptusU8 *amigaTileArchiveRawBank(const AmigaTileArchive *archive, DeceptusU8 mask_bank, DeceptusU16 bank)
{
   DeceptusU16 count, remaining;
   DeceptusU32 table, first;
   count = mask_bank ? archive->masks : archive->colors;
   if (!archive->bytes || bank >= bankCount(count))
   {
      return 0;
   }
   remaining = count - bank * AMIGA_NATIVE_TILES_PER_BANK;
   if (remaining > AMIGA_NATIVE_TILES_PER_BANK)
   {
      remaining = AMIGA_NATIVE_TILES_PER_BANK;
   }
   table = (mask_bank ? archive->mask_table : archive->color_table) + (DeceptusU32)bank * 4;
   first = readLong(archive->bytes + table);
   if (readLong(archive->bytes + table + 4) - first != (DeceptusU32)remaining * (mask_bank ? AMIGA_NATIVE_MASK_BYTES : AMIGA_NATIVE_TILE_BYTES))
   {
      return 0;
   }
   return archive->bytes + archive->payload + first;
}

DeceptusU8 amigaTileArchiveDecode(const AmigaTileArchive *archive, DeceptusU8 mask_bank, DeceptusU16 bank, DeceptusU8 *destination,
                                  DeceptusU32 capacity)
{
   DeceptusU16 count, remaining;
   DeceptusU32 table, first, last, bytes;
   if (!archive->bytes || !destination || mask_bank > 1)
   {
      return 0;
   }
   count = mask_bank ? archive->masks : archive->colors;
   if (bank >= bankCount(count))
   {
      return 0;
   }
   remaining = count - bank * AMIGA_NATIVE_TILES_PER_BANK;
   if (remaining > AMIGA_NATIVE_TILES_PER_BANK)
   {
      remaining = AMIGA_NATIVE_TILES_PER_BANK;
   }
   bytes = (DeceptusU32)remaining * (mask_bank ? AMIGA_NATIVE_MASK_BYTES : AMIGA_NATIVE_TILE_BYTES);
   if (capacity < bytes)
   {
      return 0;
   }
   table = (mask_bank ? archive->mask_table : archive->color_table) + (DeceptusU32)bank * 4;
   first = readLong(archive->bytes + table);
   last = readLong(archive->bytes + table + 4);
   if (last - first == bytes)
   {
      memcpy(destination, archive->bytes + archive->payload + first, bytes);
      return 1;
   }
   return deceptusLz4Decode(archive->bytes + archive->payload + first, last - first, destination, bytes);
}
