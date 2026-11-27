#include "deceptus/itemicon.h"
/* original text and bitmap glyphs; console tile layout is backend-local. */
static DeceptusU16 snes_ui_map[1024];
static DeceptusU8 snes_ui_dirty, snes_ui_valid, snes_ui_overflow;
/* while the inventory is open the room's layers are off: its item picture shows through a gap in
 * the menu layer, which would otherwise let the room show around the picture. */
static DeceptusU8 snes_ui_room_hidden, snes_ui_room_hidden_shown;
#define SNES_UI_WIDTH 256

/* clears the menu layer's priority under the item picture, so the picture's sprite is drawn over it. */
static void snesUiItemGap(void)
{
   DeceptusU16 row, column, left;
   left = (DeceptusU16)((((SNES_UI_WIDTH - ITEM_ICON_CELL) / 2) & ~7) / 8);
   for (row = ITEM_ICON_TOP / 8; row < (ITEM_ICON_TOP + ITEM_ICON_CELL) / 8; ++row)
   {
      for (column = left; column < left + ITEM_ICON_CELL / 8; ++column)
      {
         snes_ui_map[row * 32 + column] &= (DeceptusU16)~0x2000;
      }
   }
}
static DeceptusU8 snes_ui_mode, snes_ui_selection, snes_ui_room, snes_ui_page, snes_ui_prompt, snes_ui_label;
static DeceptusU16 snes_ui_flags;
static const DeceptusInteraction *snes_ui_dialogue;

static DeceptusU8 snesUiColumns(DeceptusU8 character)
{
   if (character < 32 || character > 126)
   {
      character = '?';
   }
   return snes_font_glyphs[character - 32].columns;
}

static void snesUiGlyph(DeceptusU16 row, DeceptusU16 column, DeceptusU8 character)
{
   const SnesFontGlyph *glyph;
   DeceptusU16 y, x;
   if (character < 32 || character > 126)
   {
      character = '?';
   }
   glyph = &snes_font_glyphs[character - 32];
   for (y = 0; y < 3 && row + y < 28; ++y)
   {
      for (x = 0; x < glyph->columns && column + x < 32; ++x)
      {
         snes_ui_map[(row + y) * 32 + column + x] = snes_font_layout[glyph->layout + y * glyph->columns + x] | 0x2000;
      }
   }
}

static void snesUiText(DeceptusU16 row, DeceptusU16 column, const char *text, DeceptusU16 width, DeceptusU8 lines)
{
   const char *word;
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
            word_width += snesUiColumns((DeceptusU8)*word);
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
      if (column + snesUiColumns(character) > origin + width)
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
      snesUiGlyph(row, column, character);
      column += snesUiColumns(character);
      if (character == ' ')
      {
         word = text;
      }
   }
   if (*text)
   {
      snes_ui_overflow = 1;
   }
}

static void snesUiBox(DeceptusU16 row, DeceptusU16 rows)
{
   DeceptusU16 y, x;
   for (y = row; y < row + rows && y < 28; ++y)
   {
      for (x = 1; x < 31; ++x)
      {
         snes_ui_map[y * 32 + x] = (SNES_FONT_TILE_BASE + 1) | 0x2000;
      }
   }
}

static void snesUiOption(DeceptusU16 row, const char *text, DeceptusU8 selected)
{
   snesUiText(row, 5, selected ? ">" : " ", 1, 1);
   snesUiText(row, 7, text, 22, 1);
}

static void snesUiInit(void)
{
   DeceptusU16 index;
   for (index = 0; index < 1024; ++index)
   {
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

static void snesUiPrepare(const DeceptusAdventure *game)
{
   DeceptusU16 index;
   DeceptusU8 mode, label;
   const DeceptusAdventureRoom *room;
   mode = game->campaign.mode;
   label = game->room_label_ticks && mode == DECEPTUS_CAMPAIGN_PLAYING;
   if (snes_ui_valid && snes_ui_mode == mode && snes_ui_selection == game->selection && snes_ui_room == game->campaign.room_index &&
       snes_ui_page == game->page && snes_ui_prompt == game->prompt && snes_ui_label == label && snes_ui_dialogue == game->dialogue &&
       snes_ui_flags == game->flags)
   {
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
   snes_ui_room_hidden = 0;
   if (mode == DECEPTUS_MENU_INVENTORY)
   {
      snes_ui_room_hidden = 1;
   }
   for (index = 0; index < 1024; ++index)
   {
      snes_ui_map[index] = SNES_FONT_TILE_BASE | 0x2000;
   }
   if (mode == DECEPTUS_CAMPAIGN_TITLE)
   {
      snesUiBox(3, 22);
      snesUiText(5, 11, "DECEPTUS", 20, 1);
      snesUiOption(11, "NEW GAME", game->selection == 0);
      snesUiOption(14, "OPTIONS", game->selection == 1);
      snesUiOption(17, "EXIT", game->selection == 2);
   }
   else if ((mode == DECEPTUS_DIALOGUE || mode == DECEPTUS_NOTIFICATION) && game->dialogue)
   {
      snesUiBox(12, 16);
      snesUiText(13, 2, game->dialogue->pages[game->page], 28, 4);
      snesUiText(25, 23, "A NEXT", 7, 1);
   }
   else if (mode == DECEPTUS_MENU_PAUSE)
   {
      snesUiBox(3, 22);
      snesUiText(5, 13, "PAUSED", 16, 1);
      snesUiOption(11, "RESUME", game->selection == 0);
      snesUiOption(14, "OPTIONS", game->selection == 1);
      snesUiOption(17, "QUIT", game->selection == 2);
   }
   else if (mode == DECEPTUS_MENU_INVENTORY)
   {
      snesUiBox(3, 22);
      snesUiItemGap();
      snesUiText(5, 11, "INVENTORY", 20, 1);
      snesUiText(11, 3, game->selection ? game->definition->item_names[game->selection] : "NO ITEMS", 26, 2);
      snesUiText(19, 5, "UP/DOWN BROWSE", 24, 1);
      snesUiText(22, 12, "B BACK", 12, 1);
   }
   else if (mode == DECEPTUS_MENU_OPTIONS)
   {
      snesUiBox(3, 22);
      snesUiText(5, 12, "OPTIONS", 16, 1);
      snesUiOption(12, "CONTROLS", game->selection == 0);
      snesUiOption(15, "BACK", game->selection == 1);
   }
   else if (mode == DECEPTUS_MENU_CONTROLS)
   {
      snesUiBox(2, 26);
      snesUiText(3, 11, "CONTROLS", 20, 1);
      snesUiText(7, 3, "D-PAD MOVE", 26, 1);
      snesUiText(10, 3, "A/B JUMP  UP INTERACT", 26, 1);
      snesUiText(13, 3, "X/Y SWORD  DOWN BEND", 26, 1);
      snesUiText(16, 3, "SELECT INVENTORY", 26, 1);
      snesUiText(19, 3, "START PAUSE", 26, 1);
      snesUiText(24, 12, "B BACK", 12, 1);
   }
   else if (mode == DECEPTUS_MENU_CONFIRM)
   {
      snesUiBox(5, 18);
      snesUiText(7, 5, "RETURN TO TITLE?", 24, 1);
      snesUiOption(13, "NO", game->selection == 0);
      snesUiOption(16, "YES", game->selection == 1);
   }
   else if (mode == DECEPTUS_MENU_EXIT)
   {
      snesUiBox(5, 18);
      snesUiText(8, 4, "CLOSE THE EMULATOR TO EXIT.", 24, 3);
      snesUiText(19, 10, "A/B BACK", 18, 1);
   }
   else
   {
      if (label)
      {
         snesUiText(1, 1, snes_stages[game->campaign.room_index].title, 30, 1);
      }
      room = &game->definition->rooms[game->campaign.room_index];
      if (game->prompt < room->interaction_count)
      {
         snesUiText(24, 11, "UP INTERACT", 18, 1);
      }
   }
   snes_ui_dirty = 1;
}

static void snesUiCommit(void)
{
   if (snes_ui_dirty)
   {
      snesWaitForVblank();
      dmaCopyVram((u8 *)snes_ui_map, 0x1000, sizeof(snes_ui_map));
      if (snes_ui_room_hidden != snes_ui_room_hidden_shown)
      {
         if (snes_ui_room_hidden)
         {
            bgSetDisable(0);
            bgSetDisable(1);
         }
         else
         {
            bgSetEnable(0);
            bgSetEnable(1);
         }
         snes_ui_room_hidden_shown = snes_ui_room_hidden;
      }
      snes_ui_dirty = 0;
   }
}
