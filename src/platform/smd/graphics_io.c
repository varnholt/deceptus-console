#include "opening_video.h"

deceptus_u8 smd_graphics_error;

deceptus_u16 smd_graphics_tile(deceptus_u16 logical)
{
    if (logical < 1408) {
        return logical;
    }
    if (logical < 1536) {
        return logical + 256;
    }
    return logical + 448;
}

deceptus_u8 smd_decode_block(const smd_rom_blob *blob, deceptus_u16 block, deceptus_u8 *destination)
{
    const deceptus_u8 *source, *end;
    deceptus_u16 count, bytes, written, unit;
    deceptus_u8 code;
    if (block >= blob->blocks) {
        smd_graphics_error = 1;
        return 0;
    }
    source = blob->data + blob->offsets[block];
    end = blob->data + blob->offsets[block + 1];
    written = 0;
    unit = blob->unit_bytes;
    while (source < end && written < blob->block_bytes) {
        code = *source++;
        count = code < 128 ? code + 1 : code - 127;
        bytes = count * unit;
        if (bytes > blob->block_bytes - written || source + (code < 128 ? bytes : unit) > end) {
            smd_graphics_error = 1;
            return 0;
        }
        if (code < 128) {
            while (bytes--) {
                destination[written++] = *source++;
            }
        } else {
            while (count--) {
                for (bytes = 0; bytes < unit; ++bytes) {
                    destination[written++] = source[bytes];
                }
            }
            source += unit;
        }
    }
    if (written != blob->block_bytes || source != end) {
        smd_graphics_error = 1;
        return 0;
    }
    return 1;
}

const deceptus_u8 *smd_cell(const smd_rom_blob *blob, deceptus_u16 row, deceptus_u16 column)
{
    const deceptus_u8 *source, *end;
    deceptus_u16 count, bytes;
    deceptus_u8 code;
    if (row >= blob->blocks || column >= blob->block_bytes / 4 || blob->unit_bytes != 4) {
        smd_graphics_error = 1;
        return 0;
    }
    source = blob->data + blob->offsets[row];
    end = blob->data + blob->offsets[row + 1];
    while (source < end) {
        code = *source++;
        count = code < 128 ? code + 1 : code - 127;
        bytes = code < 128 ? count * 4 : 4;
        if (source + bytes > end) {
            break;
        }
        if (column < count) {
            return source + (code < 128 ? column * 4 : 0);
        }
        column -= count;
        source += bytes;
    }
    smd_graphics_error = 1;
    return 0;
}
