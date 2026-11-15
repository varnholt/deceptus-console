/* original text and bitmap glyphs; console tile layout is backend-local. */
static deceptus_u16 smd_ui_map[2048];
static deceptus_u8 smd_ui_dirty, smd_ui_valid, smd_ui_overflow, smd_ui_font_ready;
static deceptus_u8 smd_ui_window_row, smd_ui_window_bottom;
static deceptus_u32 smd_ui_font_buffer[8];
static deceptus_u8 smd_ui_mode, smd_ui_selection, smd_ui_room, smd_ui_page, smd_ui_prompt, smd_ui_label;
static deceptus_u16 smd_ui_flags;
static const deceptus_interaction *smd_ui_dialogue;

static deceptus_u8 smd_ui_columns(deceptus_u8 character)
{
    if (character < 32 || character > 126) {
        character = '?';
    }
    return smd_font_glyphs[character - 32].columns;
}

static void smd_ui_glyph(deceptus_u16 row, deceptus_u16 column, deceptus_u8 character)
{
    const smd_font_glyph *glyph;
    deceptus_u16 y, x;
    if (character < 32 || character > 126) {
        character = '?';
    }
    glyph = &smd_font_glyphs[character - 32];
    for (y = 0; y < SMD_FONT_ROWS && row + y < 28; ++y) {
        for (x = 0; x < glyph->columns && column + x < 32; ++x) {
            smd_ui_map[(row + y) * 64 + column + x + 4] =
                smd_graphics_tile(SMD_FONT_FIRST + glyph->tile + y * glyph->columns + x) | 0xc000;
        }
    }
}

static void smd_ui_text(deceptus_u16 row, deceptus_u16 column, const char *text, deceptus_u16 width, deceptus_u8 lines)
{
    const char *word;
    deceptus_u16 origin, word_width;
    deceptus_u8 character;
    origin = column;
    word = text;
    while (*text && lines) {
        if (*text == '\n') {
            ++text;
            row += 3;
            column = origin;
            --lines;
            continue;
        }
        if (*text != ' ' && (text == word || column == origin)) {
            word_width = 0;
            for (word = text; *word && *word != ' ' && *word != '\n'; ++word) {
                word_width += smd_ui_columns((deceptus_u8)*word);
            }
            if (column > origin && column + word_width > origin + width) {
                row += 3;
                column = origin;
                --lines;
                if (!lines) {
                    break;
                }
            }
        }
        character = (deceptus_u8)*text++;
        if (column + smd_ui_columns(character) > origin + width) {
            row += 3;
            column = origin;
            --lines;
            if (!lines) {
                break;
            }
        }
        if (character == ' ' && column == origin) {
            word = text;
            continue;
        }
        smd_ui_glyph(row, column, character);
        column += smd_ui_columns(character);
        if (character == ' ') {
            word = text;
        }
    }
    if (*text) {
        smd_ui_overflow = 1;
    }
}

static void smd_ui_box(deceptus_u16 row, deceptus_u16 rows)
{
    deceptus_u16 y, x;
    for (y = row; y < row + rows && y < 28; ++y) {
        for (x = 1; x < 31; ++x) {
            smd_ui_map[y * 64 + x + 4] = smd_graphics_tile(SMD_FONT_FIRST) | 0xc000;
        }
    }
}

static void smd_ui_option(deceptus_u16 row, const char *text, deceptus_u8 selected)
{
    smd_ui_text(row, 5, selected ? ">" : " ", 1, 1);
    smd_ui_text(row, 7, text, 22, 1);
}

static void smd_ui_init(void)
{
    deceptus_u16 index;
    if (!smd_ui_font_ready) {
        for (index = 0; index < SMD_FONT_TILES; ++index) {
            if (!smd_decode_block(&smd_font, index, (deceptus_u8 *)smd_ui_font_buffer)) {
                return;
            }
            VDP_loadTileData(smd_ui_font_buffer, smd_graphics_tile(SMD_FONT_FIRST + index), 1, DMA);
        }
        smd_ui_font_ready = 1;
    }
    memset(smd_ui_map, 0, sizeof(smd_ui_map));
    smd_ui_dirty = 1;
    smd_ui_valid = 0;
}

static void smd_ui_prepare(const deceptus_adventure *game)
{
    deceptus_u16 index;
    deceptus_u8 mode, label;
    const deceptus_adventure_room *room;
    mode = game->campaign.mode;
    label = game->room_label_ticks && mode == DECEPTUS_CAMPAIGN_PLAYING;
    if (smd_ui_valid && smd_ui_mode == mode && smd_ui_selection == game->selection &&
        smd_ui_room == game->campaign.room_index && smd_ui_page == game->page && smd_ui_prompt == game->prompt &&
        smd_ui_label == label && smd_ui_dialogue == game->dialogue && smd_ui_flags == game->flags) {
        return;
    }
    smd_ui_valid = 1;
    smd_ui_mode = mode;
    smd_ui_selection = game->selection;
    smd_ui_room = game->campaign.room_index;
    smd_ui_page = game->page;
    smd_ui_prompt = game->prompt;
    smd_ui_label = label;
    smd_ui_dialogue = game->dialogue;
    smd_ui_flags = game->flags;
    smd_ui_overflow = 0;
    smd_ui_window_bottom = 1;
    smd_ui_window_row = 0;
    if (mode == DECEPTUS_CAMPAIGN_PLAYING) {
        room = &game->definition->rooms[game->campaign.room_index];
        if (game->prompt < room->interaction_count) {
            smd_ui_window_row = 23;
        } else {
            smd_ui_window_bottom = 0;
            smd_ui_window_row = label ? 4 : 0;
        }
    } else if (mode == DECEPTUS_DIALOGUE || mode == DECEPTUS_NOTIFICATION) {
        smd_ui_window_row = 12;
    }
    for (index = 0; index < 2048; ++index) {
        smd_ui_map[index] = 0;
    }
    if (mode == DECEPTUS_CAMPAIGN_TITLE) {
        smd_ui_box(3, 22);
        smd_ui_text(5, 11, "DECEPTUS", 20, 1);
        smd_ui_option(11, "NEW GAME", game->selection == 0);
        smd_ui_option(14, "OPTIONS", game->selection == 1);
        smd_ui_option(17, "EXIT", game->selection == 2);
    } else if ((mode == DECEPTUS_DIALOGUE || mode == DECEPTUS_NOTIFICATION) && game->dialogue) {
        smd_ui_box(12, 16);
        smd_ui_text(13, 2, game->dialogue->pages[game->page], 28, 4);
        smd_ui_text(25, 23, "X NEXT", 7, 1);
    } else if (mode == DECEPTUS_MENU_PAUSE) {
        smd_ui_box(3, 22);
        smd_ui_text(5, 13, "PAUSED", 16, 1);
        smd_ui_option(11, "RESUME", game->selection == 0);
        smd_ui_option(14, "OPTIONS", game->selection == 1);
        smd_ui_option(17, "QUIT", game->selection == 2);
    } else if (mode == DECEPTUS_MENU_INVENTORY) {
        smd_ui_box(3, 22);
        smd_ui_text(5, 11, "INVENTORY", 20, 1);
        smd_ui_text(11, 3, game->selection ? game->definition->item_names[game->selection] : "NO ITEMS", 26, 2);
        smd_ui_text(19, 5, "UP/DOWN BROWSE", 24, 1);
        smd_ui_text(22, 12, "Z BACK", 12, 1);
    } else if (mode == DECEPTUS_MENU_OPTIONS) {
        smd_ui_box(3, 22);
        smd_ui_text(5, 12, "OPTIONS", 16, 1);
        smd_ui_option(12, "CONTROLS", game->selection == 0);
        smd_ui_option(15, "BACK", game->selection == 1);
    } else if (mode == DECEPTUS_MENU_CONTROLS) {
        smd_ui_box(2, 26);
        smd_ui_text(3, 11, "CONTROLS", 20, 1);
        smd_ui_text(7, 3, "D-PAD MOVE", 26, 1);
        smd_ui_text(10, 3, "B/C JUMP  UP INTERACT", 26, 1);
        smd_ui_text(13, 3, "A SWORD  DOWN BEND", 26, 1);
        smd_ui_text(16, 3, "MODE INVENTORY", 26, 1);
        smd_ui_text(19, 3, "START PAUSE", 26, 1);
        smd_ui_text(24, 12, "Z BACK", 12, 1);
    } else if (mode == DECEPTUS_MENU_CONFIRM) {
        smd_ui_box(5, 18);
        smd_ui_text(7, 5, "RETURN TO TITLE?", 24, 1);
        smd_ui_option(13, "NO", game->selection == 0);
        smd_ui_option(16, "YES", game->selection == 1);
    } else if (mode == DECEPTUS_MENU_EXIT) {
        smd_ui_box(5, 18);
        smd_ui_text(8, 4, "CLOSE THE EMULATOR TO EXIT.", 24, 3);
        smd_ui_text(19, 10, "X/Z BACK", 18, 1);
    } else {
        if (label) {
            smd_ui_text(1, 1, smd_stages[game->campaign.room_index].title, 30, 1);
        }
        room = &game->definition->rooms[game->campaign.room_index];
        if (game->prompt < room->interaction_count) {
            smd_ui_text(24, 11, "UP INTERACT", 18, 1);
        }
    }
    smd_ui_dirty = 1;
}

static void smd_ui_commit(void)
{
    if (smd_ui_dirty) {
        DMA_doDma(DMA_VRAM, smd_ui_map, 0xb000, 2048, 2);
        VDP_setWindowHPos(FALSE, 0);
        VDP_setWindowVPos(smd_ui_window_bottom, smd_ui_window_row);
        smd_ui_dirty = 0;
    }
}
