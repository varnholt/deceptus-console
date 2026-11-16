#include "background.h"
#include "snes_assets.h"
#include <snes.h>

#define SNES_BG_SLOTS 1072
#define SNES_BG_HASH 2048
#define SNES_BG_WIDTH 33
#define SNES_BG_HEIGHT 29
#define SNES_BG_CLIPS 8
#define SNES_BG_EMPTY 65535U
#define SNES_BG_DELETED 65534U
#define SNES_BG_DMA_COMMANDS 192

#if SNES_FONT_BYTES > 4608
#error source_font_overlaps_foreground_scratch_tiles
#endif

typedef struct SnesBackgroundDmaCommand
{
   DeceptusU16 source;
   DeceptusU8 bank, increment;
   DeceptusU16 target, bytes;
} SnesBackgroundDmaCommand;

typedef char SnesBackgroundDmaCommandSize[(sizeof(SnesBackgroundDmaCommand) == 8) ? 1 : -1];
extern void snesBackgroundRunDma(const SnesBackgroundDmaCommand *commands, DeceptusU16 count);

typedef struct SnesBackgroundWork
{
   DeceptusU16 resident[SNES_BG_SLOTS], references[SNES_BG_SLOTS], free_slots[SNES_BG_SLOTS];
   DeceptusU16 hash_keys[SNES_BG_HASH], hash_slots[SNES_BG_HASH];
   DeceptusU16 maps[2][2048], uploads[SNES_BG_SLOTS], column[32];
   DeceptusU16 staged_columns[2][32], dma_count;
   SnesBackgroundDmaCommand dma[SNES_BG_DMA_COMMANDS];
   DeceptusU16 clip_frame[SNES_BG_CLIPS], clip_elapsed[SNES_BG_CLIPS], clip_cells[SNES_BG_CLIPS];
   DeceptusU8 pending[SNES_BG_SLOTS], domains[SNES_BG_SLOTS], dirty_column[2];
   DeceptusU16 dirty_rows[2][2], dma_bytes, free_count, upload_count, flags, room_cells, hash_tombstones;
   DeceptusI16 left, top;
   DeceptusU8 loaded;
} SnesBackgroundWork;

typedef char SnesBackgroundStorageFits[(sizeof(SnesBackgroundWork) <= 32768) ? 1 : -1];
extern DeceptusU8 snes_background_storage[];
#define work ((SnesBackgroundWork *)snes_background_storage)

DeceptusU8 snes_background_error, snes_background_full;
DeceptusU16 snes_background_uploads;
DeceptusU16 snes_background_dma_bytes;
DeceptusU16 snes_background_compactions;
DeceptusU16 snes_background_hash_probes, snes_background_hash_rebuilds;
static const SnesStage *stage;
static DeceptusU16 source_flags;
static DeceptusI16 scroll_x, scroll_y;

static DeceptusU16 mapAddress(DeceptusI16 x, DeceptusI16 y)
{
   return (x & 31) + ((y & 31) << 5) + ((x & 32) ? 1024 : 0);
}

static DeceptusU16 slotTile(DeceptusU8 plane, DeceptusU16 slot)
{
   if (!plane)
   {
      return slot;
   }
   return slot < 768 ? slot + 256 : slot - 1024 + 208;
}

static DeceptusU16 tileSlot(DeceptusU8 plane, DeceptusU16 tile)
{
   tile &= 1023;
   if (!plane)
   {
      return tile;
   }
   return tile >= 256 ? tile - 256 : tile - 208 + 1024;
}

static DeceptusU8 slotAddressable(DeceptusU8 plane, DeceptusU16 slot)
{
   if (!plane)
   {
      return slot < 1024;
   }
   if (slot < 768)
   {
      return 1;
   }
   return slot >= 1024;
}

static DeceptusU16 hashFind(DeceptusU16 pattern, DeceptusU8 insert)
{
   DeceptusU16 key, count, deleted;
   key = (pattern ^ (pattern >> 5)) & (SNES_BG_HASH - 1);
   deleted = SNES_BG_EMPTY;
   for (count = 0; count < SNES_BG_HASH; ++count)
   {
      if (work->hash_keys[key] == pattern)
      {
         if (count + 1 > snes_background_hash_probes)
         {
            snes_background_hash_probes = count + 1;
         }
         return key;
      }
      if (work->hash_keys[key] == SNES_BG_EMPTY)
      {
         if (count + 1 > snes_background_hash_probes)
         {
            snes_background_hash_probes = count + 1;
         }
         return insert && deleted != SNES_BG_EMPTY ? deleted : key;
      }
      if (insert && deleted == SNES_BG_EMPTY && work->hash_keys[key] == SNES_BG_DELETED)
      {
         deleted = key;
      }
      key = (key + 1) & (SNES_BG_HASH - 1);
   }
   snes_background_hash_probes = SNES_BG_HASH;
   return deleted;
}

static void rebuildHash(void)
{
   DeceptusU16 index, domain, key, pattern;
   for (index = 0; index < SNES_BG_HASH; ++index)
   {
      work->hash_keys[index] = SNES_BG_EMPTY;
   }
   work->hash_tombstones = 0;
   for (index = 1; index < SNES_BG_SLOTS; ++index)
   {
      if (work->references[index] == 0)
      {
         continue;
      }
      for (domain = 0; domain < 2; ++domain)
      {
         if (work->domains[index] & (1U << domain))
         {
            pattern = work->resident[index] | (domain << 14);
            key = hashFind(pattern, 1);
            if (key == SNES_BG_EMPTY)
            {
               snes_background_error = 2;
               return;
            }
            work->hash_keys[key] = pattern;
            work->hash_slots[key] = index;
         }
      }
   }
   ++snes_background_hash_rebuilds;
}

static void resetCache(void)
{
   DeceptusU16 index, plane;
   for (index = 0; index < SNES_BG_HASH; ++index)
   {
      work->hash_keys[index] = SNES_BG_EMPTY;
   }
   for (index = 0; index < SNES_BG_SLOTS; ++index)
   {
      work->resident[index] = SNES_BG_EMPTY;
      work->references[index] = 0;
      work->pending[index] = 0;
      work->domains[index] = 0;
      if (index)
      {
         work->free_slots[index - 1] = index;
      }
   }
   for (plane = 0; plane < 2; ++plane)
   {
      for (index = 0; index < 2048; ++index)
      {
         work->maps[plane][index] = slotTile((DeceptusU8)plane, 0);
      }
      work->dirty_rows[plane][0] = 65535U;
      work->dirty_rows[plane][1] = 65535U;
      work->dirty_column[plane] = 255;
   }
   work->resident[0] = 0;
   work->references[0] = SNES_BG_EMPTY;
   work->uploads[0] = 0;
   work->pending[0] = 1;
   work->upload_count = 1;
   work->dma_bytes = 8192 + 32;
   work->free_count = SNES_BG_SLOTS - 1;
   work->hash_tombstones = 0;
}

static void releaseCell(DeceptusU8 plane, DeceptusI16 x, DeceptusI16 y)
{
   DeceptusU16 address, slot, key, domain;
   address = mapAddress(x, y);
   slot = tileSlot(plane, work->maps[plane][address]);
   work->maps[plane][address] = slotTile(plane, 0);
   if (slot == 0)
   {
      return;
   }
   if (work->references[slot] == 0)
   {
      return;
   }
   if (!--work->references[slot])
   {
      for (domain = 0; domain < 2; ++domain)
      {
         if (work->domains[slot] & (1U << domain))
         {
            key = hashFind(work->resident[slot] | (domain << 14), 0);
            if (key != SNES_BG_EMPTY)
            {
               work->hash_keys[key] = SNES_BG_DELETED;
               ++work->hash_tombstones;
            }
         }
      }
      work->domains[slot] = 0;
      work->resident[slot] = SNES_BG_EMPTY;
      work->free_slots[work->free_count++] = slot;
   }
}

static DeceptusU16 acquirePattern(DeceptusU8 plane, DeceptusU16 pattern)
{
   DeceptusU16 key, slot, domain_key, other, index;
   if (!pattern)
   {
      return 0;
   }
   if (pattern >= stage->pattern_count)
   {
      snes_background_error = 1;
      return 0;
   }
   domain_key = pattern | ((DeceptusU16)plane << 14);
   key = hashFind(domain_key, 1);
   if (key == SNES_BG_EMPTY)
   {
      snes_background_error = 2;
      return 0;
   }
   if (work->hash_keys[key] == domain_key)
   {
      slot = work->hash_slots[key];
   }
   else
   {
      slot = SNES_BG_EMPTY;
      other = hashFind(domain_key ^ 16384, 0);
      if (other != SNES_BG_EMPTY && work->hash_keys[other] == (domain_key ^ 16384))
      {
         index = work->hash_slots[other];
         if (slotAddressable(plane, index))
         {
            slot = index;
         }
      }
      if (slot == SNES_BG_EMPTY)
      {
         index = work->free_count;
         while (index)
         {
            --index;
            if (slotAddressable(plane, work->free_slots[index]))
            {
               slot = work->free_slots[index];
               work->free_slots[index] = work->free_slots[--work->free_count];
               break;
            }
         }
         if (slot == SNES_BG_EMPTY)
         {
            snes_background_error = 2;
            return 0;
         }
         work->resident[slot] = pattern;
         if (!work->pending[slot])
         {
            work->pending[slot] = 1;
            work->uploads[work->upload_count++] = slot;
            work->dma_bytes += 32;
         }
      }
      if (work->hash_keys[key] == SNES_BG_DELETED)
      {
         --work->hash_tombstones;
      }
      work->hash_keys[key] = domain_key;
      work->hash_slots[key] = slot;
      work->domains[slot] |= (DeceptusU8)(1U << plane);
   }
   ++work->references[slot];
   return slot;
}

static DeceptusU8 clipEnabled(const SnesBackgroundAnimation *clip, DeceptusU16 flags)
{
   if ((flags & clip->required_flags) != clip->required_flags)
   {
      return 0;
   }
   if (flags & clip->forbidden_flags)
   {
      return 0;
   }
   return 1;
}

static void loadCell(DeceptusU8 plane, DeceptusI16 x, DeceptusI16 y, DeceptusU8 dirty)
{
   DeceptusU16 pattern, attributes, index, offset, limit, within, bit, row;
   const SnesRomBlob *blob;
   const SnesBackgroundAnimation *clip;
   const DeceptusU16 *words;
   pattern = 0;
   attributes = 0;
   if (x >= 0 && y >= 0 && x < stage->columns && y < stage->rows)
   {
      blob = plane ? &stage->foreground : &stage->background;
      offset = (DeceptusU16)y * stage->columns + x;
      limit = work->room_cells;
      for (index = 0; index < stage->animation_count; ++index)
      {
         clip = &stage->animations[index];
         if (clip->plane == plane && clipEnabled(clip, source_flags) && x >= clip->left && y >= clip->top &&
             x < clip->left + clip->columns && y < clip->top + clip->rows)
         {
            blob = &clip->cells;
            offset = (work->clip_frame[index] * clip->rows + (y - clip->top)) * clip->columns + (x - clip->left);
            limit = work->clip_cells[index];
         }
      }
      if (offset >= limit)
      {
         snes_background_error = 1;
      }
      else
      {
         words = (const DeceptusU16 *)blob->chunks[offset >> 13];
         within = (offset & 8191) * 2;
         pattern = words[within];
         attributes = words[within + 1];
      }
   }
   index = mapAddress(x, y);
   pattern = acquirePattern(plane, pattern);
   attributes = (attributes & 0xdc00) | (plane ? 0x2000 : 0);
   work->maps[plane][index] = slotTile(plane, pattern) | attributes;
   if (dirty == 1)
   {
      if (work->dirty_column[plane] == 255)
      {
         work->dirty_column[plane] = (DeceptusU8)(x & 63);
         work->dma_bytes += 64;
      }
      else if (work->dirty_column[plane] != (x & 63))
      {
         snes_background_error = 1;
      }
   }
   else if (dirty == 2)
   {
      row = (y & 31) >> 4;
      bit = 1U << (y & 15);
      if (!(work->dirty_rows[plane][row] & bit))
      {
         work->dirty_rows[plane][row] |= bit;
         work->dma_bytes += 128;
      }
   }
}

void snesBackgroundLoad(DeceptusU8 room)
{
   DeceptusU16 index;
   DeceptusU32 cells;
   const SnesBackgroundAnimation *clip;
   snes_background_error = 0;
   snes_background_uploads = 0;
   snes_background_compactions = 0;
   snes_background_hash_probes = 0;
   snes_background_hash_rebuilds = 0;
   snes_background_full = 1;
   if (room >= SNES_STAGE_COUNT)
   {
      snes_background_error = 1;
      room = 0;
   }
   stage = &snes_stages[room];
   if (stage->animation_count > SNES_BG_CLIPS || stage->pattern_count > 16384)
   {
      snes_background_error = 1;
   }
   cells = (DeceptusU32)stage->columns * stage->rows;
   if (cells == 0 || cells > 65535 || stage->background.bytes != cells * 4 || stage->foreground.bytes != cells * 4 ||
       stage->patterns.bytes != (DeceptusU32)stage->pattern_count * 32)
   {
      snes_background_error = 1;
   }
   work->room_cells = (DeceptusU16)cells;
   work->loaded = 0;
   work->flags = 0;
   source_flags = 0;
   for (index = 0; index < SNES_BG_CLIPS; ++index)
   {
      work->clip_frame[index] = 0;
      work->clip_elapsed[index] = 0;
      work->clip_cells[index] = 0;
      if (index < stage->animation_count)
      {
         clip = &stage->animations[index];
         cells = (DeceptusU32)clip->frames * clip->columns * clip->rows;
         if (cells == 0 || cells > 65535 || clip->cells.bytes != cells * 4)
         {
            snes_background_error = 1;
         }
         work->clip_cells[index] = (DeceptusU16)cells;
      }
   }
   bgSetGfxPtr(0, 0x2000);
   bgSetGfxPtr(1, 0x1000);
   bgSetMapPtr(0, 0x0000, SC_64x32);
   bgSetMapPtr(1, 0x0800, SC_64x32);
   dmaCopyCGram((u8 *)stage->palette + 32, 16, 224);
}

void snesBackgroundSetFlags(DeceptusU16 flags)
{
   source_flags = flags;
}

static void loadWindow(DeceptusI16 left, DeceptusI16 top)
{
   DeceptusI16 x, y;
   DeceptusU16 plane;
   resetCache();
   snes_background_full = 1;
   for (plane = 0; plane < 2; ++plane)
   {
      for (y = top; y < top + SNES_BG_HEIGHT; ++y)
      {
         for (x = left; x < left + SNES_BG_WIDTH; ++x)
         {
            loadCell((DeceptusU8)plane, x, y, 0);
         }
      }
   }
   work->loaded = 1;
}

static void stageDma(const void *source, DeceptusU16 target, DeceptusU16 bytes, DeceptusU8 increment)
{
   dmaMemory memory;
   DeceptusU16 index;
   if (work->dma_count >= SNES_BG_DMA_COMMANDS)
   {
      work->dma_count = SNES_BG_EMPTY;
      return;
   }
   memory.mem.p = (void *)source;
   index = work->dma_count++;
   work->dma[index].source = memory.mem.c.addr;
   work->dma[index].bank = memory.mem.c.bank;
   work->dma[index].increment = increment;
   work->dma[index].target = target;
   work->dma[index].bytes = bytes;
}

static void prepareDma(void)
{
   DeceptusU16 index, slot, pattern, target, plane, row, column, base, mask;
   const DeceptusU8 *bytes;
   work->dma_count = 0;
   if (work->dma_bytes == 0)
   {
      return;
   }
   for (index = 0; index < work->upload_count; ++index)
   {
      slot = work->uploads[index];
      pattern = work->resident[slot];
      if (pattern != SNES_BG_EMPTY)
      {
         bytes = stage->patterns.chunks[pattern >> 10] + (pattern & 1023) * 32;
         target = slot < 1024 ? 0x2000 + slot * 16 : 0x1d00 + (slot - 1024) * 16;
         stageDma(bytes, target, 32, 0x80);
      }
   }
   for (plane = 0; plane < 2; ++plane)
   {
      base = plane ? 0x0800 : 0;
      mask = work->dirty_rows[plane][0];
      for (row = 0; row < 32; ++row)
      {
         if (row == 16)
         {
            mask = work->dirty_rows[plane][1];
         }
         if (mask & 1)
         {
            stageDma(&work->maps[plane][row * 32], base + row * 32, 64, 0x80);
            stageDma(&work->maps[plane][1024 + row * 32], base + 1024 + row * 32, 64, 0x80);
         }
         mask >>= 1;
      }
      column = work->dirty_column[plane];
      if (column < 64)
      {
         for (row = 0; row < 32; ++row)
         {
            index = (column & 31) + row * 32 + (column >= 32 ? 1024 : 0);
            slot = work->maps[plane][index];
            work->staged_columns[plane][row] = slot;
         }
         stageDma(work->staged_columns[plane], base + (column & 31) + (column >= 32 ? 1024 : 0), 64, 0x81);
      }
   }
   if (work->dma_count == SNES_BG_EMPTY)
   {
      snes_background_full = 1;
   }
   else if ((DeceptusU32)work->dma_count * 768 + (DeceptusU32)snes_background_dma_bytes * 8 > 36000)
   {
      /* leave time for the sdk nmi and oam transfer in the ntsc vblank. */
      snes_background_full = 1;
   }
   if (work->dma_count != SNES_BG_EMPTY)
   {
      for (index = 0; index < work->upload_count; ++index)
      {
         slot = work->uploads[index];
         work->pending[slot] = 0;
      }
      work->upload_count = 0;
      for (plane = 0; plane < 2; ++plane)
      {
         work->dirty_rows[plane][0] = 0;
         work->dirty_rows[plane][1] = 0;
         work->dirty_column[plane] = 255;
      }
   }
}

void snesBackgroundPrepare(DeceptusI16 camera_x, DeceptusI16 camera_y)
{
   DeceptusI16 left, top, x, y, added_column, refresh_left, refresh_top, refresh_right, refresh_bottom;
   DeceptusU16 index, plane, duration;
   DeceptusU8 dirty_clips, active, full_refresh;
   const SnesBackgroundAnimation *clip;
   if (snes_background_error)
   {
      return;
   }
   scroll_x = camera_x;
   scroll_y = camera_y;
   work->dma_bytes = 0;
   snes_background_hash_probes = 0;
   if (work->loaded)
   {
      if (work->hash_tombstones > 128)
      {
         rebuildHash();
      }
   }
   left = camera_x / 8;
   top = camera_y / 8;
   dirty_clips = 0;
   for (index = 0; index < stage->animation_count; ++index)
   {
      clip = &stage->animations[index];
      active = clipEnabled(clip, source_flags);
      if (active != clipEnabled(clip, work->flags))
      {
         dirty_clips |= (DeceptusU8)(1U << index);
         work->clip_frame[index] = 0;
         work->clip_elapsed[index] = 0;
      }
      else if (active && clip->frames > 1)
      {
         duration = clip->durations ? clip->durations[work->clip_frame[index]] : clip->frame_ticks;
         if (!duration)
         {
            snes_background_error = 1;
            return;
         }
         if (++work->clip_elapsed[index] >= duration)
         {
            work->clip_elapsed[index] = 0;
            if (++work->clip_frame[index] >= clip->frames)
            {
               work->clip_frame[index] = 0;
            }
            dirty_clips |= (DeceptusU8)(1U << index);
         }
      }
   }
   full_refresh = work->loaded == 0;
   if (left < work->left - 1 || left > work->left + 1 || top < work->top - 1 || top > work->top + 1)
   {
      full_refresh = 1;
   }
   if (full_refresh)
   {
      loadWindow(left, top);
   }
   else
   {
      for (plane = 0; plane < 2; ++plane)
      {
         if (left != work->left)
         {
            x = left > work->left ? work->left : work->left + SNES_BG_WIDTH - 1;
            for (y = work->top; y < work->top + SNES_BG_HEIGHT; ++y)
            {
               releaseCell((DeceptusU8)plane, x, y);
            }
         }
         if (top != work->top)
         {
            y = top > work->top ? work->top : work->top + SNES_BG_HEIGHT - 1;
            for (x = work->left; x < work->left + SNES_BG_WIDTH; ++x)
            {
               releaseCell((DeceptusU8)plane, x, y);
            }
         }
         added_column = -1;
         if (left != work->left)
         {
            added_column = left > work->left ? left + SNES_BG_WIDTH - 1 : left;
            for (y = top; y < top + SNES_BG_HEIGHT; ++y)
            {
               loadCell((DeceptusU8)plane, added_column, y, 1);
            }
         }
         if (top != work->top)
         {
            y = top > work->top ? top + SNES_BG_HEIGHT - 1 : top;
            for (x = left; x < left + SNES_BG_WIDTH; ++x)
            {
               if (x != added_column)
               {
                  loadCell((DeceptusU8)plane, x, y, 2);
               }
            }
         }
      }
      for (index = 0; index < stage->animation_count; ++index)
      {
         if (!(dirty_clips & (1U << index)))
         {
            continue;
         }
         clip = &stage->animations[index];
         refresh_left = left;
         if (refresh_left < (DeceptusI16)clip->left)
         {
            refresh_left = (DeceptusI16)clip->left;
         }
         refresh_top = top;
         if (refresh_top < (DeceptusI16)clip->top)
         {
            refresh_top = (DeceptusI16)clip->top;
         }
         refresh_right = left + SNES_BG_WIDTH;
         if (refresh_right > (DeceptusI16)(clip->left + clip->columns))
         {
            refresh_right = (DeceptusI16)(clip->left + clip->columns);
         }
         refresh_bottom = top + SNES_BG_HEIGHT;
         if (refresh_bottom > (DeceptusI16)(clip->top + clip->rows))
         {
            refresh_bottom = (DeceptusI16)(clip->top + clip->rows);
         }
         /* retain row order and release every old reference before reloading. */
         for (y = refresh_top; y < refresh_bottom; ++y)
         {
            for (x = refresh_left; x < refresh_right; ++x)
            {
               releaseCell(clip->plane, x, y);
            }
         }
         for (y = refresh_top; y < refresh_bottom; ++y)
         {
            for (x = refresh_left; x < refresh_right; ++x)
            {
               loadCell(clip->plane, x, y, 2);
            }
         }
      }
   }
   if (snes_background_error == 2)
   {
      if (snes_background_full == 0)
      {
         /* plane address domains can fragment although the visible set fits.
          * rebuild once before publishing; a genuine budget failure stays an error. */
         snes_background_error = 0;
         ++snes_background_compactions;
         loadWindow(left, top);
      }
   }
   work->left = left;
   work->top = top;
   work->flags = source_flags;
   snes_background_uploads = work->upload_count;
   snes_background_dma_bytes = work->dma_bytes;
   prepareDma();
}

static void copyColumn(const DeceptusU16 *source, DeceptusU16 address)
{
   dmaMemory memory;
   memory.mem.p = (void *)source;
   REG_VMAIN = 0x81;
   REG_VMADDLH = address;
   REG_DMAP0 = 1;
   REG_BBAD0 = 0x18;
   REG_A1T0LH = memory.mem.c.addr;
   REG_A1B0 = memory.mem.c.bank;
   REG_DAS0LH = 64;
   REG_MDMAEN = 1;
   REG_VMAIN = 0x80;
}

void snesBackgroundCommit(void)
{
   DeceptusU16 index, slot, pattern, plane, row, column, base, mask;
   const DeceptusU8 *bytes;
   if (snes_background_error)
   {
      return;
   }
   if (work->dma_count != SNES_BG_EMPTY)
   {
      snesBackgroundRunDma(work->dma, work->dma_count);
      bgSetScroll(0, scroll_x, scroll_y);
      bgSetScroll(1, scroll_x, scroll_y);
      snes_background_full = 0;
      return;
   }
   for (index = 0; index < work->upload_count; ++index)
   {
      slot = work->uploads[index];
      pattern = work->resident[slot];
      if (pattern != SNES_BG_EMPTY)
      {
         bytes = stage->patterns.chunks[pattern >> 10] + (pattern & 1023) * 32;
         row = slot < 1024 ? 0x2000 + slot * 16 : 0x1d00 + (slot - 1024) * 16;
         dmaCopyVram((u8 *)bytes, row, 32);
      }
      work->pending[slot] = 0;
   }
   work->upload_count = 0;
   for (plane = 0; plane < 2; ++plane)
   {
      base = plane ? 0x0800 : 0;
      mask = work->dirty_rows[plane][0];
      for (row = 0; row < 32; ++row)
      {
         if (row == 16)
         {
            mask = work->dirty_rows[plane][1];
         }
         if (mask & 1)
         {
            dmaCopyVram((u8 *)&work->maps[plane][row * 32], base + row * 32, 64);
            dmaCopyVram((u8 *)&work->maps[plane][1024 + row * 32], base + 1024 + row * 32, 64);
         }
         mask >>= 1;
      }
      work->dirty_rows[plane][0] = 0;
      work->dirty_rows[plane][1] = 0;
      column = work->dirty_column[plane];
      if (column < 64)
      {
         for (row = 0; row < 32; ++row)
         {
            index = (column & 31) + row * 32 + (column >= 32 ? 1024 : 0);
            slot = work->maps[plane][index];
            work->column[row] = slot;
         }
         copyColumn(work->column, base + (column & 31) + (column >= 32 ? 1024 : 0));
         work->dirty_column[plane] = 255;
      }
   }
   bgSetScroll(0, scroll_x, scroll_y);
   bgSetScroll(1, scroll_x, scroll_y);
   snes_background_full = 0;
}
