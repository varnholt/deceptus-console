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

typedef struct snes_background_dma_command {
    deceptus_u16 source;
    deceptus_u8 bank, increment;
    deceptus_u16 target, bytes;
} snes_background_dma_command;

typedef char snes_background_dma_command_size[(sizeof(snes_background_dma_command) == 8) ? 1 : -1];
extern void snes_background_run_dma(const snes_background_dma_command *commands, deceptus_u16 count);

typedef struct snes_background_work {
    deceptus_u16 resident[SNES_BG_SLOTS], references[SNES_BG_SLOTS], free_slots[SNES_BG_SLOTS];
    deceptus_u16 hash_keys[SNES_BG_HASH], hash_slots[SNES_BG_HASH];
    deceptus_u16 maps[2][2048], uploads[SNES_BG_SLOTS], column[32];
    deceptus_u16 staged_columns[2][32], dma_count;
    snes_background_dma_command dma[SNES_BG_DMA_COMMANDS];
    deceptus_u16 clip_frame[SNES_BG_CLIPS], clip_elapsed[SNES_BG_CLIPS], clip_cells[SNES_BG_CLIPS];
    deceptus_u8 pending[SNES_BG_SLOTS], domains[SNES_BG_SLOTS], dirty_column[2];
    deceptus_u16 dirty_rows[2][2], dma_bytes, free_count, upload_count, flags, room_cells, hash_tombstones;
    deceptus_i16 left, top;
    deceptus_u8 loaded;
} snes_background_work;

typedef char snes_background_storage_fits[(sizeof(snes_background_work) <= 32768) ? 1 : -1];
extern deceptus_u8 snes_background_storage[];
#define work ((snes_background_work *)snes_background_storage)

deceptus_u8 snes_background_error, snes_background_full;
deceptus_u16 snes_background_uploads;
deceptus_u16 snes_background_dma_bytes;
deceptus_u16 snes_background_compactions;
deceptus_u16 snes_background_hash_probes, snes_background_hash_rebuilds;
static const snes_stage *stage;
static deceptus_u16 source_flags;
static deceptus_i16 scroll_x, scroll_y;

static deceptus_u16 map_address(deceptus_i16 x, deceptus_i16 y)
{
    return (x & 31) + ((y & 31) << 5) + ((x & 32) ? 1024 : 0);
}

static deceptus_u16 slot_tile(deceptus_u8 plane, deceptus_u16 slot)
{
    if (!plane) {
        return slot;
    }
    return slot < 768 ? slot + 256 : slot - 1024 + 208;
}

static deceptus_u16 tile_slot(deceptus_u8 plane, deceptus_u16 tile)
{
    tile &= 1023;
    if (!plane) {
        return tile;
    }
    return tile >= 256 ? tile - 256 : tile - 208 + 1024;
}

static deceptus_u8 slot_addressable(deceptus_u8 plane, deceptus_u16 slot)
{
    if (!plane) {
        return slot < 1024;
    }
    if (slot < 768) {
        return 1;
    }
    return slot >= 1024;
}

static deceptus_u16 hash_find(deceptus_u16 pattern, deceptus_u8 insert)
{
    deceptus_u16 key, count, deleted;
    key = (pattern ^ (pattern >> 5)) & (SNES_BG_HASH - 1);
    deleted = SNES_BG_EMPTY;
    for (count = 0; count < SNES_BG_HASH; ++count) {
        if (work->hash_keys[key] == pattern) {
            if (count + 1 > snes_background_hash_probes) {
                snes_background_hash_probes = count + 1;
            }
            return key;
        }
        if (work->hash_keys[key] == SNES_BG_EMPTY) {
            if (count + 1 > snes_background_hash_probes) {
                snes_background_hash_probes = count + 1;
            }
            return insert && deleted != SNES_BG_EMPTY ? deleted : key;
        }
        if (insert && deleted == SNES_BG_EMPTY && work->hash_keys[key] == SNES_BG_DELETED) {
            deleted = key;
        }
        key = (key + 1) & (SNES_BG_HASH - 1);
    }
    snes_background_hash_probes = SNES_BG_HASH;
    return deleted;
}

static void rebuild_hash(void)
{
    deceptus_u16 index, domain, key, pattern;
    for (index = 0; index < SNES_BG_HASH; ++index) {
        work->hash_keys[index] = SNES_BG_EMPTY;
    }
    work->hash_tombstones = 0;
    for (index = 1; index < SNES_BG_SLOTS; ++index) {
        if (work->references[index] == 0) {
            continue;
        }
        for (domain = 0; domain < 2; ++domain) {
            if (work->domains[index] & (1U << domain)) {
                pattern = work->resident[index] | (domain << 14);
                key = hash_find(pattern, 1);
                if (key == SNES_BG_EMPTY) {
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

static void reset_cache(void)
{
    deceptus_u16 index, plane;
    for (index = 0; index < SNES_BG_HASH; ++index) {
        work->hash_keys[index] = SNES_BG_EMPTY;
    }
    for (index = 0; index < SNES_BG_SLOTS; ++index) {
        work->resident[index] = SNES_BG_EMPTY;
        work->references[index] = 0;
        work->pending[index] = 0;
        work->domains[index] = 0;
        if (index) {
            work->free_slots[index - 1] = index;
        }
    }
    for (plane = 0; plane < 2; ++plane) {
        for (index = 0; index < 2048; ++index) {
            work->maps[plane][index] = slot_tile((deceptus_u8)plane, 0);
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

static void release_cell(deceptus_u8 plane, deceptus_i16 x, deceptus_i16 y)
{
    deceptus_u16 address, slot, key, domain;
    address = map_address(x, y);
    slot = tile_slot(plane, work->maps[plane][address]);
    work->maps[plane][address] = slot_tile(plane, 0);
    if (slot == 0) {
        return;
    }
    if (work->references[slot] == 0) {
        return;
    }
    if (!--work->references[slot]) {
        for (domain = 0; domain < 2; ++domain) {
            if (work->domains[slot] & (1U << domain)) {
                key = hash_find(work->resident[slot] | (domain << 14), 0);
                if (key != SNES_BG_EMPTY) {
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

static deceptus_u16 acquire_pattern(deceptus_u8 plane, deceptus_u16 pattern)
{
    deceptus_u16 key, slot, domain_key, other, index;
    if (!pattern) {
        return 0;
    }
    if (pattern >= stage->pattern_count) {
        snes_background_error = 1;
        return 0;
    }
    domain_key = pattern | ((deceptus_u16)plane << 14);
    key = hash_find(domain_key, 1);
    if (key == SNES_BG_EMPTY) {
        snes_background_error = 2;
        return 0;
    }
    if (work->hash_keys[key] == domain_key) {
        slot = work->hash_slots[key];
    } else {
        slot = SNES_BG_EMPTY;
        other = hash_find(domain_key ^ 16384, 0);
        if (other != SNES_BG_EMPTY && work->hash_keys[other] == (domain_key ^ 16384)) {
            index = work->hash_slots[other];
            if (slot_addressable(plane, index)) {
                slot = index;
            }
        }
        if (slot == SNES_BG_EMPTY) {
            index = work->free_count;
            while (index) {
                --index;
                if (slot_addressable(plane, work->free_slots[index])) {
                    slot = work->free_slots[index];
                    work->free_slots[index] = work->free_slots[--work->free_count];
                    break;
                }
            }
            if (slot == SNES_BG_EMPTY) {
                snes_background_error = 2;
                return 0;
            }
            work->resident[slot] = pattern;
            if (!work->pending[slot]) {
                work->pending[slot] = 1;
                work->uploads[work->upload_count++] = slot;
                work->dma_bytes += 32;
            }
        }
        if (work->hash_keys[key] == SNES_BG_DELETED) {
            --work->hash_tombstones;
        }
        work->hash_keys[key] = domain_key;
        work->hash_slots[key] = slot;
        work->domains[slot] |= (deceptus_u8)(1U << plane);
    }
    ++work->references[slot];
    return slot;
}

static deceptus_u8 clip_enabled(const snes_background_animation *clip, deceptus_u16 flags)
{
    if ((flags & clip->required_flags) != clip->required_flags) {
        return 0;
    }
    if (flags & clip->forbidden_flags) {
        return 0;
    }
    return 1;
}

static void load_cell(deceptus_u8 plane, deceptus_i16 x, deceptus_i16 y, deceptus_u8 dirty)
{
    deceptus_u16 pattern, attributes, index, offset, limit, within, bit, row;
    const snes_rom_blob *blob;
    const snes_background_animation *clip;
    const deceptus_u16 *words;
    pattern = 0;
    attributes = 0;
    if (x >= 0 && y >= 0 && x < stage->columns && y < stage->rows) {
        blob = plane ? &stage->foreground : &stage->background;
        offset = (deceptus_u16)y * stage->columns + x;
        limit = work->room_cells;
        for (index = 0; index < stage->animation_count; ++index) {
            clip = &stage->animations[index];
            if (clip->plane == plane && clip_enabled(clip, source_flags) && x >= clip->left && y >= clip->top &&
                x < clip->left + clip->columns && y < clip->top + clip->rows) {
                blob = &clip->cells;
                offset = (work->clip_frame[index] * clip->rows + (y - clip->top)) * clip->columns + (x - clip->left);
                limit = work->clip_cells[index];
            }
        }
        if (offset >= limit) {
            snes_background_error = 1;
        } else {
            words = (const deceptus_u16 *)blob->chunks[offset >> 13];
            within = (offset & 8191) * 2;
            pattern = words[within];
            attributes = words[within + 1];
        }
    }
    index = map_address(x, y);
    pattern = acquire_pattern(plane, pattern);
    attributes = (attributes & 0xdc00) | (plane ? 0x2000 : 0);
    work->maps[plane][index] = slot_tile(plane, pattern) | attributes;
    if (dirty == 1) {
        if (work->dirty_column[plane] == 255) {
            work->dirty_column[plane] = (deceptus_u8)(x & 63);
            work->dma_bytes += 64;
        } else if (work->dirty_column[plane] != (x & 63)) {
            snes_background_error = 1;
        }
    } else if (dirty == 2) {
        row = (y & 31) >> 4;
        bit = 1U << (y & 15);
        if (!(work->dirty_rows[plane][row] & bit)) {
            work->dirty_rows[plane][row] |= bit;
            work->dma_bytes += 128;
        }
    }
}

void snes_background_load(deceptus_u8 room)
{
    deceptus_u16 index;
    deceptus_u32 cells;
    const snes_background_animation *clip;
    snes_background_error = 0;
    snes_background_uploads = 0;
    snes_background_compactions = 0;
    snes_background_hash_probes = 0;
    snes_background_hash_rebuilds = 0;
    snes_background_full = 1;
    if (room >= SNES_STAGE_COUNT) {
        snes_background_error = 1;
        room = 0;
    }
    stage = &snes_stages[room];
    if (stage->animation_count > SNES_BG_CLIPS || stage->pattern_count > 16384) {
        snes_background_error = 1;
    }
    cells = (deceptus_u32)stage->columns * stage->rows;
    if (cells == 0 || cells > 65535 || stage->background.bytes != cells * 4 || stage->foreground.bytes != cells * 4 ||
        stage->patterns.bytes != (deceptus_u32)stage->pattern_count * 32) {
        snes_background_error = 1;
    }
    work->room_cells = (deceptus_u16)cells;
    work->loaded = 0;
    work->flags = 0;
    source_flags = 0;
    for (index = 0; index < SNES_BG_CLIPS; ++index) {
        work->clip_frame[index] = 0;
        work->clip_elapsed[index] = 0;
        work->clip_cells[index] = 0;
        if (index < stage->animation_count) {
            clip = &stage->animations[index];
            cells = (deceptus_u32)clip->frames * clip->columns * clip->rows;
            if (cells == 0 || cells > 65535 || clip->cells.bytes != cells * 4) {
                snes_background_error = 1;
            }
            work->clip_cells[index] = (deceptus_u16)cells;
        }
    }
    bgSetGfxPtr(0, 0x2000);
    bgSetGfxPtr(1, 0x1000);
    bgSetMapPtr(0, 0x0000, SC_64x32);
    bgSetMapPtr(1, 0x0800, SC_64x32);
    dmaCopyCGram((u8 *)stage->palette + 32, 16, 224);
}

void snes_background_set_flags(deceptus_u16 flags) { source_flags = flags; }

static void load_window(deceptus_i16 left, deceptus_i16 top)
{
    deceptus_i16 x, y;
    deceptus_u16 plane;
    reset_cache();
    snes_background_full = 1;
    for (plane = 0; plane < 2; ++plane) {
        for (y = top; y < top + SNES_BG_HEIGHT; ++y) {
            for (x = left; x < left + SNES_BG_WIDTH; ++x) {
                load_cell((deceptus_u8)plane, x, y, 0);
            }
        }
    }
    work->loaded = 1;
}

static void stage_dma(const void *source, deceptus_u16 target, deceptus_u16 bytes, deceptus_u8 increment)
{
    dmaMemory memory;
    deceptus_u16 index;
    if (work->dma_count >= SNES_BG_DMA_COMMANDS) {
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

static void prepare_dma(void)
{
    deceptus_u16 index, slot, pattern, target, plane, row, column, base, mask;
    const deceptus_u8 *bytes;
    work->dma_count = 0;
    if (work->dma_bytes == 0) {
        return;
    }
    for (index = 0; index < work->upload_count; ++index) {
        slot = work->uploads[index];
        pattern = work->resident[slot];
        if (pattern != SNES_BG_EMPTY) {
            bytes = stage->patterns.chunks[pattern >> 10] + (pattern & 1023) * 32;
            target = slot < 1024 ? 0x2000 + slot * 16 : 0x1d00 + (slot - 1024) * 16;
            stage_dma(bytes, target, 32, 0x80);
        }
    }
    for (plane = 0; plane < 2; ++plane) {
        base = plane ? 0x0800 : 0;
        mask = work->dirty_rows[plane][0];
        for (row = 0; row < 32; ++row) {
            if (row == 16) {
                mask = work->dirty_rows[plane][1];
            }
            if (mask & 1) {
                stage_dma(&work->maps[plane][row * 32], base + row * 32, 64, 0x80);
                stage_dma(&work->maps[plane][1024 + row * 32], base + 1024 + row * 32, 64, 0x80);
            }
            mask >>= 1;
        }
        column = work->dirty_column[plane];
        if (column < 64) {
            for (row = 0; row < 32; ++row) {
                index = (column & 31) + row * 32 + (column >= 32 ? 1024 : 0);
                slot = work->maps[plane][index];
                work->staged_columns[plane][row] = slot;
            }
            stage_dma(work->staged_columns[plane], base + (column & 31) + (column >= 32 ? 1024 : 0), 64, 0x81);
        }
    }
    if (work->dma_count == SNES_BG_EMPTY) {
        snes_background_full = 1;
    } else if ((deceptus_u32)work->dma_count * 768 + (deceptus_u32)snes_background_dma_bytes * 8 > 36000) {
        /* leave time for the sdk nmi and oam transfer in the ntsc vblank. */
        snes_background_full = 1;
    }
    if (work->dma_count != SNES_BG_EMPTY) {
        for (index = 0; index < work->upload_count; ++index) {
            slot = work->uploads[index];
            work->pending[slot] = 0;
        }
        work->upload_count = 0;
        for (plane = 0; plane < 2; ++plane) {
            work->dirty_rows[plane][0] = 0;
            work->dirty_rows[plane][1] = 0;
            work->dirty_column[plane] = 255;
        }
    }
}

void snes_background_prepare(deceptus_i16 camera_x, deceptus_i16 camera_y)
{
    deceptus_i16 left, top, x, y, added_column;
    deceptus_u16 index, plane, duration;
    deceptus_u8 dirty_clips, active, full_refresh;
    const snes_background_animation *clip;
    if (snes_background_error) {
        return;
    }
    scroll_x = camera_x;
    scroll_y = camera_y;
    work->dma_bytes = 0;
    snes_background_hash_probes = 0;
    if (work->loaded) {
        if (work->hash_tombstones > 128) {
            rebuild_hash();
        }
    }
    left = camera_x / 8;
    top = camera_y / 8;
    dirty_clips = 0;
    for (index = 0; index < stage->animation_count; ++index) {
        clip = &stage->animations[index];
        active = clip_enabled(clip, source_flags);
        if (active != clip_enabled(clip, work->flags)) {
            dirty_clips |= (deceptus_u8)(1U << index);
            work->clip_frame[index] = 0;
            work->clip_elapsed[index] = 0;
        } else if (active && clip->frames > 1) {
            duration = clip->durations ? clip->durations[work->clip_frame[index]] : clip->frame_ticks;
            if (!duration) {
                snes_background_error = 1;
                return;
            }
            if (++work->clip_elapsed[index] >= duration) {
                work->clip_elapsed[index] = 0;
                if (++work->clip_frame[index] >= clip->frames) {
                    work->clip_frame[index] = 0;
                }
                dirty_clips |= (deceptus_u8)(1U << index);
            }
        }
    }
    full_refresh = work->loaded == 0;
    if (left < work->left - 1 || left > work->left + 1 || top < work->top - 1 || top > work->top + 1) {
        full_refresh = 1;
    }
    if (full_refresh) {
        load_window(left, top);
    } else {
        for (plane = 0; plane < 2; ++plane) {
            if (left != work->left) {
                x = left > work->left ? work->left : work->left + SNES_BG_WIDTH - 1;
                for (y = work->top; y < work->top + SNES_BG_HEIGHT; ++y) {
                    release_cell((deceptus_u8)plane, x, y);
                }
            }
            if (top != work->top) {
                y = top > work->top ? work->top : work->top + SNES_BG_HEIGHT - 1;
                for (x = work->left; x < work->left + SNES_BG_WIDTH; ++x) {
                    release_cell((deceptus_u8)plane, x, y);
                }
            }
            added_column = -1;
            if (left != work->left) {
                added_column = left > work->left ? left + SNES_BG_WIDTH - 1 : left;
                for (y = top; y < top + SNES_BG_HEIGHT; ++y) {
                    load_cell((deceptus_u8)plane, added_column, y, 1);
                }
            }
            if (top != work->top) {
                y = top > work->top ? top + SNES_BG_HEIGHT - 1 : top;
                for (x = left; x < left + SNES_BG_WIDTH; ++x) {
                    if (x != added_column) {
                        load_cell((deceptus_u8)plane, x, y, 2);
                    }
                }
            }
        }
        for (index = 0; index < stage->animation_count; ++index) {
            if (!(dirty_clips & (1U << index))) {
                continue;
            }
            clip = &stage->animations[index];
            for (y = top; y < top + SNES_BG_HEIGHT; ++y) {
                for (x = left; x < left + SNES_BG_WIDTH; ++x) {
                    if (x >= clip->left && x < clip->left + clip->columns && y >= clip->top &&
                        y < clip->top + clip->rows) {
                        release_cell(clip->plane, x, y);
                    }
                }
            }
            for (y = top; y < top + SNES_BG_HEIGHT; ++y) {
                for (x = left; x < left + SNES_BG_WIDTH; ++x) {
                    if (x >= clip->left && x < clip->left + clip->columns && y >= clip->top &&
                        y < clip->top + clip->rows) {
                        load_cell(clip->plane, x, y, 2);
                    }
                }
            }
        }
    }
    if (snes_background_error == 2) {
        if (snes_background_full == 0) {
            /* plane address domains can fragment although the visible set fits.
             * rebuild once before publishing; a genuine budget failure stays an error. */
            snes_background_error = 0;
            ++snes_background_compactions;
            load_window(left, top);
        }
    }
    work->left = left;
    work->top = top;
    work->flags = source_flags;
    snes_background_uploads = work->upload_count;
    snes_background_dma_bytes = work->dma_bytes;
    prepare_dma();
}

static void copy_column(const deceptus_u16 *source, deceptus_u16 address)
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

void snes_background_commit(void)
{
    deceptus_u16 index, slot, pattern, plane, row, column, base, mask;
    const deceptus_u8 *bytes;
    if (snes_background_error) {
        return;
    }
    if (work->dma_count != SNES_BG_EMPTY) {
        snes_background_run_dma(work->dma, work->dma_count);
        bgSetScroll(0, scroll_x, scroll_y);
        bgSetScroll(1, scroll_x, scroll_y);
        snes_background_full = 0;
        return;
    }
    for (index = 0; index < work->upload_count; ++index) {
        slot = work->uploads[index];
        pattern = work->resident[slot];
        if (pattern != SNES_BG_EMPTY) {
            bytes = stage->patterns.chunks[pattern >> 10] + (pattern & 1023) * 32;
            row = slot < 1024 ? 0x2000 + slot * 16 : 0x1d00 + (slot - 1024) * 16;
            dmaCopyVram((u8 *)bytes, row, 32);
        }
        work->pending[slot] = 0;
    }
    work->upload_count = 0;
    for (plane = 0; plane < 2; ++plane) {
        base = plane ? 0x0800 : 0;
        mask = work->dirty_rows[plane][0];
        for (row = 0; row < 32; ++row) {
            if (row == 16) {
                mask = work->dirty_rows[plane][1];
            }
            if (mask & 1) {
                dmaCopyVram((u8 *)&work->maps[plane][row * 32], base + row * 32, 64);
                dmaCopyVram((u8 *)&work->maps[plane][1024 + row * 32], base + 1024 + row * 32, 64);
            }
            mask >>= 1;
        }
        work->dirty_rows[plane][0] = 0;
        work->dirty_rows[plane][1] = 0;
        column = work->dirty_column[plane];
        if (column < 64) {
            for (row = 0; row < 32; ++row) {
                index = (column & 31) + row * 32 + (column >= 32 ? 1024 : 0);
                slot = work->maps[plane][index];
                work->column[row] = slot;
            }
            copy_column(work->column, base + (column & 31) + (column >= 32 ? 1024 : 0));
            work->dirty_column[plane] = 255;
        }
    }
    bgSetScroll(0, scroll_x, scroll_y);
    bgSetScroll(1, scroll_x, scroll_y);
    snes_background_full = 0;
}
