#include "system.h"
#include <exec/execbase.h>
#include <graphics/gfxbase.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <hardware/intbits.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>

struct ExecBase *SysBase;
struct GfxBase *GfxBase;
struct DosLibrary *DOSBase;
static volatile struct Custom *const hardware = (struct Custom *)0xdff000;
static APTR volatile *volatile level_three_vector = (APTR volatile *)0x6c;
static struct View *saved_view;
static APTR saved_interrupt;
static DeceptusU16 saved_dma, saved_ints;
static DeceptusU8 display_owned;

int amigaSystemOpen(void)
{
   __asm__ volatile("move.l 4.w,%0" : "=a"(SysBase));
   if (SysBase->AttnFlags & AFF_68010)
   {
      return 0;
   }
   GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR) "graphics.library", 0);
   if (!GfxBase)
   {
      return 0;
   }
   if (!(GfxBase->DisplayFlags & PAL))
   {
      CloseLibrary((struct Library *)GfxBase);
      GfxBase = 0;
      return 0;
   }
   saved_view = GfxBase->ActiView;
   return 1;
}

void amigaSystemTakeDisplay(void (*interrupt_handler)(void), const DeceptusU16 *copper)
{
   Forbid();
   saved_dma = hardware->dmaconr;
   saved_ints = hardware->intenar;
   LoadView(0);
   WaitTOF();
   WaitTOF();
   OwnBlitter();
   WaitBlit();
   Disable();
   saved_interrupt = *level_three_vector;
   hardware->intena = 0x7fff;
   hardware->dmacon = 0x7fff;
   hardware->intreq = 0x7fff;
   *level_three_vector = (APTR)interrupt_handler;
   hardware->cop1lc = (ULONG)copper;
   hardware->copjmp1 = 0;
   hardware->dmacon = DMAF_SETCLR | DMAF_MASTER | DMAF_RASTER | DMAF_COPPER | DMAF_BLITTER;
   hardware->intena = INTF_SETCLR | INTF_INTEN | INTF_VERTB;
   display_owned = 1;
}

void amigaSystemReleaseDisplay(void)
{
   if (!display_owned)
   {
      return;
   }
   WaitBlit();
   hardware->intena = 0x7fff;
   hardware->dmacon = 0x7fff;
   hardware->intreq = 0x7fff;
   *level_three_vector = saved_interrupt;
   hardware->cop1lc = (ULONG)GfxBase->copinit;
   hardware->cop2lc = (ULONG)GfxBase->LOFlist;
   hardware->copjmp1 = 0;
   hardware->dmacon = saved_dma | DMAF_SETCLR;
   hardware->intena = saved_ints | INTF_SETCLR;
   DisownBlitter();
   Enable();
   Permit();
   display_owned = 0;
}

static LONG readInto(BPTR file, DeceptusU8 *destination, DeceptusU32 capacity)
{
   LONG count;
   DeceptusU32 total;
   total = 0;
   while (total < capacity)
   {
      count = Read(file, destination + total, capacity - total);
      if (count < 0)
      {
         return -1;
      }
      if (!count)
      {
         break;
      }
      total += (DeceptusU32)count;
   }
   return (LONG)total;
}

DeceptusU32 amigaSystemReadFile(const char *path, DeceptusU8 *destination, DeceptusU32 capacity)
{
   return amigaSystemReadFilePair(path, destination, capacity, 0, 0);
}

DeceptusU32 amigaSystemReadFilePair(const char *path, DeceptusU8 *first, DeceptusU32 first_capacity, DeceptusU8 *second,
                                    DeceptusU32 second_capacity)
{
   BPTR file;
   LONG count, extra, tail;
   DeceptusU8 trailing;
   if (display_owned || !GfxBase || (first_capacity && !first) || (second_capacity && !second) || first_capacity > 2147483647UL ||
       second_capacity > 2147483647UL || (!first_capacity && !second_capacity))
   {
      return 0;
   }
   if (!DOSBase)
   {
      DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR) "dos.library", 0);
   }
   if (!DOSBase)
   {
      return 0;
   }
   file = Open((CONST_STRPTR)path, MODE_OLDFILE);
   if (!file)
   {
      return 0;
   }
   count = readInto(file, first, first_capacity);
   tail = count == (LONG)first_capacity ? readInto(file, second, second_capacity) : 0;
   extra = count >= 0 && tail >= 0 ? Read(file, &trailing, 1) : -1;
   Close(file);
   if (count < 0 || tail < 0 || extra != 0)
   {
      return 0;
   }
   return (DeceptusU32)count + (DeceptusU32)tail;
}

void amigaSystemClose(void)
{
   amigaSystemReleaseDisplay();
   if (GfxBase)
   {
      LoadView(saved_view);
      WaitTOF();
      WaitTOF();
      CloseLibrary((struct Library *)GfxBase);
      GfxBase = 0;
   }
   if (DOSBase)
   {
      CloseLibrary((struct Library *)DOSBase);
      DOSBase = 0;
   }
}
