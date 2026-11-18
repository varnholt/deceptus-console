#include "opening_ui.h"
#include "amiga_opening_assets.h"

static DeceptusU8 *target;
static DeceptusU16 stride;
static DeceptusU32 plane_stride;
static DeceptusI16 origin_x, origin_y;

static void blackBox(DeceptusI16 x, DeceptusI16 y, DeceptusI16 width, DeceptusI16 height, DeceptusRect *rectangle)
{
   DeceptusU8 plane;
   DeceptusI16 row, column;
   DeceptusU8 *destination;
   rectangle->x = x + origin_x;
   rectangle->y = y + origin_y;
   rectangle->width = width;
   rectangle->height = height;
   for (plane = 0; plane < 5; ++plane)
   {
      for (row = y; row < y + height; ++row)
      {
         for (column = x; column < x + width; ++column)
         {
            destination = target + (DeceptusU32)plane * plane_stride + (row + origin_y) * stride + (column + origin_x) / 8;
            *destination &= (DeceptusU8) ~(128U >> ((column + origin_x) & 7));
         }
      }
   }
}

static void text(DeceptusI16 x, DeceptusI16 y, const char *value, DeceptusI16 width, DeceptusU8 lines)
{
   DeceptusI16 cursor, row, column;
   DeceptusU8 character, plane, advance, mask;
   const DeceptusU16 *glyph;
   DeceptusU8 *destination;
   cursor = x;
   while (*value && lines)
   {
      character = (DeceptusU8)*value++;
      if (character == '\n')
      {
         cursor = x;
         y += 18;
         --lines;
         continue;
      }
      if (character < 32 || character > 126)
      {
         character = '?';
      }
      advance = amiga_font_advances[character - 32];
      if (cursor + advance > x + width)
      {
         cursor = x;
         y += 18;
         if (!--lines)
         {
            break;
         }
      }
      glyph = amiga_font[character - 32];
      for (row = 0; row < 18 && y + row < 224; ++row)
      {
         for (column = 0; column < 12 && cursor + column < 320; ++column)
         {
            if (!(glyph[row] & (32768U >> column)))
            {
               continue;
            }
            mask = (DeceptusU8)(128U >> ((cursor + column + origin_x) & 7));
            for (plane = 0; plane < 5; ++plane)
            {
               destination = target + (DeceptusU32)plane * plane_stride + (y + row + origin_y) * stride + (cursor + column + origin_x) / 8;
               *destination |= mask;
            }
         }
      }
      cursor += advance;
   }
}

static void option(DeceptusI16 y, const char *label, DeceptusU8 selected)
{
   text(72, y, selected ? ">" : " ", 16, 1);
   text(92, y, label, 180, 1);
}

DeceptusU8 amigaOpeningUiDraw(const DeceptusAdventure *game, DeceptusU8 *pixels, DeceptusU16 row_bytes, DeceptusU32 plane_bytes,
                              DeceptusI16 view_x, DeceptusI16 view_y, DeceptusRect *rectangles)
{
   DeceptusU8 mode, count;
   target = pixels;
   stride = row_bytes;
   plane_stride = plane_bytes;
   origin_x = view_x;
   origin_y = view_y;
   mode = game->campaign.mode;
   count = 0;
   if (mode == DECEPTUS_DIALOGUE || mode == DECEPTUS_NOTIFICATION)
   {
      blackBox(8, 136, 304, 80, &rectangles[count++]);
      if (game->dialogue && game->page < game->dialogue->page_count)
      {
         text(16, 140, game->dialogue->pages[game->page], 288, 3);
      }
      if (mode == DECEPTUS_DIALOGUE)
      {
         text(16, 196, "RIGHT CTRL / RETURN", 288, 1);
      }
   }
   else if (mode == DECEPTUS_CAMPAIGN_TITLE || mode >= DECEPTUS_MENU_PAUSE)
   {
      blackBox(32, 32, 256, 176, &rectangles[count++]);
      if (mode == DECEPTUS_CAMPAIGN_TITLE)
      {
         text(104, 48, "DECEPTUS", 160, 1);
         option(88, "NEW GAME", game->selection == 0);
         option(116, "OPTIONS", game->selection == 1);
         option(144, "EXIT", game->selection == 2);
      }
      else if (mode == DECEPTUS_MENU_PAUSE)
      {
         text(112, 48, "PAUSED", 160, 1);
         option(88, "RESUME", game->selection == 0);
         option(116, "OPTIONS", game->selection == 1);
         option(144, "TITLE", game->selection == 2);
      }
      else if (mode == DECEPTUS_MENU_INVENTORY)
      {
         text(88, 48, "INVENTORY", 200, 1);
         text(48, 90,
              game->selection && game->selection <= game->definition->item_count ? game->definition->item_names[game->selection] : "EMPTY",
              224, 3);
         text(48, 174, "UP/DOWN  RETURN: BACK", 224, 1);
      }
      else if (mode == DECEPTUS_MENU_OPTIONS)
      {
         text(104, 48, "OPTIONS", 160, 1);
         option(100, "CONTROLS", game->selection == 0);
         option(132, "BACK", game->selection == 1);
      }
      else if (mode == DECEPTUS_MENU_CONTROLS)
      {
         text(48, 44, "CURSOR KEYS: MOVE", 224, 1);
         text(48, 68, "RIGHT CTRL: JUMP", 224, 1);
         text(48, 92, "UP: INTERACT", 224, 1);
         text(48, 116, "SPACE: SWORD", 224, 1);
         text(48, 140, "TAB: INVENTORY", 224, 1);
         text(48, 164, "ESC: PAUSE / BACK", 224, 1);
      }
      else if (mode == DECEPTUS_MENU_CONFIRM)
      {
         text(64, 64, "RETURN TO TITLE?", 224, 1);
         option(112, "NO", game->selection == 0);
         option(144, "YES", game->selection == 1);
      }
      else
      {
         text(48, 76, "HOLD BOTH MOUSE BUTTONS", 224, 2);
         text(48, 120, "TO EXIT.", 224, 1);
         text(48, 170, "RETURN: BACK", 224, 1);
      }
   }
   else
   {
      if (game->room_label_ticks)
      {
         blackBox(0, 0, 320, 24, &rectangles[count++]);
         text(8, 2, amiga_room_titles[game->campaign.room_index], 304, 1);
      }
      if (game->prompt < game->definition->rooms[game->campaign.room_index].interaction_count)
      {
         blackBox(88, 200, 144, 24, &rectangles[count++]);
         text(96, 202, "UP: INTERACT", 128, 1);
      }
   }
   return count;
}
