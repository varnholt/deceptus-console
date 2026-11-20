#include "scrollsurface.h"

static DeceptusI16 tileFloor(DeceptusI16 pixel)
{
   return pixel < 0 ? (DeceptusI16)(-((-(DeceptusI32)pixel + 15) / 16)) : pixel / 16;
}

DeceptusU8 amigaScrollSurfaceInit(AmigaScrollSurface *surface, DeceptusU16 columns, DeceptusU16 height, DeceptusU8 planes)
{
   DeceptusU32 words;
   surface->words = 0;
   words = (DeceptusU32)columns * height * planes;
   if (!columns || !height || (height & 15) || !planes || planes > 8 || words > 65535UL)
   {
      return 0;
   }
   surface->columns = columns;
   surface->height = height;
   surface->planes = planes;
   surface->stride = columns * planes;
   surface->words = (DeceptusU16)words;
   return 1;
}

DeceptusU16 amigaScrollSurfaceOffset(const AmigaScrollSurface *surface, DeceptusI16 column, DeceptusI16 y, DeceptusU8 plane)
{
   DeceptusI16 row;
   DeceptusI32 offset;
   /* the ring holds whole rows, so reducing the row first keeps both factors 16-bit (mulu.w) and replaces
      the 32-bit modulo library call with a few subtractions; this runs per tile, per bob and per ui row. */
   row = y;
   while (row < 0)
   {
      row += (DeceptusI16)surface->height;
   }
   while (row >= (DeceptusI16)surface->height)
   {
      row -= (DeceptusI16)surface->height;
   }
   offset = (DeceptusI32)((DeceptusU32)(DeceptusU16)row * (DeceptusU16)surface->stride);
   offset += column + (DeceptusI32)((DeceptusU32)(DeceptusU16)plane * (DeceptusU16)surface->columns);
   while (offset < 0)
   {
      offset += surface->words;
   }
   while (offset >= (DeceptusI32)surface->words)
   {
      offset -= surface->words;
   }
   return (DeceptusU16)offset;
}

DeceptusU16 amigaScrollSurfaceRows(const AmigaScrollSurface *surface, DeceptusU16 offset, DeceptusU16 rows)
{
   DeceptusU16 available;
   /* nearly every blit ends before the ring wraps; avoid the 32-bit division library call then. */
   if ((DeceptusU32)(DeceptusU16)rows * (DeceptusU16)surface->columns <= (DeceptusU32)(surface->words - offset))
   {
      return rows;
   }
   available = (surface->words - 1 - offset) / surface->columns + 1;
   return rows < available ? rows : available;
}

DeceptusU16 amigaScrollSurfaceWrap(const AmigaScrollSurface *surface, DeceptusU16 offset)
{
   return (DeceptusU16)(((DeceptusU32)surface->words - offset + surface->stride - 1) / surface->stride);
}

DeceptusU8 amigaScrollSurfaceVisit(const AmigaScrollSurface *surface, AmigaScrollWindow *window, DeceptusI16 camera_x, DeceptusI16 camera_y,
                                   AmigaScrollTileVisitor visitor, void *context)
{
   DeceptusI16 first_x, first_y, x, y, height, left, right;
   first_x = tileFloor(camera_x) - 1;
   first_y = tileFloor(camera_y);
   height = surface->height / 16;
   if (window->valid && first_x == window->column && first_y == window->row)
   {
      return 1;
   }
   /* only the strips the move exposed: rows outside the old window in full, and in shared rows just
      the columns on the side the window moved toward. */
   for (y = first_y; y < first_y + height; ++y)
   {
      left = first_x;
      right = first_x + (DeceptusI16)surface->columns;
      if (window->valid && y >= window->row && y < window->row + height)
      {
         if (first_x < window->column)
         {
            right = right < window->column ? right : window->column;
         }
         else
         {
            left = window->column + (DeceptusI16)surface->columns > left ? window->column + (DeceptusI16)surface->columns : left;
         }
      }
      for (x = left; x < right; ++x)
      {
         if (!visitor(context, x, y))
         {
            window->valid = 0;
            return 0;
         }
      }
   }
   window->column = first_x;
   window->row = first_y;
   window->valid = 1;
   return 1;
}
