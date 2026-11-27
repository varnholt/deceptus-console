#include "opening_video.h"
#include <genesis.h>

#define WORLD_HASH 2048
#define WORLD_WIDTH 41
#define WORLD_HEIGHT 29
#define WORLD_CLIPS 8
#define EMPTY 65535U
#define DELETED 65534U
#define MAX_UPLOADS 192
/* per-slot state bits: an upload is owed, or the slot sits in the free ring. */
#define SLOT_PENDING 1
#define SLOT_QUEUED 2

typedef struct SmdBackgroundWork
{
   DeceptusU16 resident[SMD_WORLD_SLOTS], references[SMD_WORLD_SLOTS], free_slots[SMD_WORLD_SLOTS];
   DeceptusU16 hash_keys[WORLD_HASH], hash_slots[WORLD_HASH];
   DeceptusU16 maps[2][2048], uploads[MAX_UPLOADS], column[2][32];
   DeceptusU16 frame[WORLD_CLIPS], elapsed[WORLD_CLIPS], dirty_rows[2][2];
   DeceptusU16 free_head, free_count, upload_count, flags, tombstones;
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

/* fibonacci hashing: a room's patterns are numbered in runs, and a home close to the pattern
   number turned those runs into long probe clusters once released patterns stay resident. */
static DeceptusU16 hashHome(DeceptusU16 pattern)
{
   return (DeceptusU16)((DeceptusU16)(pattern * 40503U) >> 5) & (WORLD_HASH - 1);
}

/* backward-shift deletion keeps the linear probe table free of tombstones. scrolling releases
   patterns constantly, and the tombstone rebuild it replaces rehashed every resident slot. */
static void hashDelete(DeceptusU16 hole)
{
   DeceptusU16 next, home;
   next = hole;
   while (1)
   {
      next = (next + 1) & (WORLD_HASH - 1);
      if (work.hash_keys[next] == EMPTY)
      {
         break;
      }
      home = hashHome(work.hash_keys[next]);
      /* an entry may fill the hole unless its home lies cyclically in (hole, next]. */
      if (hole <= next ? (home <= hole || home > next) : (home <= hole && home > next))
      {
         work.hash_keys[hole] = work.hash_keys[next];
         work.hash_slots[hole] = work.hash_slots[next];
         hole = next;
      }
   }
   work.hash_keys[hole] = EMPTY;
}

static DeceptusU16 hashFind(DeceptusU16 pattern, DeceptusU8 insert)
{
   DeceptusU16 key, count, deleted;
   key = hashHome(pattern);
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
         work.pending[index] = SLOT_QUEUED;
      }
   }
   work.resident[0] = 0;
   work.references[0] = EMPTY;
   work.free_head = 0;
   work.free_count = SMD_WORLD_SLOTS - 1;
   work.pending[0] = SLOT_PENDING;
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
      if (work.resident[index] != EMPTY)
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
   DeceptusU16 address, slot, tail;
   address = mapAddress(x, y);
   slot = work.maps[plane][address] & 0x7ff;
   work.maps[plane][address] = 0;
   if (!slot || !work.references[slot])
   {
      return;
   }
   /* a released pattern stays resident and hashed at the back of the free ring, so an animated
      clip that cycles back to it, or a scroll that returns, finds it in VRAM instead of decoding
      and uploading it again. it is only evicted when its slot is reused. */
   if (!--work.references[slot] && !(work.pending[slot] & SLOT_QUEUED))
   {
      work.pending[slot] |= SLOT_QUEUED;
      tail = work.free_head + work.free_count;
      if (tail >= SMD_WORLD_SLOTS)
      {
         tail -= SMD_WORLD_SLOTS;
      }
      work.free_slots[tail] = slot;
      ++work.free_count;
   }
}

/* the oldest released slot whose pattern nobody acquired again, with that pattern evicted from the
   hash. slots revived while queued are dropped from the ring here and requeued on release. */
static DeceptusU16 takeFreeSlot(void)
{
   DeceptusU16 slot, key;
   while (work.free_count)
   {
      slot = work.free_slots[work.free_head];
      if (++work.free_head >= SMD_WORLD_SLOTS)
      {
         work.free_head = 0;
      }
      --work.free_count;
      work.pending[slot] &= (DeceptusU8)~SLOT_QUEUED;
      if (work.references[slot])
      {
         continue;
      }
      if (work.resident[slot] != EMPTY)
      {
         key = hashFind(work.resident[slot], 0);
         if (key == EMPTY || work.hash_keys[key] != work.resident[slot])
         {
            smd_background_error = 2;
            return EMPTY;
         }
         hashDelete(key);
         work.resident[slot] = EMPTY;
      }
      return slot;
   }
   smd_background_error = 2;
   return EMPTY;
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
      slot = takeFreeSlot();
      if (slot == EMPTY)
      {
         return 0;
      }
      /* evicting the slot's previous pattern shifted the probe sequence. */
      key = hashFind(pattern, 1);
      if (key == EMPTY)
      {
         smd_background_error = 2;
         return 0;
      }
      work.resident[slot] = pattern;
      if (!(work.pending[slot] & SLOT_PENDING))
      {
         work.pending[slot] |= SLOT_PENDING;
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

static DeceptusU8 cellValue(DeceptusU8 plane, DeceptusI16 x, DeceptusI16 y, DeceptusU16 *pattern_out, DeceptusU16 *attributes_out)
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
         return 0;
      }
      pattern = ((DeceptusU16)cell[0] << 8) | cell[1];
      attributes = ((DeceptusU16)cell[2] << 8) | cell[3];
   }
   *pattern_out = pattern;
   *attributes_out = attributes;
   return 1;
}

static void storeCell(DeceptusU8 plane, DeceptusI16 x, DeceptusI16 y, DeceptusU8 dirty, DeceptusU16 pattern, DeceptusU16 attributes)
{
   DeceptusU16 column, index;
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

static void loadCell(DeceptusU8 plane, DeceptusI16 x, DeceptusI16 y, DeceptusU8 dirty)
{
   DeceptusU16 pattern, attributes;
   if (cellValue(plane, x, y, &pattern, &attributes))
   {
      storeCell(plane, x, y, dirty, pattern, attributes);
   }
}

/* an animated clip's next frame usually repeats most of its cells. a cell whose pattern and
   attributes already sit in the map keeps its slot, so it is neither re-hashed nor re-sent. */
static void refreshCellWith(DeceptusU8 plane, DeceptusI16 x, DeceptusI16 y, DeceptusU16 pattern, DeceptusU16 attributes)
{
   DeceptusU16 current;
   current = work.maps[plane][mapAddress(x, y)];
   if (!(attributes & 0x7ff) && (current & ~0x7ff) == attributes && work.resident[current & 0x7ff] == pattern)
   {
      return;
   }
   releaseCell(plane, x, y);
   storeCell(plane, x, y, 2, pattern, attributes);
}

static void refreshCell(DeceptusU8 plane, DeceptusI16 x, DeceptusI16 y)
{
   DeceptusU16 pattern, attributes;
   if (cellValue(plane, x, y, &pattern, &attributes))
   {
      refreshCellWith(plane, x, y, pattern, attributes);
   }
}

/* whether a later enabled clip on the same plane overlaps this one. cellValue lets the later clip
   win, so only an uncovered clip may take its cells straight from its own frame data. */
static DeceptusU8 clipCovered(DeceptusU16 index)
{
   const SmdBackgroundAnimation *clip, *other;
   DeceptusU16 later;
   clip = &stage->animations[index];
   for (later = index + 1; later < stage->animation_count; ++later)
   {
      other = &stage->animations[later];
      if (other->plane == clip->plane && clipEnabled(other, source_flags) && other->left < clip->left + clip->columns &&
          clip->left < other->left + other->columns && other->top < clip->top + clip->rows && clip->top < other->top + other->rows)
      {
         return 1;
      }
   }
   return 0;
}

/* sequential reader over one run-length row of map cells. */
typedef struct SmdCellRun
{
   const DeceptusU8 *source, *end, *cell;
   DeceptusU16 left;
   DeceptusU8 literal;
} SmdCellRun;

static DeceptusU8 cellRunStart(SmdCellRun *run, const SmdRomBlob *blob, DeceptusU16 row)
{
   if (row >= blob->blocks || blob->unit_bytes != 4)
   {
      return 0;
   }
   run->source = blob->data + blob->offsets[row];
   run->end = blob->data + blob->offsets[row + 1];
   run->left = 0;
   return 1;
}

static const DeceptusU8 *cellRunNext(SmdCellRun *run)
{
   const DeceptusU8 *cell;
   DeceptusU8 code;
   if (!run->left)
   {
      if (run->source >= run->end)
      {
         return 0;
      }
      code = *run->source++;
      run->literal = code < 128;
      run->left = run->literal ? code + 1 : code - 127;
      run->cell = run->source;
      run->source += run->literal ? run->left * 4 : 4;
      if (run->source > run->end)
      {
         run->left = 0;
         run->source = run->end;
         return 0;
      }
   }
   cell = run->cell;
   if (run->literal)
   {
      run->cell += 4;
   }
   --run->left;
   return cell;
}

/* a clip that stepped to its next frame only changes the cells whose data differs from the frame
   before; both frames' rows are read side by side instead of resolving every cell of the clip. */
static void refreshClipRow(const SmdBackgroundAnimation *clip, DeceptusU16 frame, DeceptusU16 previous, DeceptusI16 y,
                           DeceptusI16 clip_left, DeceptusI16 clip_right, DeceptusU8 covered)
{
   SmdCellRun now, before;
   const DeceptusU8 *a, *b;
   DeceptusI16 x;
   DeceptusU8 compare;
   compare = cellRunStart(&now, &clip->cells, frame * clip->rows + y - clip->top) &&
             cellRunStart(&before, &clip->cells, previous * clip->rows + y - clip->top);
   for (x = clip->left; compare && x < clip_left; ++x)
   {
      compare = cellRunNext(&now) && cellRunNext(&before);
   }
   for (x = clip_left; x < clip_right; ++x)
   {
      if (compare)
      {
         a = cellRunNext(&now);
         b = cellRunNext(&before);
         if (a && b && a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3])
         {
            continue;
         }
         compare = a && b;
         /* the reader already holds the new cell; resolving it again walked the row from its start. */
         if (a && !covered)
         {
            refreshCellWith(clip->plane, x, y, ((DeceptusU16)a[0] << 8) | a[1], ((DeceptusU16)a[2] << 8) | a[3]);
            continue;
         }
      }
      refreshCell(clip->plane, x, y);
   }
}

/* whether an enabled clip of the plane covers part of the window row, where cellValue reads the clip. */
static DeceptusU8 rowHasClip(DeceptusU8 plane, DeceptusI16 y, DeceptusI16 left)
{
   const SmdBackgroundAnimation *clip;
   DeceptusU16 index;
   for (index = 0; index < stage->animation_count; ++index)
   {
      clip = &stage->animations[index];
      if ((clip->plane == plane) && clipEnabled(clip, source_flags) && (y >= clip->top) && (y < clip->top + clip->rows) &&
          (left < clip->left + clip->columns) && (left + WORLD_WIDTH > clip->left))
      {
         return 1;
      }
   }
   return 0;
}

/* a new window row in one pass over its run-length row, instead of walking the row from its start
   for each cell. rows a clip covers, or that leave the room, take loadCell per cell; the cells are
   stored in the same order either way, so the cache ends in the same state. skip is a column the
   caller loads itself. */
static void loadRow(DeceptusU8 plane, DeceptusI16 y, DeceptusI16 left, DeceptusI16 skip)
{
   SmdCellRun run;
   const DeceptusU8 *cell;
   DeceptusI16 x;
   if ((y < 0) || (y >= stage->rows) || (left < 0) || (left + WORLD_WIDTH > stage->columns) || rowHasClip(plane, y, left) ||
       !cellRunStart(&run, plane ? &stage->foreground : &stage->background, y))
   {
      for (x = left; x < left + WORLD_WIDTH; ++x)
      {
         if (x != skip)
         {
            loadCell(plane, x, y, 2);
         }
      }
      return;
   }
   for (x = 0; x < left; ++x)
   {
      if (!cellRunNext(&run))
      {
         smd_graphics_error = smd_background_error = 1;
         return;
      }
   }
   for (x = left; x < left + WORLD_WIDTH; ++x)
   {
      cell = cellRunNext(&run);
      if (!cell)
      {
         smd_graphics_error = smd_background_error = 1;
         return;
      }
      if (x != skip)
      {
         storeCell(plane, x, y, 2, ((DeceptusU16)cell[0] << 8) | cell[1], ((DeceptusU16)cell[2] << 8) | cell[3]);
      }
   }
}

static void loadWindow(DeceptusI16 left, DeceptusI16 top)
{
   DeceptusU16 plane;
   DeceptusI16 y;
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
         loadRow((DeceptusU8)plane, y, left, -1);
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
   DeceptusU16 index, duration, plane, half, rows;
   DeceptusU8 dirty, active, stepped, covered;
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
   dirty = stepped = 0;
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
            stepped |= 1U << index;
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
            loadRow((DeceptusU8)plane, top > work.top ? top + WORLD_HEIGHT - 1 : top, left, added);
         }
      }
      for (index = 0; index < stage->animation_count; ++index)
      {
         if (!(dirty & (1U << index)))
         {
            continue;
         }
         clip = &stage->animations[index];
         covered = clipCovered(index);
         clip_left = left > clip->left ? left : clip->left;
         clip_top = top > clip->top ? top : clip->top;
         clip_right = left + WORLD_WIDTH < clip->left + clip->columns ? left + WORLD_WIDTH : clip->left + clip->columns;
         clip_bottom = top + WORLD_HEIGHT < clip->top + clip->rows ? top + WORLD_HEIGHT : clip->top + clip->rows;
         for (y = clip_top; y < clip_bottom; ++y)
         {
            if (stepped & (1U << index))
            {
               refreshClipRow(clip, work.frame[index], work.frame[index] ? work.frame[index] - 1 : clip->frames - 1, y, clip_left,
                              clip_right, covered);
               continue;
            }
            for (x = clip_left; x < clip_right; ++x)
            {
               refreshCell(clip->plane, x, y);
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
      /* only the set bits of the dirty words, as in smdBackgroundCommit. */
      for (half = 0; half < 2; ++half)
      {
         index = half * 16;
         for (rows = work.dirty_rows[plane][half]; rows; rows >>= 1, ++index)
         {
            if (rows & 1)
            {
               smd_background_dma_bytes += (work.row_right[plane][index] - work.row_left[plane][index] + 1) * 2;
            }
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
   DeceptusU16 index, slot, plane, row, base, half, rows;
   if (smd_background_error)
   {
      return;
   }
   for (index = 0; index < (work.reload ? SMD_WORLD_SLOTS : work.upload_count); ++index)
   {
      slot = work.reload ? index : work.uploads[index];
      if ((work.pending[slot] & SLOT_PENDING) && work.resident[slot] != EMPTY)
      {
         if (!smdDecodeBlock(&stage->patterns, work.resident[slot], (DeceptusU8 *)tile_buffer))
         {
            return;
         }
         VDP_loadTileData(tile_buffer, slot, 1, DMA);
      }
      work.pending[slot] &= (DeceptusU8)~SLOT_PENDING;
   }
   work.reload = 0;
   work.upload_count = 0;
   for (plane = 0; plane < 2; ++plane)
   {
      base = plane ? 0xc000 : 0xe000;
      /* walk the set bits of each dirty word instead of testing all 32 rows: testing a row by its
         bit cost about 150 cycles, so a still screen spent close to 10,000 cycles finding nothing. */
      for (half = 0; half < 2; ++half)
      {
         row = half * 16;
         for (rows = work.dirty_rows[plane][half]; rows; rows >>= 1, ++row)
         {
            if (rows & 1)
            {
               DMA_doDma(DMA_VRAM, &work.maps[plane][row * 64 + work.row_left[plane][row]],
                         base + row * 128 + work.row_left[plane][row] * 2, work.row_right[plane][row] - work.row_left[plane][row] + 1, 2);
            }
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
