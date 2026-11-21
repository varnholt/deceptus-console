#ifndef DECEPTUS_AMIGA_SCROLLSURFACE_H
#define DECEPTUS_AMIGA_SCROLLSURFACE_H

#include "deceptus/types.h"

typedef struct AmigaScrollSurface
{
   DeceptusU16 columns, height, stride, words;
   DeceptusU8 planes;
} AmigaScrollSurface;

typedef struct AmigaScrollWindow
{
   DeceptusI16 column, row;
   DeceptusU8 valid;
} AmigaScrollWindow;

typedef DeceptusU8 (*AmigaScrollTileVisitor)(void *context, DeceptusI16 column, DeceptusI16 row);
/* columns left up to right (exclusive) of one tile row. */
typedef DeceptusU8 (*AmigaScrollRunVisitor)(void *context, DeceptusI16 left, DeceptusI16 right, DeceptusI16 row);

/* the caller reserves words plus a fetch-width guard at the end of each page. */
DeceptusU8 amigaScrollSurfaceInit(AmigaScrollSurface *surface, DeceptusU16 columns, DeceptusU16 height, DeceptusU8 planes);
DeceptusU16 amigaScrollSurfaceOffset(const AmigaScrollSurface *surface, DeceptusI16 column, DeceptusI16 y, DeceptusU8 plane);
/* physical rows before the next pointer wrap; a final row may use the guard. */
DeceptusU16 amigaScrollSurfaceRows(const AmigaScrollSurface *surface, DeceptusU16 offset, DeceptusU16 rows);
/* the copper resets this plane pointer at the returned scanline. */
DeceptusU16 amigaScrollSurfaceWrap(const AmigaScrollSurface *surface, DeceptusU16 offset);
/* visits only tiles outside the previous resident window, committing on success. */
DeceptusU8 amigaScrollSurfaceVisit(const AmigaScrollSurface *surface, AmigaScrollWindow *window, DeceptusI16 camera_x, DeceptusI16 camera_y,
                                   AmigaScrollTileVisitor visitor, void *context);
/* the same tiles as amigaScrollSurfaceVisit, handed over as runs of one row. */
DeceptusU8 amigaScrollSurfaceVisitRuns(const AmigaScrollSurface *surface, AmigaScrollWindow *window, DeceptusI16 camera_x,
                                       DeceptusI16 camera_y, AmigaScrollRunVisitor visitor, void *context);

#endif
