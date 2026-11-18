#ifndef DECEPTUS_AMIGA_ROOMS_H
#define DECEPTUS_AMIGA_ROOMS_H
#include "deceptus/adventure.h"
#include "graphics.h"

const DeceptusRoom *amigaRoomLoad(void *context, DeceptusU8 room);
const AmigaGraphics *amigaRoomGraphics(void);
DeceptusU8 amigaRoomLoadGraphics(DeceptusU8 room, DeceptusU8 sword);
DeceptusU8 amigaRoomSwordGraphics(void);
DeceptusU16 amigaRoomLoads(void);
/* scratch remains valid until the next graphics load and never overlaps resident sprites. */
DeceptusU8 *amigaRoomScratch(DeceptusU32 *size);
#endif
