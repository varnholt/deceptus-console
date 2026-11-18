#ifndef DECEPTUS_AMIGA_OPENING_VIDEO_H
#define DECEPTUS_AMIGA_OPENING_VIDEO_H
#include "deceptus/adventure.h"

DeceptusU8 amigaOpeningVideoDraw(const DeceptusAdventure *game, const DeceptusFrame *frame);
void amigaOpeningVideoSuspend(void);
void amigaOpeningVideoReset(void);
void amigaOpeningVideoClose(void);
DeceptusU32 amigaOpeningVideoFrames(void);
#endif
