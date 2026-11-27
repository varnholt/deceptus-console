#include "system.h"
#include <dos/dosextens.h>
#include <exec/execbase.h>
#include <graphics/gfxbase.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <hardware/intbits.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>

struct ExecBase* SysBase;
/* for the debugger: free memory after loading, for the resource budget (chip total, chip largest,
   other total, other largest); rooms.c then records the banks it allocated. */
volatile DeceptusU32 amiga_memory_debug[4];
struct GfxBase* GfxBase;
struct DosLibrary* DOSBase;
static volatile struct Custom* const hardware = (struct Custom*)0xdff000;
static APTR volatile* volatile level_three_vector = (APTR volatile*)0x6c;
static struct View* saved_view;
static APTR saved_interrupt;
static DeceptusU16 saved_dma, saved_ints;
static DeceptusU8 display_owned;
/* the folder the program was started from, as the Shell named it ("DH1:Games/" or empty when it
   was started by plain name from the current directory). */
static char program_folder[128];

static void findProgramFolder(void)
{
   struct Process* self;
   struct CommandLineInterface* cli;
   const UBYTE* name;
   DeceptusU8 length, index, end;
   program_folder[0] = 0;
   self = (struct Process*)FindTask(0);
   if (self->pr_Task.tc_Node.ln_Type != NT_PROCESS || !self->pr_CLI)
   {
      return;
   }
   cli = (struct CommandLineInterface*)BADDR(self->pr_CLI);
   name = (const UBYTE*)BADDR(cli->cli_CommandName);
   if (!name)
   {
      return;
   }
   length = name[0];
   end = 0;
   for (index = 0; index < length && index < sizeof(program_folder) - 1; ++index)
   {
      if (name[1 + index] == ':' || name[1 + index] == '/')
      {
         end = (DeceptusU8)(index + 1);
      }
   }
   for (index = 0; index < end; ++index)
   {
      program_folder[index] = (char)name[1 + index];
   }
   program_folder[end] = 0;
}

int amigaSystemOpen(void)
{
   __asm__ volatile("move.l 4.w,%0" : "=a"(SysBase));
   if (SysBase->AttnFlags & AFF_68010)
   {
      return 0;
   }
   GfxBase = (struct GfxBase*)OpenLibrary((CONST_STRPTR) "graphics.library", 0);
   if (!GfxBase)
   {
      return 0;
   }
   if (!(GfxBase->DisplayFlags & PAL))
   {
      CloseLibrary((struct Library*)GfxBase);
      GfxBase = 0;
      return 0;
   }
   saved_view = GfxBase->ActiView;
   findProgramFolder();
   amiga_memory_debug[0] = AvailMem(MEMF_CHIP);
   amiga_memory_debug[1] = AvailMem(MEMF_CHIP | MEMF_LARGEST);
   amiga_memory_debug[2] = AvailMem(MEMF_ANY) - amiga_memory_debug[0];
   amiga_memory_debug[3] = AvailMem(MEMF_FAST | MEMF_LARGEST) > AvailMem(MEMF_PUBLIC | MEMF_LARGEST) ? AvailMem(MEMF_FAST | MEMF_LARGEST)
                                                                                                     : AvailMem(MEMF_PUBLIC | MEMF_LARGEST);
   return 1;
}

void amigaSystemTakeDisplay(void (*interrupt_handler)(void), const DeceptusU16* copper)
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
   hardware->dmacon = DMAF_SETCLR | DMAF_MASTER | DMAF_RASTER | DMAF_COPPER | DMAF_BLITTER | DMAF_SPRITE;
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

static LONG readInto(BPTR file, DeceptusU8* destination, DeceptusU32 capacity)
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

DeceptusU32 amigaSystemReadFile(const char* path, DeceptusU8* destination, DeceptusU32 capacity)
{
   return amigaSystemReadFilePair(path, destination, capacity, 0, 0);
}

DeceptusU32
amigaSystemReadFilePair(const char* path, DeceptusU8* first, DeceptusU32 first_capacity, DeceptusU8* second, DeceptusU32 second_capacity)
{
   BPTR file;
   char located[192];
   LONG count, extra, tail;
   DeceptusU8 trailing;
   if (display_owned || !GfxBase || (first_capacity && !first) || (second_capacity && !second) || first_capacity > 2147483647UL ||
       second_capacity > 2147483647UL || (!first_capacity && !second_capacity))
   {
      return 0;
   }
   if (!DOSBase)
   {
      DOSBase = (struct DosLibrary*)OpenLibrary((CONST_STRPTR) "dos.library", 0);
   }
   if (!DOSBase)
   {
      return 0;
   }
   /* game data paths name the Build: volume. the program's own folder comes first, then the current
      directory, so a copy on any drive or partition runs however it is started. */
   file = 0;
   if (path[0] == 'B' && path[1] == 'u' && path[2] == 'i' && path[3] == 'l' && path[4] == 'd' && path[5] == ':')
   {
      if (program_folder[0])
      {
         char* cursor;
         const char* source;
         cursor = located;
         for (source = program_folder; *source; ++source)
         {
            *cursor++ = *source;
         }
         for (source = path + 6; *source && cursor < located + sizeof(located) - 1; ++source)
         {
            *cursor++ = *source;
         }
         *cursor = 0;
         file = Open((CONST_STRPTR)located, MODE_OLDFILE);
      }
      if (!file)
      {
         file = Open((CONST_STRPTR)(path + 6), MODE_OLDFILE);
      }
   }
   if (!file)
   {
      file = Open((CONST_STRPTR)path, MODE_OLDFILE);
   }
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

/* cpu-only data asks for non-chip memory first (slow or fast RAM), so it never takes Chip RAM the
   blitter and display need while other memory exists; machines with Chip RAM only fall back to it. */
void* amigaSystemAllocate(DeceptusU32 bytes, DeceptusU8 chip)
{
   void* memory;
   if (!bytes)
   {
      return 0;
   }
   if (chip)
   {
      return AllocMem(bytes, MEMF_CHIP | MEMF_CLEAR);
   }
   /* with non-chip memory present, a request it cannot hold fails (and is reported) rather than
      quietly taking the Chip RAM the display and blitter depend on. */
   if (AvailMem(MEMF_FAST))
   {
      return AllocMem(bytes, MEMF_FAST | MEMF_CLEAR);
   }
   memory = AllocMem(bytes, MEMF_PUBLIC | MEMF_CLEAR);
   return memory;
}

void amigaSystemFree(void* memory, DeceptusU32 bytes)
{
   if (memory && bytes)
   {
      FreeMem(memory, bytes);
   }
}

DeceptusU32 amigaSystemLargest(DeceptusU8 chip)
{
   DeceptusU32 largest;
   if (chip)
   {
      return AvailMem(MEMF_CHIP | MEMF_LARGEST);
   }
   largest = AvailMem(MEMF_FAST | MEMF_LARGEST);
   return largest ? largest : AvailMem(MEMF_PUBLIC | MEMF_LARGEST);
}

static char* appendText(char* cursor, const char* text)
{
   while (*text)
   {
      *cursor++ = *text++;
   }
   return cursor;
}

static char* appendNumber(char* cursor, DeceptusU32 value)
{
   char digits[10];
   DeceptusU8 count;
   count = 0;
   do
   {
      digits[count++] = (char)('0' + value % 10);
      value /= 10;
   } while (value);
   while (count)
   {
      *cursor++ = digits[--count];
   }
   return cursor;
}

void amigaSystemReportMemory(const char* what, DeceptusU32 needed, DeceptusU8 chip)
{
   char line[128], *cursor;
   if (!DOSBase)
   {
      DOSBase = (struct DosLibrary*)OpenLibrary((CONST_STRPTR) "dos.library", 0);
   }
   if (!DOSBase || !Output())
   {
      return;
   }
   cursor = appendText(line, what);
   cursor = appendText(cursor, ": need ");
   cursor = appendNumber(cursor, needed);
   cursor = appendText(cursor, chip ? " bytes, largest free Chip block " : " bytes, largest free block ");
   cursor = appendNumber(cursor, amigaSystemLargest(chip));
   *cursor++ = '\n';
   Write(Output(), line, cursor - line);
}

void amigaSystemClose(void)
{
   amigaSystemReleaseDisplay();
   if (GfxBase)
   {
      LoadView(saved_view);
      WaitTOF();
      WaitTOF();
      CloseLibrary((struct Library*)GfxBase);
      GfxBase = 0;
   }
   if (DOSBase)
   {
      CloseLibrary((struct Library*)DOSBase);
      DOSBase = 0;
   }
}
