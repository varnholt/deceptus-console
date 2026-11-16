#include "opening_video.h"
#include <genesis.h>

#define WORLD_HASH 2048
#define WORLD_WIDTH 41
#define WORLD_HEIGHT 29
#define WORLD_CLIPS 8
#define EMPTY 65535U
#define DELETED 65534U
#define MAX_UPLOADS 192

typedef struct SmdBackgroundWork
{
   DeceptusU16 resident[SMD_WORLD_SLOTS], references[SMD_WORLD_SLOTS], free_slots[SMD_WORLD_SLOTS];
   DeceptusU16 hash_keys[WORLD_HASH], hash_slots[WORLD_HASH];
   DeceptusU16 maps[2][2048], uploads[MAX_UPLOADS], column[2][32];
   DeceptusU16 frame[WORLD_CLIPS], elapsed[WORLD_CLIPS], dirty_rows[2][2];
   DeceptusU16 free_count, upload_count, flags, tombstones;
   DeceptusI16 left, top;
   DeceptusU8 pending[SMD_WORLD_SLOTS], loaded, reload, dirty_column[2];
   DeceptusU8 row_left[2][32], row_right[2][32];
} SmdBackgroundWork;

static SmdBackgroundWork work;
static const SmdStage *stage;
static DeceptusU16 source_flags;
static DeceptusI16 scroll_x, scroll_y;
static DeceptusU32 tile_buffer[8];
DeceptusU8 smd_background_error, smd_background_full;
DeceptusU16 smd_background_dma_bytes, smd_background_uploads;

static DeceptusU16 mapAddress(DeceptusI16 x, DeceptusI16 y)
{
   return (y & 31) * 64 + (x & 63);
}

static DeceptusU16 hashFind(DeceptusU16 pattern, DeceptusU8 insert)
{
   DeceptusU16 key, count, deleted;
   key = (pattern ^ (pattern >> 5)) & (WORLD_HASH - 1);
   deleted = EMPTY;
   for (count = 0; count < WORLD_HASH; ++count)
   {
      if (work.hash_keys[key] == pattern)
      {
         return key;
      }
      if (work.hash_keys[key] == EMPTY)
      {
         return insert && deleted != EMPTY ? deleted : key;
      }
      if (insert && deleted == EMPTY && work.hash_keys[key] == DELETED)
      {
         deleted = key;
      }
      key = (key + 1) & (WORLD_HASH - 1);
   }
   return deleted;
}

static void resetCache(void)
{
   DeceptusU16 index;
   memset(&work, 0, sizeof(work));
   for (index = 0; index < WORLD_HASH; ++index)
   {
      work.hash_keys[index] = EMPTY;
   }
   for (index = 0; index < SMD_WORLD_SLOTS; ++index)
   {
      work.resident[index] = EMPTY;
      if (index)
      {
         work.free_slots[index - 1] = index;
      }
   }
   work.resident[0] = 0;
   work.references[0] = EMPTY;
   work.free_count = SMD_WORLD_SLOTS - 1;
   work.pending[0] = 1;
   work.uploads[0] = 0;
   work.upload_count = 1;
   work.dirty_rows[0][0] = work.dirty_rows[0][1] = EMPTY;
   work.dirty_rows[1][0] = work.dirty_rows[1][1] = EMPTY;
   work.dirty_column[0] = work.dirty_column[1] = 255;
   memset(work.row_right, 63, sizeof(work.row_right));
   work.reload = 1;
   smd_background_full = 1;
}

static void rebuildHash(void)
{
   DeceptusU16 index, key;
   for (index = 0; index < WORLD_HASH; ++index)
   {
      work.hash_keys[index] = EMPTY;
   }
   for (index = 1; index < SMD_WORLD_SLOTS; ++index)
   {
      if (work.references[index])
      {
         key = hashFind(work.resident[index], 1);
         if (key == EMPTY)
         {
            smd_background_error = 2;
            return;
         }
         work.hash_keys[key] = work.resident[index];
         work.hash_slots[key] = index;
      }
   }
   work.tombstones = 0;
}

static void releaseCell(DeceptusU8 plane, DeceptusI16 x, DeceptusI16 y)
{
   DeceptusU16 address, slot, key;
   address = mapAddress(x, y);
   slot = work.maps[plane][address] & 0x7ff;
   work.maps[plane][address] = 0;
   if (!slot || !work.references[slot])
   {
      return;
   }
   if (!--work.references[slot])
   {
      key = hashFind(work.resident[slot], 0);
      if (key == EMPTY)
      {
         smd_background_error = 2;
         return;
      }
      work.hash_keys[key] = DELETED;
      ++work.tombstones;
      work.resident[slot] = EMPTY;
      work.free_slots[work.free_count++] = slot;
   }
}

static DeceptusU16 acquire(DeceptusU16 pattern)
{
   DeceptusU16 key, slot;
   if (!pattern)
   {
      return 0;
   }
   if (pattern >= stage->pattern_count)
   {
      smd_background_error = 1;
      return 0;
   }
   key = hashFind(pattern, 1);
   if (key == EMPTY)
   {
      smd_background_error = 2;
      return 0;
   }
   if (work.hash_keys[key] == pattern)
   {
      slot = work.hash_slots[key];
   }
   else
   {
      if (!work.free_count)
      {
         smd_background_error = 2;
         return 0;
      }
      slot = work.free_slots[--work.free_count];
      work.resident[slot] = pattern;
      if (!work.pending[slot])
      {
         work.pending[slot] = 1;
         if (work.upload_count < MAX_UPLOADS)
         {
            work.uploads[work.upload_count++] = slot;
         }
         else
         {
            work.reload = 1;
            smd_background_full = 1;
         }
         smd_background_dma_bytes += 32;
      }
      if (work.hash_keys[key] == DELETED)
      {
         --work.tombstones;
      }
      work.hash_keys[key] = pattern;
      work.hash_slots[key] = slot;
   }
   ++work.references[slot];
   return slot;
}

static DeceptusU8 clipEnabled(const SmdBackgroundAnimation *clip, DeceptusU16 flags)
{
   return (flags & clip->required_flags) == clip->required_flags && !(flags & clip->forbidden_flags);
}

static void loadCell(DeceptusU8 plane, DeceptusI16 x, DeceptusI16 y, DeceptusU8 dirty)
{
   const SmdRomBlob *blob;
   const SmdBackgroundAnimation *clip;
   const DeceptusU8 *cell;
   DeceptusU16 row, column, index, pattern, attributes;
   pattern = attributes = 0;
   if (x >= 0 && y >= 0 && x < stage->columns && y < stage->rows)
   {
      blob = plane ? &stage->foreground : &stage->background;
      row = y;
      column = x;
      for (index = 0; index < stage->animation_count; ++index)
      {
         clip = &stage->animations[index];
         if (clip->plane == plane && clipEnabled(clip, source_flags) && x >= clip->left && y >= clip->top &&
             x < clip->left + clip->columns && y < clip->top + clip->rows)
         {
            blob = &clip->cells;
            row = work.frame[index] * clip->rows + y - clip->top;
            column = x - clip->left;
         }
      }
      cell = smdCell(blob, row, column);
      if (!cell)
      {
         smd_background_error = 1;
         return;
      }
      pattern = ((DeceptusU16)cell[0] << 8) | cell[1];
      attributes = ((DeceptusU16)cell[2] << 8) | cell[3];
   }
   work.maps[plane][mapAddress(x, y)] = acquire(pattern) | attributes;
   if (dirty == 1)
   {
      work.dirty_column[plane] = (DeceptusU8)(x & 63);
   }
   else
   {
      index = y & 31;
      column = x & 63;
      if (!(work.dirty_rows[plane][index >> 4] & (1U << (index & 15))))
      {
         work.row_left[plane][index] = work.row_right[plane][index] = (DeceptusU8)column;
      }
      else
      {
         if (column < work.row_left[plane][index])
         {
            work.row_left[plane][index] = (DeceptusU8)column;
         }
         if (column > work.row_right[plane][index])
         {
            work.row_right[plane][index] = (DeceptusU8)column;
         }
      }
      work.dirty_rows[plane][index >> 4] |= 1U << (index & 15);
   }
}

static void loadWindow(DeceptusI16 left, DeceptusI16 top)
{
   DeceptusU16 plane;
   DeceptusI16 x, y;
   DeceptusU16 frames[WORLD_CLIPS], elapsed[WORLD_CLIPS];
   memcpy(frames, work.frame, sizeof(frames));
   memcpy(elapsed, work.elapsed, sizeof(elapsed));
   resetCache();
   memcpy(work.frame, frames, sizeof(frames));
   memcpy(work.elapsed, elapsed, sizeof(elapsed));
   for (plane = 0; plane < 2; ++plane)
   {
      for (y = top; y < top + WORLD_HEIGHT; ++y)
      {
         for (x = left; x < left + WORLD_WIDTH; ++x)
         {
            loadCell((DeceptusU8)plane, x, y, 2);
         }
      }
   }
   work.loaded = 1;
}

void smdBackgroundLoad(DeceptusU8 room)
{
   smd_background_error = smd_graphics_error = 0;
   if (room >= SMD_STAGE_COUNT)
   {
      smd_background_error = 1;
      return;
   }
   stage = &smd_stages[room];
   if (stage->animation_count > WORLD_CLIPS)
   {
      smd_background_error = 1;
      return;
   }
   memset(&work, 0, sizeof(work));
   work.flags = source_flags;
   PAL_setColors(0, stage->palette, 32, CPU);
}

void smdBackgroundSetFlags(DeceptusU16 flags)
{
   source_flags = flags;
}

void smdBackgroundPrepare(DeceptusI16 camera_x, DeceptusI16 camera_y)
{
   const SmdBackgroundAnimation *clip;
   DeceptusU16 index, duration, plane;
   DeceptusU8 dirty, active;
   DeceptusI16 left, top, x, y, added, clip_left, clip_top, clip_right, clip_bottom;
   if (smd_background_error)
   {
      return;
   }
   scroll_x = camera_x;
   scroll_y = camera_y;
   left = camera_x / 8;
   top = camera_y / 8;
   smd_background_dma_bytes = 0;
   if (work.tombstones > 128)
   {
      rebuildHash();
   }
   dirty = 0;
   for (index = 0; index < stage->animation_count; ++index)
   {
      clip = &stage->animations[index];
      active = clipEnabled(clip, source_flags);
      if (active != clipEnabled(clip, work.flags))
      {
         dirty |= 1U << index;
         work.frame[index] = work.elapsed[index] = 0;
      }
      else if (active && clip->frames > 1)
      {
         duration = clip->durations ? clip->durations[work.frame[index]] : clip->frame_ticks;
         if (!duration)
         {
            smd_background_error = 1;
            return;
         }
         if (++work.elapsed[index] >= duration)
         {
            work.elapsed[index] = 0;
            if (++work.frame[index] >= clip->frames)
            {
               work.frame[index] = 0;
            }
            dirty |= 1U << index;
         }
      }
   }
   if (!work.loaded || left < work.left - 1 || left > work.left + 1 || top < work.top - 1 || top > work.top + 1)
   {
      loadWindow(left, top);
   }
   else
   {
      for (plane = 0; plane < 2; ++plane)
      {
         if (left != work.left)
         {
            x = left > work.left ? work.left : work.left + WORLD_WIDTH - 1;
            for (y = work.top; y < work.top + WORLD_HEIGHT; ++y)
            {
               releaseCell((DeceptusU8)plane, x, y);
            }
         }
         if (top != work.top)
         {
            y = top > work.top ? work.top : work.top + WORLD_HEIGHT - 1;
            for (x = work.left; x < work.left + WORLD_WIDTH; ++x)
            {
               if (left == work.left || x != (left > work.left ? work.left : work.left + WORLD_WIDTH - 1))
               {
                  releaseCell((DeceptusU8)plane, x, y);
               }
            }
         }
      }
      for (plane = 0; plane < 2; ++plane)
      {
         added = -1;
         if (left != work.left)
         {
            added = left > work.left ? left + WORLD_WIDTH - 1 : left;
            for (y = top; y < top + WORLD_HEIGHT; ++y)
            {
               loadCell((DeceptusU8)plane, added, y, 1);
            }
         }
         if (top != work.top)
         {
            y = top > work.top ? top + WORLD_HEIGHT - 1 : top;
            for (x = left; x < left + WORLD_WIDTH; ++x)
            {
               if (x != added)
               {
                  loadCell((DeceptusU8)plane, x, y, 2);
               }
            }
         }
      }
      for (index = 0; index < stage->animation_count; ++index)
      {
         if (!(dirty & (1U << index)))
         {
            continue;
         }
         clip = &stage->animations[index];
         clip_left = left > clip->left ? left : clip->left;
         clip_top = top > clip->top ? top : clip->top;
         clip_right = left + WORLD_WIDTH < clip->left + clip->columns ? left + WORLD_WIDTH : clip->left + clip->columns;
         clip_bottom = top + WORLD_HEIGHT < clip->top + clip->rows ? top + WORLD_HEIGHT : clip->top + clip->rows;
         for (y = clip_top; y < clip_bottom; ++y)
         {
            for (x = clip_left; x < clip_right; ++x)
            {
               releaseCell(clip->plane, x, y);
            }
         }
         for (y = clip_top; y < clip_bottom; ++y)
         {
            for (x = clip_left; x < clip_right; ++x)
            {
               loadCell(clip->plane, x, y, 2);
            }
         }
      }
   }
   if (smd_background_error == 2 && !smd_background_full)
   {
      smd_background_error = 0;
      loadWindow(left, top);
   }
   work.left = left;
   work.top = top;
   work.flags = source_flags;
   smd_background_uploads = work.upload_count;
   for (plane = 0; plane < 2; ++plane)
   {
      if (work.dirty_column[plane] < 64)
      {
         smd_background_dma_bytes += 64;
         for (index = 0; index < 32; ++index)
         {
            work.column[plane][index] = work.maps[plane][index * 64 + work.dirty_column[plane]];
         }
      }
      for (index = 0; index < 32; ++index)
      {
         if (work.dirty_rows[plane][index >> 4] & (1U << (index & 15)))
         {
            smd_background_dma_bytes += (work.row_right[plane][index] - work.row_left[plane][index] + 1) * 2;
         }
      }
   }
   if (smd_background_dma_bytes > 5500)
   {
      smd_background_full = 1;
   }
}

void smdBackgroundCommit(void)
{
   DeceptusU16 index, slot, plane, row, base;
   if (smd_background_error)
   {
      return;
   }
   for (index = 0; index < (work.reload ? SMD_WORLD_SLOTS : work.upload_count); ++index)
   {
      slot = work.reload ? index : work.uploads[index];
      if (work.pending[slot] && work.resident[slot] != EMPTY)
      {
         if (!smdDecodeBlock(&stage->patterns, work.resident[slot], (DeceptusU8 *)tile_buffer))
         {
            return;
         }
         VDP_loadTileData(tile_buffer, slot, 1, DMA);
      }
      work.pending[slot] = 0;
   }
   work.reload = 0;
   work.upload_count = 0;
   for (plane = 0; plane < 2; ++plane)
   {
      base = plane ? 0xc000 : 0xe000;
      for (row = 0; row < 32; ++row)
      {
         if (work.dirty_rows[plane][row >> 4] & (1U << (row & 15)))
         {
            DMA_doDma(DMA_VRAM, &work.maps[plane][row * 64 + work.row_left[plane][row]], base + row * 128 + work.row_left[plane][row] * 2,
                      work.row_right[plane][row] - work.row_left[plane][row] + 1, 2);
         }
      }
      if (work.dirty_column[plane] < 64)
      {
         DMA_doDma(DMA_VRAM, work.column[plane], base + work.dirty_column[plane] * 2, 32, 128);
      }
      work.dirty_column[plane] = 255;
      work.dirty_rows[plane][0] = work.dirty_rows[plane][1] = 0;
   }
   VDP_setAutoInc(2);
   VDP_setHorizontalScroll(BG_A, -scroll_x);
   VDP_setHorizontalScroll(BG_B, -scroll_x);
   VDP_setVerticalScroll(BG_A, scroll_y);
   VDP_setVerticalScroll(BG_B, scroll_y);
   smd_background_full = 0;
}
