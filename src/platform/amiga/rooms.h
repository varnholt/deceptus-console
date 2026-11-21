#ifndef DECEPTUS_AMIGA_ROOMS_H
#define DECEPTUS_AMIGA_ROOMS_H
#include "deceptus/adventure.h"
#include "graphics.h"
#include "tilearchive.h"

/* startup and shutdown only: the room resource banks are taken from free memory once. */
DeceptusU8 amigaRoomsAllocate(void);
void amigaRoomsRelease(void);
DeceptusU32 amigaRoomBankBytes(DeceptusU8 chip);
const DeceptusRoom *amigaRoomLoad(void *context, DeceptusU8 room);
const AmigaGraphics *amigaRoomGraphics(void);
DeceptusU8 amigaRoomLoadGraphics(DeceptusU8 room, DeceptusU8 sword);
DeceptusU8 amigaRoomSwordGraphics(void);
DeceptusU16 amigaRoomLoads(void);
const AmigaTileArchive *amigaRoomTiles(void);
const DeceptusU8 *amigaRoomTileAnimations(DeceptusU32 *size);
/* scratch remains valid until the next graphics load and never overlaps resident sprites. */
DeceptusU8 *amigaRoomScratch(DeceptusU32 *size);
DeceptusU8 *amigaRoomChipScratch(DeceptusU32 *size);
#endif
