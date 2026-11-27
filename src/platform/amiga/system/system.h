#ifndef DECEPTUS_AMIGA_SYSTEM_H
#define DECEPTUS_AMIGA_SYSTEM_H
#include "deceptus/types.h"

int amigaSystemOpen(void);
void amigaSystemTakeDisplay(void (*interrupt_handler)(void), const DeceptusU16* copper);
void amigaSystemReleaseDisplay(void);
/* while the system owns the display, exec calls handler every vertical blank (an interrupt server),
   as the game's own interrupt does while it owns the display; at most one is added. */
void amigaSystemAddVerticalBlankServer(void (*handler)(void));
void amigaSystemRemoveVerticalBlankServer(void);
/* while the system owns the display, shows this copper list instead of the system's; amigaSystemClose
   gives the system its own back. */
void amigaSystemShowCopper(const DeceptusU16* copper);
void amigaSystemClose(void);
/* startup-only memory: large buffers come from whatever the machine has free, in separate blocks,
   so a small executable loads even when the system fragments Chip RAM. never call per frame. */
void* amigaSystemAllocate(DeceptusU32 bytes, DeceptusU8 chip);
void amigaSystemFree(void* memory, DeceptusU32 bytes);
/* the largest single free block of that memory kind. */
DeceptusU32 amigaSystemLargest(DeceptusU8 chip);
/* prints "<what>: need N bytes, largest free block M" to the launching shell; startup only. */
void amigaSystemReportMemory(const char* what, DeceptusU32 needed, DeceptusU8 chip);
/* DOS access is available only while the display is released. */
DeceptusU32 amigaSystemReadFile(const char* path, DeceptusU8* destination, DeceptusU32 capacity);
DeceptusU32 amigaSystemReadFilePair(
   const char* path,
   DeceptusU8* first,
   DeceptusU32 first_capacity,
   DeceptusU8* second,
   DeceptusU32 second_capacity
);
#endif
