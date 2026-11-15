/* compare the native obj writer with the c reference inside an actual rom. */
#include "deceptus/adventure.h"
#include "snes_assets.h"
#include <snes.h>
#define SNES_SPRITES_REFERENCE 1
#include "sprites.h"

volatile deceptus_u16 snes_sprite_test[16];
static deceptus_u8 before_oam[544], before_lines[224], before_slivers[224];
static deceptus_u8 expected_oam[544], expected_lines[224], expected_slivers[224];
static snes_obj_slot before_slots[128], expected_slots[128];
static snes_obj_request test_request;
static deceptus_u16 seed = 1729;

static deceptus_u16 random_word(void)
{
    seed = seed * 109 + 89;
    return seed;
}

int main(void)
{
    deceptus_u16 index, tile, before_count, expected_count;
    deceptus_i16 x, y;
    deceptus_u8 palette, flip, priority, size, before_overflow, expected_overflow;
    consoleInit();
    snes_sprite_test[0] = 0x53a7;
    snes_sprite_test[1] = 0xd19e;
    for (index = 0; index < 4096; ++index) {
        if (!(index & 31)) {
            memset(snes_obj_oam, 0, sizeof(snes_obj_oam));
            memset(snes_obj_lines, 0, sizeof(snes_obj_lines));
            memset(snes_obj_slivers, 0, sizeof(snes_obj_slivers));
            snes_obj_count = 0;
            snes_sprite_overflow = 0;
        }
        x = (deceptus_i16)(random_word() % 320) - 32;
        y = (deceptus_i16)(random_word() % 288) - 32;
        tile = random_word() & 511;
        palette = random_word() & 7;
        flip = (random_word() >> 4) & 1;
        priority = (random_word() >> 4) & 3;
        size = (random_word() & 1) ? 8 : 16;
        if ((index & 31) == 1) {
            x = -8;
            y = -8;
            size = 16;
        } else if ((index & 31) == 2) {
            x = -16;
            y = 40;
            size = 16;
        } else if ((index & 31) == 3) {
            x = 255;
            y = 223;
        } else if ((index & 31) == 4) {
            x = 256;
            y = 224;
        } else if ((index & 31) == 5) {
            x = 0;
            y = 40;
            snes_obj_lines[40] = 32;
        } else if ((index & 31) == 6) {
            x = 0;
            y = 60;
            snes_obj_slivers[60] = 34;
        } else if ((index & 31) == 7) {
            x = 0;
            y = 80;
            snes_obj_count = 128;
        } else if ((index & 31) == 8) {
            snes_obj_count = 0;
        }
        memcpy(before_oam, snes_obj_oam, sizeof(before_oam));
        memcpy(before_lines, snes_obj_lines, sizeof(before_lines));
        memcpy(before_slivers, snes_obj_slivers, sizeof(before_slivers));
        before_count = snes_obj_count;
        before_overflow = snes_sprite_overflow;
        if (index < 2048) {
            snes_obj_piece_reference(x, y, tile, palette, flip, priority, size);
        } else {
            tile &= 127;
            size = index & 15;
            snes_obj_emit_quads_reference(x, y, tile, palette, flip, priority, size);
        }
        memcpy(expected_oam, snes_obj_oam, sizeof(expected_oam));
        memcpy(expected_lines, snes_obj_lines, sizeof(expected_lines));
        memcpy(expected_slivers, snes_obj_slivers, sizeof(expected_slivers));
        expected_count = snes_obj_count;
        expected_overflow = snes_sprite_overflow;
        memcpy(snes_obj_oam, before_oam, sizeof(before_oam));
        memcpy(snes_obj_lines, before_lines, sizeof(before_lines));
        memcpy(snes_obj_slivers, before_slivers, sizeof(before_slivers));
        snes_obj_count = before_count;
        snes_sprite_overflow = before_overflow;
        if (index < 2048) {
            snes_obj_piece(x, y, tile, palette, flip, priority, size);
        } else {
            snes_obj_emit_quads(x, y, tile, palette, flip, priority, size);
        }
        snes_sprite_test[3] = index;
        snes_sprite_test[4] = x;
        snes_sprite_test[5] = y;
        snes_sprite_test[6] = size;
        snes_sprite_test[7] = snes_obj_count;
        snes_sprite_test[8] = expected_count;
        snes_sprite_test[9] = (deceptus_u16)memcmp(expected_oam, snes_obj_oam, sizeof(expected_oam));
        snes_sprite_test[10] = (deceptus_u16)memcmp(expected_lines, snes_obj_lines, sizeof(expected_lines));
        snes_sprite_test[11] = (deceptus_u16)memcmp(expected_slivers, snes_obj_slivers, sizeof(expected_slivers));
        if (snes_obj_count != expected_count) {
            break;
        }
        if (snes_sprite_overflow != expected_overflow) {
            break;
        }
        if (snes_sprite_test[9]) {
            break;
        }
        if (snes_sprite_test[10]) {
            break;
        }
        if (snes_sprite_test[11]) {
            break;
        }
    }
    if (index != 4096) {
        snes_sprite_test[2] = 2;
    } else {
        for (index = 0; index < 256; ++index) {
            memset(snes_obj_oam, index, sizeof(snes_obj_oam));
            memset(snes_obj_lines, index + 17, sizeof(snes_obj_lines));
            memset(snes_obj_slivers, index + 29, sizeof(snes_obj_slivers));
            memset(snes_obj_slots, index * 13, sizeof(snes_obj_slots));
            memcpy(before_oam, snes_obj_oam, sizeof(before_oam));
            memcpy(before_lines, snes_obj_lines, sizeof(before_lines));
            memcpy(before_slivers, snes_obj_slivers, sizeof(before_slivers));
            memcpy(before_slots, snes_obj_slots, sizeof(before_slots));
            snes_obj_begin_reference();
            memcpy(expected_oam, snes_obj_oam, sizeof(expected_oam));
            memcpy(expected_lines, snes_obj_lines, sizeof(expected_lines));
            memcpy(expected_slivers, snes_obj_slivers, sizeof(expected_slivers));
            memcpy(expected_slots, snes_obj_slots, sizeof(expected_slots));
            memcpy(snes_obj_oam, before_oam, sizeof(before_oam));
            memcpy(snes_obj_lines, before_lines, sizeof(before_lines));
            memcpy(snes_obj_slivers, before_slivers, sizeof(before_slivers));
            memcpy(snes_obj_slots, before_slots, sizeof(before_slots));
            snes_obj_begin();
            snes_sprite_test[3] = 4096 + index;
            snes_sprite_test[9] = (deceptus_u16)memcmp(expected_oam, snes_obj_oam, sizeof(expected_oam));
            snes_sprite_test[10] = (deceptus_u16)memcmp(expected_lines, snes_obj_lines, sizeof(expected_lines));
            snes_sprite_test[11] = (deceptus_u16)memcmp(expected_slivers, snes_obj_slivers, sizeof(expected_slivers));
            snes_sprite_test[12] = (deceptus_u16)memcmp(expected_slots, snes_obj_slots, sizeof(expected_slots));
            if (snes_sprite_test[9]) {
                break;
            }
            if (snes_sprite_test[10]) {
                break;
            }
            if (snes_sprite_test[11]) {
                break;
            }
            if (snes_sprite_test[12]) {
                break;
            }
        }
        if (index != 256) {
            snes_sprite_test[2] = 2;
        } else {
            for (index = 0; index < SNES_SPRITE_RESOURCE_COUNT * 2; ++index) {
                test_request.resource = index;
                if (test_request.resource >= SNES_SPRITE_RESOURCE_COUNT) {
                    test_request.resource -= SNES_SPRITE_RESOURCE_COUNT;
                }
                tile = test_request.resource & 3;
                test_request.x = tile == 0 ? -24 : tile == 1 ? 0 : tile == 2 ? 240 : 64;
                test_request.y = tile == 0 ? -16 : tile == 1 ? 0 : tile == 2 ? 216 : 48;
                test_request.flip = index >= SNES_SPRITE_RESOURCE_COUNT;
                test_request.priority = (index >> 2) & 3;
                palette = (index >> 4) & 7;
                snes_obj_reset_cache();
                snes_obj_begin_reference();
                snes_obj_count = snes_sprite_overflow = snes_sprite_blank = 0;
                snes_obj_draw_resource_reference(&test_request, palette);
                memcpy(expected_oam, snes_obj_oam, sizeof(expected_oam));
                memcpy(expected_lines, snes_obj_lines, sizeof(expected_lines));
                memcpy(expected_slivers, snes_obj_slivers, sizeof(expected_slivers));
                memcpy(expected_slots, snes_obj_slots, sizeof(expected_slots));
                expected_count = snes_obj_count;
                expected_overflow = snes_sprite_overflow;
                snes_obj_reset_cache();
                snes_obj_begin_reference();
                snes_obj_count = snes_sprite_overflow = snes_sprite_blank = 0;
                snes_obj_draw_resource(&test_request, palette);
                snes_sprite_test[3] = 4352 + index;
                snes_sprite_test[4] = test_request.resource;
                snes_sprite_test[7] = snes_obj_count;
                snes_sprite_test[8] = expected_count;
                snes_sprite_test[9] = (deceptus_u16)memcmp(expected_oam, snes_obj_oam, sizeof(expected_oam));
                snes_sprite_test[10] = (deceptus_u16)memcmp(expected_lines, snes_obj_lines, sizeof(expected_lines));
                snes_sprite_test[11] =
                    (deceptus_u16)memcmp(expected_slivers, snes_obj_slivers, sizeof(expected_slivers));
                snes_sprite_test[12] = (deceptus_u16)memcmp(expected_slots, snes_obj_slots, sizeof(expected_slots));
                if (snes_obj_count != expected_count) {
                    break;
                }
                if (snes_sprite_overflow != expected_overflow) {
                    break;
                }
                if (snes_sprite_test[9]) {
                    break;
                }
                if (snes_sprite_test[10]) {
                    break;
                }
                if (snes_sprite_test[11]) {
                    break;
                }
                if (snes_sprite_test[12]) {
                    break;
                }
            }
            snes_sprite_test[2] = index == SNES_SPRITE_RESOURCE_COUNT * 2 ? 1 : 2;
        }
    }
    while (1) {
        WaitForVBlank();
    }
    return 0;
}
