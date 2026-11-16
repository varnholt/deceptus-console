#ifndef DECEPTUS_AMIGA_VIDEO_H
#define DECEPTUS_AMIGA_VIDEO_H

#include "deceptus/engine.h"

int amigaVideoInit(void);
void amigaVideoClose(void);
void amigaVideoDraw(const DeceptusFrame *frame);
DeceptusU32 amigaVideoFrames(void);
void amigaVideoWait(void);

#endif
