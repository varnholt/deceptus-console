#ifndef DECEPTUS_AMIGA_OPENING_UI_H
#define DECEPTUS_AMIGA_OPENING_UI_H
#include "deceptus/adventure.h"
#include "scrollsurface.h"

DeceptusU8 amigaOpeningUiDraw(const DeceptusAdventure *game, DeceptusU8 *pixels, DeceptusU16 row_bytes, DeceptusU32 plane_bytes,
                              DeceptusI16 view_x, DeceptusI16 view_y, DeceptusRect *rectangles);
/* clears a world-space rectangle to colour 0; lets a backend replace per-byte cpu clears with a blit. */
typedef void (*AmigaUiClear)(const DeceptusRect *rectangle);

/* glyphs = 0 clears the boxes only, for a backend that blits cached text from a mask. */
DeceptusU8 amigaOpeningUiDrawSurface(const DeceptusAdventure *game, DeceptusU8 *pixels, const AmigaScrollSurface *surface,
                                     DeceptusI16 camera_x, DeceptusI16 camera_y, AmigaUiClear clear, DeceptusU8 glyphs,
                                     DeceptusRect *rectangles);
/* renders only the text of screen rows [top, bottom) into one zeroed plane, row_bytes per row. */
DeceptusU8 amigaOpeningUiDrawMask(const DeceptusAdventure *game, DeceptusU8 *rows, DeceptusU16 row_bytes, DeceptusI16 top,
                                  DeceptusI16 bottom, DeceptusRect *rectangles);
#endif
