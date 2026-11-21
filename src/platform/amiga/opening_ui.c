#include "opening_ui.h"
#include "amiga_native_assets.h"
#include "amiga_opening_assets.h"

#include "perfcount.h"

typedef struct AmigaUiCanvas
{
   DeceptusU8 *pixels;
   DeceptusU16 stride;
   DeceptusU32 plane_stride;
   DeceptusI16 origin_x, origin_y;
   const AmigaScrollSurface *surface;
   AmigaUiClear clear;
   DeceptusU32 plane_bytes;
   /* boxes and text are optional so a backend can cache text separately; a mask canvas holds one
      plane covering screen rows [top, bottom). */
   DeceptusU8 boxes, glyphs, planes;
   DeceptusI16 top, bottom;
} AmigaUiCanvas;

static DeceptusU8 *canvasAddress(const AmigaUiCanvas *canvas, DeceptusI16 column, DeceptusI16 y, DeceptusU8 plane)
{
   if (canvas->surface)
   {
      return canvas->pixels + amigaScrollSurfaceOffset(canvas->surface, column / 2, y, plane) * 2 + (column & 1);
   }
   return canvas->pixels + (DeceptusU32)plane * canvas->plane_stride + (DeceptusI32)(y - canvas->top) * canvas->stride + column;
}

/* whether three bytes in each plane row from here stay clear of the ring end. */
static DeceptusU8 canvasLinear(const AmigaUiCanvas *canvas, const DeceptusU8 *address)
{
   if (!canvas->surface)
   {
      return 1;
   }
   return (DeceptusU32)(address - canvas->pixels) + (DeceptusU32)(canvas->planes - 1) * canvas->plane_bytes + 3 <= (DeceptusU32)canvas->surface->words * 2;
}

/* the same byte in the next bitplane; interleaved surface rows keep planes one plane-row apart. */
static DeceptusU8 *canvasPlane(const AmigaUiCanvas *canvas, DeceptusU8 *address)
{
   DeceptusU32 byte, end;
   if (!canvas->surface)
   {
      return address + canvas->plane_stride;
   }
   byte = (DeceptusU32)(address - canvas->pixels) + (DeceptusU32)canvas->surface->columns * 2;
   end = (DeceptusU32)canvas->surface->words * 2;
   return canvas->pixels + (byte >= end ? byte - end : byte);
}

/* the next byte along the same row; on a scroll surface this wraps at the ring end instead of
   recomputing a full surface offset for every byte. */
static DeceptusU8 *canvasNext(const AmigaUiCanvas *canvas, DeceptusU8 *address)
{
   DeceptusU32 byte, end;
   if (!canvas->surface)
   {
      return address + 1;
   }
   byte = (DeceptusU32)(address - canvas->pixels) + 1;
   end = (DeceptusU32)canvas->surface->words * 2;
   return canvas->pixels + (byte >= end ? byte - end : byte);
}

static void blackBox(const AmigaUiCanvas *canvas, DeceptusI16 x, DeceptusI16 y, DeceptusI16 width, DeceptusI16 height,
                     DeceptusRect *rectangle)
{
   DeceptusU8 plane, first_mask, last_mask;
   DeceptusI16 row, column, first, last;
   DeceptusU8 *address;
   rectangle->x = x + canvas->origin_x;
   rectangle->y = y + canvas->origin_y;
   rectangle->width = width;
   rectangle->height = height;
   if (!canvas->boxes)
   {
      return;
   }
   if (canvas->clear)
   {
      AMIGA_PERF(amigaPerfPhase(5);)
      canvas->clear(rectangle);
      AMIGA_PERF(amigaPerfPhase(9);)
      return;
   }
   first = rectangle->x / 8;
   last = (rectangle->x + width - 1) / 8;
   first_mask = (DeceptusU8)(255U >> (rectangle->x & 7));
   last_mask = (DeceptusU8)(255U << (7 - ((rectangle->x + width - 1) & 7)));
   for (plane = 0; plane < canvas->planes; ++plane)
   {
      for (row = 0; row < height; ++row)
      {
         address = canvasAddress(canvas, first, rectangle->y + row, plane);
         if (first == last)
         {
            *address &= (DeceptusU8) ~(first_mask & last_mask);
         }
         else
         {
            *address &= (DeceptusU8)~first_mask;
            for (column = first + 1; column < last; ++column)
            {
               address = canvasNext(canvas, address);
               *address = 0;
            }
            address = canvasNext(canvas, address);
            *address &= (DeceptusU8)~last_mask;
         }
      }
   }
}

static void text(const AmigaUiCanvas *canvas, DeceptusI16 x, DeceptusI16 y, const char *value, DeceptusI16 width, DeceptusU8 lines)
{
   DeceptusI16 cursor, row, pixel_x, visible;
   DeceptusU8 character, plane, advance, first, middle, last;
   DeceptusU32 bits;
   DeceptusU16 word;
   const DeceptusU16 *glyph;
   DeceptusU8 *destination, *line;
   if (!canvas->glyphs)
   {
      return;
   }
   AMIGA_PERF(amigaPerfPhase(5);)
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
      glyph = amiga_native_font[character - 32];
      pixel_x = cursor + canvas->origin_x;
      visible = 320 - cursor;
      for (row = 0; row < 18 && y + row < 224; ++row)
      {
         if (y + row < canvas->top || y + row >= canvas->bottom)
         {
            continue;
         }
         word = glyph[row];
         if (visible < 12)
         {
            word &= (DeceptusU16)(65535U << (16 - visible));
         }
         if (!word)
         {
            continue;
         }
         bits = ((DeceptusU32)word << 8) >> (pixel_x & 7);
         first = (DeceptusU8)(bits >> 16);
         middle = (DeceptusU8)(bits >> 8);
         last = (DeceptusU8)bits;
         line = canvasAddress(canvas, pixel_x / 8, y + row + canvas->origin_y, 0);
         if (canvasLinear(canvas, line))
         {
            /* all five plane rows lie before the ring end: plain stride, no per-byte wrap checks. */
            for (plane = 0; plane < canvas->planes; ++plane, line += canvas->plane_bytes)
            {
               line[0] |= first;
               line[1] |= middle;
               line[2] |= last;
            }
            continue;
         }
         for (plane = 0; plane < canvas->planes; ++plane, line = canvasPlane(canvas, line))
         {
            destination = line;
            if (first)
            {
               destination[0] |= first;
            }
            destination = canvasNext(canvas, destination);
            if (middle)
            {
               *destination |= middle;
            }
            if (last)
            {
               *canvasNext(canvas, destination) |= last;
            }
         }
      }
      cursor += advance;
   }
   AMIGA_PERF(amigaPerfPhase(10);)
}

static void option(const AmigaUiCanvas *canvas, DeceptusI16 y, const char *label, DeceptusU8 selected)
{
   text(canvas, 72, y, selected ? ">" : " ", 16, 1);
   text(canvas, 92, y, label, 180, 1);
}

static DeceptusU8 drawUi(AmigaUiCanvas *canvas_template, const DeceptusAdventure *game, DeceptusU8 *pixels, DeceptusU16 row_bytes,
                         DeceptusU32 plane_bytes, DeceptusI16 view_x, DeceptusI16 view_y, const AmigaScrollSurface *surface,
                         AmigaUiClear clear, DeceptusRect *rectangles)
{
   DeceptusU8 mode, count;
   AmigaUiCanvas canvas;
   canvas = *canvas_template;
   canvas.pixels = pixels;
   canvas.surface = surface;
   canvas.clear = clear;
   canvas.plane_bytes = surface ? (DeceptusU32)surface->columns * 2 : plane_bytes;
   canvas.stride = row_bytes;
   canvas.plane_stride = plane_bytes;
   canvas.origin_x = view_x;
   canvas.origin_y = view_y;
   mode = game->campaign.mode;
   count = 0;
   if (mode == DECEPTUS_DIALOGUE || mode == DECEPTUS_NOTIFICATION)
   {
      blackBox(&canvas, 8, 136, 304, 80, &rectangles[count++]);
      if (game->dialogue && game->page < game->dialogue->page_count)
      {
         text(&canvas, 16, 140, game->dialogue->pages[game->page], 288, 3);
      }
      if (mode == DECEPTUS_DIALOGUE)
      {
         text(&canvas, 16, 196, "RIGHT CTRL / RETURN", 288, 1);
      }
   }
   else if (mode == DECEPTUS_CAMPAIGN_TITLE || mode >= DECEPTUS_MENU_PAUSE)
   {
      blackBox(&canvas, 32, 32, 256, 176, &rectangles[count++]);
      if (mode == DECEPTUS_CAMPAIGN_TITLE)
      {
         text(&canvas, 104, 48, "DECEPTUS", 160, 1);
         option(&canvas, 88, "NEW GAME", game->selection == 0);
         option(&canvas, 116, "OPTIONS", game->selection == 1);
         option(&canvas, 144, "EXIT", game->selection == 2);
      }
      else if (mode == DECEPTUS_MENU_PAUSE)
      {
         text(&canvas, 112, 48, "PAUSED", 160, 1);
         option(&canvas, 88, "RESUME", game->selection == 0);
         option(&canvas, 116, "OPTIONS", game->selection == 1);
         option(&canvas, 144, "TITLE", game->selection == 2);
      }
      else if (mode == DECEPTUS_MENU_INVENTORY)
      {
         text(&canvas, 88, 48, "INVENTORY", 200, 1);
         text(&canvas, 48, 90,
              game->selection && game->selection <= game->definition->item_count ? game->definition->item_names[game->selection] : "EMPTY",
              224, 3);
         text(&canvas, 48, 174, "UP/DOWN  RETURN: BACK", 224, 1);
      }
      else if (mode == DECEPTUS_MENU_OPTIONS)
      {
         text(&canvas, 104, 48, "OPTIONS", 160, 1);
         option(&canvas, 100, "CONTROLS", game->selection == 0);
         option(&canvas, 132, "BACK", game->selection == 1);
      }
      else if (mode == DECEPTUS_MENU_CONTROLS)
      {
         text(&canvas, 48, 44, "CURSOR KEYS: MOVE", 224, 1);
         text(&canvas, 48, 68, "RIGHT CTRL: JUMP", 224, 1);
         text(&canvas, 48, 92, "UP: INTERACT", 224, 1);
         text(&canvas, 48, 116, "SPACE: SWORD", 224, 1);
         text(&canvas, 48, 140, "TAB: INVENTORY", 224, 1);
         text(&canvas, 48, 164, "ESC: PAUSE / BACK", 224, 1);
      }
      else if (mode == DECEPTUS_MENU_CONFIRM)
      {
         text(&canvas, 64, 64, "RETURN TO TITLE?", 224, 1);
         option(&canvas, 112, "NO", game->selection == 0);
         option(&canvas, 144, "YES", game->selection == 1);
      }
      else
      {
         text(&canvas, 48, 76, "HOLD BOTH MOUSE BUTTONS", 224, 2);
         text(&canvas, 48, 120, "TO EXIT.", 224, 1);
         text(&canvas, 48, 170, "RETURN: BACK", 224, 1);
      }
   }
   else
   {
      if (game->room_label_ticks)
      {
         blackBox(&canvas, 0, 0, 320, 24, &rectangles[count++]);
         text(&canvas, 8, 2, amiga_room_titles[game->campaign.room_index], 304, 1);
      }
      if (game->prompt < game->definition->rooms[game->campaign.room_index].interaction_count)
      {
         blackBox(&canvas, 88, 200, 144, 24, &rectangles[count++]);
         text(&canvas, 96, 202, "UP: INTERACT", 128, 1);
      }
   }
   return count;
}

DeceptusU8 amigaOpeningUiDraw(const DeceptusAdventure *game, DeceptusU8 *pixels, DeceptusU16 row_bytes, DeceptusU32 plane_bytes,
                              DeceptusI16 view_x, DeceptusI16 view_y, DeceptusRect *rectangles)
{
   AmigaUiCanvas canvas;
   canvas.boxes = canvas.glyphs = 1;
   canvas.planes = 5;
   canvas.top = 0;
   canvas.bottom = 224;
   return drawUi(&canvas, game, pixels, row_bytes, plane_bytes, view_x, view_y, 0, 0, rectangles);
}

DeceptusU8 amigaOpeningUiDrawSurface(const DeceptusAdventure *game, DeceptusU8 *pixels, const AmigaScrollSurface *surface,
                                     DeceptusI16 camera_x, DeceptusI16 camera_y, AmigaUiClear clear, DeceptusU8 glyphs,
                                     DeceptusRect *rectangles)
{
   AmigaUiCanvas canvas;
   canvas.boxes = 1;
   canvas.glyphs = glyphs;
   canvas.planes = surface->planes;
   canvas.top = -32767;
   canvas.bottom = 32767;
   return drawUi(&canvas, game, pixels, 0, 0, camera_x, camera_y, surface, clear, rectangles);
}

DeceptusU8 amigaOpeningUiDrawMask(const DeceptusAdventure *game, DeceptusU8 *rows, DeceptusU16 row_bytes, DeceptusI16 top,
                                  DeceptusI16 bottom, DeceptusRect *rectangles)
{
   AmigaUiCanvas canvas;
   canvas.boxes = 0;
   canvas.glyphs = 1;
   canvas.planes = 1;
   canvas.top = top;
   canvas.bottom = bottom;
   return drawUi(&canvas, game, rows, row_bytes, 0, 0, 0, 0, 0, rectangles);
}
