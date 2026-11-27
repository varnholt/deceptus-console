#ifndef DECEPTUS_AMIGA_OPENING_VIDEO_H
#define DECEPTUS_AMIGA_OPENING_VIDEO_H
#include "deceptus/adventure.h"

/* startup and shutdown only: display memory taken from free Chip RAM before the room banks. */
DeceptusU8 amigaOpeningVideoAllocate(void);
void amigaOpeningVideoRelease(void);
DeceptusU8 amigaOpeningVideoDraw(const DeceptusAdventure* game, const DeceptusFrame* frame);
void amigaOpeningVideoSuspend(void);
void amigaOpeningVideoReset(void);
void amigaOpeningVideoClose(void);
DeceptusU32 amigaOpeningVideoFrames(void);
DeceptusU8 amigaOpeningVideoUsesTiles(void);
/* bytes of the room's message box file the renderer shows dialogues from; 0: none. */
DeceptusU32 amigaOpeningVideoMessageBoxBytes(DeceptusU8 room);
#endif
