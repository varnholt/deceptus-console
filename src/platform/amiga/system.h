#ifndef DECEPTUS_AMIGA_SYSTEM_H
#define DECEPTUS_AMIGA_SYSTEM_H
#include "deceptus/types.h"

int amigaSystemOpen(void);
void amigaSystemTakeDisplay(void (*interrupt_handler)(void), const DeceptusU16 *copper);
void amigaSystemReleaseDisplay(void);
void amigaSystemClose(void);
/* DOS access is available only while the display is released. */
DeceptusU32 amigaSystemReadFile(const char *path, DeceptusU8 *destination, DeceptusU32 capacity);
DeceptusU32 amigaSystemReadFilePair(const char *path, DeceptusU8 *first, DeceptusU32 first_capacity, DeceptusU8 *second,
                                    DeceptusU32 second_capacity);
#endif
