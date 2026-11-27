/* original text and bitmap glyphs; console tile layout is backend-local. */
static DeceptusU16 smd_ui_map[2048];
static DeceptusU8 smd_ui_dirty, smd_ui_valid, smd_ui_overflow, smd_ui_font_ready;
static DeceptusU8 smd_ui_window_row, smd_ui_window_bottom;
static DeceptusU32 smd_ui_font_buffer[8];
static DeceptusU8 smd_ui_mode, smd_ui_selection, smd_ui_room, smd_ui_page, smd_ui_prompt, smd_ui_label;
static DeceptusU16 smd_ui_flags;
static const DeceptusInteraction* smd_ui_dialogue;

static DeceptusU8 smdUiColumns(DeceptusU8 character)
{
   if (character < 32 || character > 126)
   {
      character = '?';
   }
   return smd_font_glyphs[character - 32].columns;
}

static void smdUiGlyph(DeceptusU16 row, DeceptusU16 column, DeceptusU8 character)
{
   const SmdFontGlyph* glyph;
   DeceptusU16 y, x;
   if (character < 32 || character > 126)
   {
      character = '?';
   }
   glyph = &smd_font_glyphs[character - 32];
   for (y = 0; y < SMD_FONT_ROWS && row + y < 28; ++y)
   {
      for (x = 0; x < glyph->columns && column + x < 32; ++x)
      {
         smd_ui_map[(row + y) * 64 + column + x + 4] = smdGraphicsTile(SMD_FONT_FIRST + glyph->tile + y * glyph->columns + x) | 0xc000;
      }
   }
}

static void smdUiText(DeceptusU16 row, DeceptusU16 column, const char* text, DeceptusU16 width, DeceptusU8 lines)
{
   const char* word;
   DeceptusU16 origin, word_width;
   DeceptusU8 character;
   origin = column;
   word = text;
   while (*text && lines)
   {
      if (*text == '\n')
      {
         ++text;
         row += 3;
         column = origin;
         --lines;
         continue;
      }
      if (*text != ' ' && (text == word || column == origin))
      {
         word_width = 0;
         for (word = text; *word && *word != ' ' && *word != '\n'; ++word)
         {
            word_width += smdUiColumns((DeceptusU8)*word);
         }
         if (column > origin && column + word_width > origin + width)
         {
            row += 3;
            column = origin;
            --lines;
            if (!lines)
            {
               break;
            }
         }
      }
      character = (DeceptusU8)*text++;
      if (column + smdUiColumns(character) > origin + width)
      {
         row += 3;
         column = origin;
         --lines;
         if (!lines)
         {
            break;
         }
      }
      if (character == ' ' && column == origin)
      {
         word = text;
         continue;
      }
      smdUiGlyph(row, column, character);
      column += smdUiColumns(character);
      if (character == ' ')
      {
         word = text;
      }
   }
   if (*text)
   {
      smd_ui_overflow = 1;
   }
}

static void smdUiBox(DeceptusU16 row, DeceptusU16 rows)
{
   DeceptusU16 y, x;
   for (y = row; y < row + rows && y < 28; ++y)
   {
      for (x = 1; x < 31; ++x)
      {
         smd_ui_map[y * 64 + x + 4] = smdGraphicsTile(SMD_FONT_FIRST) | 0xc000;
      }
   }
}

static void smdUiOption(DeceptusU16 row, const char* text, DeceptusU8 selected)
{
   smdUiText(row, 5, selected ? ">" : " ", 1, 1);
   smdUiText(row, 7, text, 22, 1);
}

static void smdUiInit(void)
{
   DeceptusU16 index;
   if (!smd_ui_font_ready)
   {
      for (index = 0; index < SMD_FONT_TILES; ++index)
      {
         if (!smdDecodeBlock(&smd_font, index, (DeceptusU8*)smd_ui_font_buffer))
         {
            return;
         }
         VDP_loadTileData(smd_ui_font_buffer, smdGraphicsTile(SMD_FONT_FIRST + index), 1, DMA);
      }
      smd_ui_font_ready = 1;
   }
   memset(smd_ui_map, 0, sizeof(smd_ui_map));
   smd_ui_dirty = 1;
   smd_ui_valid = 0;
}

static void smdUiPrepare(const DeceptusAdventure* game)
{
   DeceptusU16 index;
   DeceptusU8 mode, label;
   const DeceptusAdventureRoom* room;
   mode = game->campaign.mode;
   label = game->room_label_ticks && mode == DECEPTUS_CAMPAIGN_PLAYING;
   if (smd_ui_valid && smd_ui_mode == mode && smd_ui_selection == game->selection && smd_ui_room == game->campaign.room_index &&
       smd_ui_page == game->page && smd_ui_prompt == game->prompt && smd_ui_label == label && smd_ui_dialogue == game->dialogue &&
       smd_ui_flags == game->flags)
   {
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
   if (mode == DECEPTUS_CAMPAIGN_PLAYING)
   {
      room = &game->definition->rooms[game->campaign.room_index];
      if (game->prompt < room->interaction_count)
      {
         smd_ui_window_row = 23;
      }
      else
      {
         smd_ui_window_bottom = 0;
         smd_ui_window_row = label ? 4 : 0;
      }
   }
   else if (mode == DECEPTUS_DIALOGUE || mode == DECEPTUS_NOTIFICATION)
   {
      smd_ui_window_row = 12;
   }
   for (index = 0; index < 2048; ++index)
   {
      smd_ui_map[index] = 0;
   }
   if (mode == DECEPTUS_CAMPAIGN_TITLE)
   {
      smdUiBox(3, 22);
      smdUiText(5, 11, "DECEPTUS", 20, 1);
      smdUiOption(11, "NEW GAME", game->selection == 0);
      smdUiOption(14, "OPTIONS", game->selection == 1);
      smdUiOption(17, "EXIT", game->selection == 2);
   }
   else if ((mode == DECEPTUS_DIALOGUE || mode == DECEPTUS_NOTIFICATION) && game->dialogue)
   {
      smdUiBox(12, 16);
      smdUiText(13, 2, game->dialogue->pages[game->page], 28, 4);
      smdUiText(25, 23, "X NEXT", 7, 1);
   }
   else if (mode == DECEPTUS_MENU_PAUSE)
   {
      smdUiBox(3, 22);
      smdUiText(5, 13, "PAUSED", 16, 1);
      smdUiOption(11, "RESUME", game->selection == 0);
      smdUiOption(14, "OPTIONS", game->selection == 1);
      smdUiOption(17, "QUIT", game->selection == 2);
   }
   else if (mode == DECEPTUS_MENU_INVENTORY)
   {
      smdUiBox(3, 22);
      smdUiText(5, 11, "INVENTORY", 20, 1);
      smdUiText(11, 3, game->selection ? game->definition->item_names[game->selection] : "NO ITEMS", 26, 2);
      smdUiText(19, 5, "UP/DOWN BROWSE", 24, 1);
      smdUiText(22, 12, "Z BACK", 12, 1);
   }
   else if (mode == DECEPTUS_MENU_OPTIONS)
   {
      smdUiBox(3, 22);
      smdUiText(5, 12, "OPTIONS", 16, 1);
      smdUiOption(12, "CONTROLS", game->selection == 0);
      smdUiOption(15, "BACK", game->selection == 1);
   }
   else if (mode == DECEPTUS_MENU_CONTROLS)
   {
      smdUiBox(2, 26);
      smdUiText(3, 11, "CONTROLS", 20, 1);
      smdUiText(7, 3, "D-PAD MOVE", 26, 1);
      smdUiText(10, 3, "B/C JUMP  UP INTERACT", 26, 1);
      smdUiText(13, 3, "A SWORD  DOWN BEND", 26, 1);
      smdUiText(16, 3, "MODE INVENTORY", 26, 1);
      smdUiText(19, 3, "START PAUSE", 26, 1);
      smdUiText(24, 12, "Z BACK", 12, 1);
   }
   else if (mode == DECEPTUS_MENU_CONFIRM)
   {
      smdUiBox(5, 18);
      smdUiText(7, 5, "RETURN TO TITLE?", 24, 1);
      smdUiOption(13, "NO", game->selection == 0);
      smdUiOption(16, "YES", game->selection == 1);
   }
   else if (mode == DECEPTUS_MENU_EXIT)
   {
      smdUiBox(5, 18);
      smdUiText(8, 4, "CLOSE THE EMULATOR TO EXIT.", 24, 3);
      smdUiText(19, 10, "X/Z BACK", 18, 1);
   }
   else
   {
      if (label)
      {
         smdUiText(1, 1, smd_stages[game->campaign.room_index].title, 30, 1);
      }
      room = &game->definition->rooms[game->campaign.room_index];
      if (game->prompt < room->interaction_count)
      {
         smdUiText(24, 11, "UP INTERACT", 18, 1);
      }
   }
   smd_ui_dirty = 1;
}

static void smdUiCommit(void)
{
   if (smd_ui_dirty)
   {
      DMA_doDma(DMA_VRAM, smd_ui_map, 0xb000, 2048, 2);
      VDP_setWindowHPos(FALSE, 0);
      VDP_setWindowVPos(smd_ui_window_bottom, smd_ui_window_row);
      smd_ui_dirty = 0;
   }
}
