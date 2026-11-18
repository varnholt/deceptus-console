#ifndef DECEPTUS_AMIGA_OPENING_UI_H
#define DECEPTUS_AMIGA_OPENING_UI_H
#include "deceptus/adventure.h"

DeceptusU8 amigaOpeningUiDraw(const DeceptusAdventure *game, DeceptusU8 *pixels, DeceptusU16 row_bytes, DeceptusU32 plane_bytes,
                              DeceptusI16 view_x, DeceptusI16 view_y, DeceptusRect *rectangles);
#endif
