/* original text and bitmap glyphs; console tile layout is backend-local. */
static deceptus_u16 snes_ui_map[1024];
static deceptus_u8 snes_ui_dirty, snes_ui_valid, snes_ui_overflow;
static deceptus_u8 snes_ui_mode, snes_ui_selection, snes_ui_room, snes_ui_page, snes_ui_prompt, snes_ui_label;
static deceptus_u16 snes_ui_flags;
static const deceptus_interaction *snes_ui_dialogue;

static deceptus_u8 snes_ui_columns(deceptus_u8 character)
{
    if (character < 32 || character > 126) {
        character = '?';
    }
    return snes_font_glyphs[character - 32].columns;
}

static void snes_ui_glyph(deceptus_u16 row, deceptus_u16 column, deceptus_u8 character)
{
    const snes_font_glyph *glyph;
    deceptus_u16 y, x;
    if (character < 32 || character > 126) {
        character = '?';
    }
    glyph = &snes_font_glyphs[character - 32];
    for (y = 0; y < 3 && row + y < 28; ++y) {
        for (x = 0; x < glyph->columns && column + x < 32; ++x) {
            snes_ui_map[(row + y) * 32 + column + x] = (glyph->tile + y * glyph->columns + x) | 0x2000;
        }
    }
}

static void snes_ui_text(deceptus_u16 row, deceptus_u16 column, const char *text, deceptus_u16 width, deceptus_u8 lines)
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
                word_width += snes_ui_columns((deceptus_u8)*word);
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
        if (column + snes_ui_columns(character) > origin + width) {
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
        snes_ui_glyph(row, column, character);
        column += snes_ui_columns(character);
        if (character == ' ') {
            word = text;
        }
    }
    if (*text) {
        snes_ui_overflow = 1;
    }
}

static void snes_ui_box(deceptus_u16 row, deceptus_u16 rows)
{
    deceptus_u16 y, x;
    for (y = row; y < row + rows && y < 28; ++y) {
        for (x = 1; x < 31; ++x) {
            snes_ui_map[y * 32 + x] = (SNES_FONT_TILE_BASE + 1) | 0x2000;
        }
    }
}

static void snes_ui_option(deceptus_u16 row, const char *text, deceptus_u8 selected)
{
    snes_ui_text(row, 5, selected ? ">" : " ", 1, 1);
    snes_ui_text(row, 7, text, 22, 1);
}

static void snes_ui_init(void)
{
    deceptus_u16 index;
    for (index = 0; index < 1024; ++index) {
        snes_ui_map[index] = SNES_FONT_TILE_BASE | 0x2000;
    }
    dmaCopyVram((u8 *)snes_font, 0x1400, SNES_FONT_BYTES);
    dmaCopyCGram((u8 *)snes_ui_palette, 0, sizeof(snes_ui_palette));
    bgSetGfxPtr(2, 0x1000);
    bgSetMapPtr(2, 0x1000, SC_32x32);
    bgSetScroll(2, 0, 0);
    bgSetEnable(2);
    setMode(BG_MODE1, BG3_MODE1_PRIORITY_HIGH);
    snes_ui_dirty = 1;
    snes_ui_valid = 0;
}

static void snes_ui_prepare(const deceptus_adventure *game)
{
    deceptus_u16 index;
    deceptus_u8 mode, label;
    const deceptus_adventure_room *room;
    mode = game->campaign.mode;
    label = game->room_label_ticks && mode == DECEPTUS_CAMPAIGN_PLAYING;
    if (snes_ui_valid && snes_ui_mode == mode && snes_ui_selection == game->selection &&
        snes_ui_room == game->campaign.room_index && snes_ui_page == game->page && snes_ui_prompt == game->prompt &&
        snes_ui_label == label && snes_ui_dialogue == game->dialogue && snes_ui_flags == game->flags) {
        return;
    }
    snes_ui_valid = 1;
    snes_ui_mode = mode;
    snes_ui_selection = game->selection;
    snes_ui_room = game->campaign.room_index;
    snes_ui_page = game->page;
    snes_ui_prompt = game->prompt;
    snes_ui_label = label;
    snes_ui_dialogue = game->dialogue;
    snes_ui_flags = game->flags;
    snes_ui_overflow = 0;
    for (index = 0; index < 1024; ++index) {
        snes_ui_map[index] = SNES_FONT_TILE_BASE | 0x2000;
    }
    if (mode == DECEPTUS_CAMPAIGN_TITLE) {
        snes_ui_box(3, 22);
        snes_ui_text(5, 11, "DECEPTUS", 20, 1);
        snes_ui_option(11, "NEW GAME", game->selection == 0);
        snes_ui_option(14, "OPTIONS", game->selection == 1);
        snes_ui_option(17, "EXIT", game->selection == 2);
    } else if ((mode == DECEPTUS_DIALOGUE || mode == DECEPTUS_NOTIFICATION) && game->dialogue) {
        snes_ui_box(12, 16);
        snes_ui_text(13, 2, game->dialogue->pages[game->page], 28, 4);
        snes_ui_text(25, 23, "A NEXT", 7, 1);
    } else if (mode == DECEPTUS_MENU_PAUSE) {
        snes_ui_box(3, 22);
        snes_ui_text(5, 13, "PAUSED", 16, 1);
        snes_ui_option(11, "RESUME", game->selection == 0);
        snes_ui_option(14, "OPTIONS", game->selection == 1);
        snes_ui_option(17, "QUIT", game->selection == 2);
    } else if (mode == DECEPTUS_MENU_INVENTORY) {
        snes_ui_box(3, 22);
        snes_ui_text(5, 11, "INVENTORY", 20, 1);
        snes_ui_text(11, 3, game->selection ? game->definition->item_names[game->selection] : "NO ITEMS", 26, 2);
        snes_ui_text(19, 5, "UP/DOWN BROWSE", 24, 1);
        snes_ui_text(22, 12, "B BACK", 12, 1);
    } else if (mode == DECEPTUS_MENU_OPTIONS) {
        snes_ui_box(3, 22);
        snes_ui_text(5, 12, "OPTIONS", 16, 1);
        snes_ui_option(12, "CONTROLS", game->selection == 0);
        snes_ui_option(15, "BACK", game->selection == 1);
    } else if (mode == DECEPTUS_MENU_CONTROLS) {
        snes_ui_box(2, 26);
        snes_ui_text(3, 11, "CONTROLS", 20, 1);
        snes_ui_text(7, 3, "D-PAD MOVE", 26, 1);
        snes_ui_text(10, 3, "A/B JUMP  UP INTERACT", 26, 1);
        snes_ui_text(13, 3, "X/Y SWORD  DOWN BEND", 26, 1);
        snes_ui_text(16, 3, "SELECT INVENTORY", 26, 1);
        snes_ui_text(19, 3, "START PAUSE", 26, 1);
        snes_ui_text(24, 12, "B BACK", 12, 1);
    } else if (mode == DECEPTUS_MENU_CONFIRM) {
        snes_ui_box(5, 18);
        snes_ui_text(7, 5, "RETURN TO TITLE?", 24, 1);
        snes_ui_option(13, "NO", game->selection == 0);
        snes_ui_option(16, "YES", game->selection == 1);
    } else if (mode == DECEPTUS_MENU_EXIT) {
        snes_ui_box(5, 18);
        snes_ui_text(8, 4, "CLOSE THE EMULATOR TO EXIT.", 24, 3);
        snes_ui_text(19, 10, "A/B BACK", 18, 1);
    } else {
        if (label) {
            snes_ui_text(1, 1, snes_stages[game->campaign.room_index].title, 30, 1);
        }
        room = &game->definition->rooms[game->campaign.room_index];
        if (game->prompt < room->interaction_count) {
            snes_ui_text(24, 11, "UP INTERACT", 18, 1);
        }
    }
    snes_ui_dirty = 1;
}

static void snes_ui_commit(void)
{
    if (snes_ui_dirty) {
        WaitForVBlank();
        dmaCopyVram((u8 *)snes_ui_map, 0x1000, sizeof(snes_ui_map));
        snes_ui_dirty = 0;
    }
}
