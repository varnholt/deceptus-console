#include <hardware/blit.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <hardware/intbits.h>
#include <stddef.h>
#include <string.h>
#include "../audio/audio.h"
#include "../system/lz4.h"
#include "../system/perfcount.h"
#include "../system/system.h"
#include "amiga_message_box.h"
#include "amiga_native_assets.h"
#include "amiga_opening_assets.h"
#include "deceptus/appearance.h"
#include "opening_ui.h"
#include "opening_video.h"
#include "rooms.h"
#include "scrollsurface.h"

#define SURFACE_COLUMNS 23
/* the 224-line view plus one tile row of scroll margin; every line here costs both pages chip RAM. */
#define SURFACE_HEIGHT 240
#define PLANES AMIGA_NATIVE_PLANES
#define SURFACE_WORDS (SURFACE_COLUMNS * SURFACE_HEIGHT * PLANES)
#define FETCH_WORDS 21
#define TILE_ROWS (SURFACE_HEIGHT / 16)
#define TILE_CELLS (SURFACE_COLUMNS * TILE_ROWS)
#define BOB_CAPACITY 96
#define MAX_SAVED_BYTES (16UL * 1024)
#define BANK_SLOTS_MAX 200U
#define BANK_INDEX_MAX 512
#define MASK_SLOTS_MAX 32U
#define COLOR_SLOTS_FIRST 40U
#define SPRITE_ENTRIES 96
/* gameplay ui text (room title, prompt) is rendered once into one-plane bands and blitted each frame. */
#define UI_BANDS 2
#define UI_BAND_ROWS 24
#define UI_BAND_WORDS 20
#define UI_MASK_BYTES (UI_BANDS * UI_BAND_ROWS * UI_BAND_WORDS * 2)
#define SPRITE_ARENA_MAX (72UL * 1024)
#define NATIVE_SPRITE_BYTES (AMIGA_SPRITE_SCRATCH_BYTES / 6 * 10 / 5 * PLANES)
#define CHIP __attribute__((section(".MEMF_CHIP")))

typedef struct AmigaTileClip
{
   AmigaGraphicsClip clip;
   const DeceptusU8* cells;
   DeceptusI16 column, row;
   DeceptusU16 columns, rows, frame;
   DeceptusU8 enabled;
} AmigaTileClip;

typedef struct AmigaNativeBob
{
   AmigaGraphicsSprite graphic;
   DeceptusI16 x, y;
} AmigaNativeBob;

typedef struct AmigaSavedBob
{
   DeceptusU16 offset, width, rows, pixels;
   DeceptusRect bounds;
} AmigaSavedBob;

static volatile struct Custom* const hardware = (struct Custom*)0xdff000;
/* two separate Chip RAM blocks, allocated at startup; one static array would need ~110 KiB contiguous at load. */
#define PAGE_BYTES ((DeceptusU32)(SURFACE_WORDS + FETCH_WORDS) * 2)
static DeceptusU16* pages[2];
/* words, at most: 130 for the moves every list has (window, scroll, priority, sprites, modulos,
   planes, palette), 24 for four plane-wrap events (a wait and a pointer pair each), 2 for the line
   255 wait and 2 for the end: 158. a list with the box adds three waits (6) and its splits (54, 30
   and 24 moves): 272. */
static DeceptusU16 copper[2][272] CHIP;
#if AMIGA_BOX_PLANES != PLANES
#error message box region and playfield need the same plane count
#endif
/* the dialogue box: a separate bitplane region the copper shows on its rows, so the scroll
   surface, the dirty cells and the bobs never touch it; written once per page, not per frame. */
#define BOX_REGION_BYTES ((DeceptusU32)AMIGA_BOX_ROWS * AMIGA_BOX_PLANES * AMIGA_BOX_ROW_BYTES)
static DeceptusU16* box_region;
#define BOX_TEXT_MASK_BYTES ((DeceptusU32)AMIGA_BOX_TEXT_ROWS * AMIGA_BOX_TEXT_WORDS * 2)
static DeceptusU16* box_text;
/* the decoded frame template without text: a page turn copies back only its text
   rows, a template change copies all of it, both fast enough to finish before the beam reaches the
   box, so no shown frame has a half-written box. */
#define BOX_TEXT_OFFSET ((DeceptusU32)AMIGA_BOX_TEXT_TOP * AMIGA_BOX_PLANES * (AMIGA_BOX_ROW_BYTES / 2))
#define BOX_TEXT_BYTES ((DeceptusU32)AMIGA_BOX_TEXT_ROWS * AMIGA_BOX_PLANES * AMIGA_BOX_ROW_BYTES)
static DeceptusU16* box_staging;
/* the next page's text rows, composed before the vertical blank. */
static DeceptusU16* box_compose;
/* the region, the template copy, the composed rows and the text mask exist only while a box shows:
   they are lent by the end of the decoded sprite arena (Chip RAM, 58-72K in every room), so the
   boxes cost no memory at startup and none while the game plays. the world stands still during a
   dialogue, so the smaller arena re-decodes almost nothing. */
#define BOX_BLOCK_BYTES (BOX_REGION_BYTES * 2 + BOX_TEXT_BYTES + BOX_TEXT_MASK_BYTES)
static DeceptusU32 box_lent_from;
/* the open room's message box file (rooms.c reads it with the room), 0 when it has none. */
static const DeceptusU8* box_file;
static DeceptusU32 box_file_bytes;
/* per list: the value word of its COLOR01 move in the box palette, 0 when the list shows no box;
   a template change patches the shown list so the new frame never shows in the old colours. */
static DeceptusU16* box_palette_moves[2];
static DeceptusU8 box_template = 255;
static const DeceptusU16* box_palette;
/* the room's page in box_region (BOX_NO_PAGE: none) and whether the list being built shows it. */
#define BOX_NO_PAGE 0xffff
/* code that runs once per room load: small rather than fast, every byte of code comes out of the
   memory the room data needs. */
#define ROOM_CODE __attribute__((optimize("Os")))
/* box code runs once per page turn, not per frame: kept out of line and small, because every byte
   of code comes out of the memory the room data needs. */
#define BOX_CODE __attribute__((noinline, optimize("Os")))
static DeceptusU16 box_page = BOX_NO_PAGE;
static DeceptusU8 box_visible;
/* per list: its constant moves are in place for the current room (cleared at reset). */
static DeceptusU8 copper_built[2];
/* hardware sprites: three attached pairs (48 pixels, 15 colours in registers 17-31) per page.
   sprite 7 has no DMA slot with the scroll fetch, and unused channels show an empty sprite. */
#define SPRITE_PAIRS 3
#define SPRITE_LINES 96
static DeceptusU16 sprite_dma[2][SPRITE_PAIRS * 2][(SPRITE_LINES + 2) * 2] CHIP;
static DeceptusU16 null_sprite[2] CHIP;
static AmigaNativeBob hardware_bob, hardware_overlay;
static DeceptusU8 overlay_used;
static DeceptusU8 hardware_used, hardware_shown[2];
static DeceptusU16 blank_copper[6] CHIP = {0x0100, 0, 0x0180, 0, 0xffff, 0xfffe};
static const DeceptusU8 blank_quarter[AMIGA_NATIVE_TILE_BYTES] __attribute__((aligned(4)));
static AmigaScrollSurface surface;
static AmigaScrollWindow windows[2];
static AmigaTileClip clips[AMIGA_OPENING_CLIPS];
static DeceptusU16 previous_frames[2][AMIGA_OPENING_CLIPS];
static DeceptusU8 previous_enabled[2][AMIGA_OPENING_CLIPS];
static AmigaNativeBob bobs[BOB_CAPACITY];
static AmigaSavedBob saved_bobs[2][BOB_CAPACITY];
static DeceptusU16 bob_count, saved_counts[2], saved_used[2], saved_capacity;
/* the bobs requested before the player's hardware sprite: they are drawn in front of her. */
static DeceptusU16 front_bob_count;
/* the inventory's item picture, drawn after the menu so the menu does not cover it. */
static AmigaNativeBob item_bob;
static DeceptusU8 item_bob_used;
typedef struct AmigaBankCache
{
   DeceptusU16* pixels;
   DeceptusU16 count;
   DeceptusU16 banks[BANK_SLOTS_MAX], ages[BANK_SLOTS_MAX];
   /* slot + 1 for each resident bank, 0 when absent: o(1) hits for a large cache. */
   DeceptusU8 slot_of[BANK_INDEX_MAX];
} AmigaBankCache;

static DeceptusU16 *saved_pixels[2], *sprite_pixels;
/* banks the archive stores raw are read in place and never enter the decode caches. */
static const DeceptusU8 *raw_colors[BANK_INDEX_MAX], *raw_masks[BANK_INDEX_MAX];
static AmigaBankCache color_cache, mask_cache;
static DeceptusU16 bank_clock;
/* 0 when the blitter's constant registers are unknown, else 1 + the cell blit kind they hold. */
static DeceptusU8 blit_setup;
#ifdef AMIGA_PERFCOUNT
volatile DeceptusU32 amiga_phase_debug[16];
static DeceptusU32 phase_mark;

static DeceptusU32 beamStamp(void);

/* the ui module times its own parts through this one (slots 9 and 10). */
void amigaPerfPhase(DeceptusU8 phase)
{
   DeceptusU32 now;
   now = beamStamp();
   amiga_phase_debug[phase] += now - phase_mark;
   phase_mark = now;
}

static void phaseEnd(DeceptusU8 phase)
{
   DeceptusU32 now;
   now = beamStamp();
   amiga_phase_debug[phase] += now - phase_mark;
   phase_mark = now;
}
#endif
/* the debugger's view of the run: slots 23-28 count (see perfcount.h), 29 and 31 describe the room's
   caches, 30 names the site of the first failure. */
extern volatile DeceptusU32 amiga_debug[32];

/* decoded, already mirrored bob frames; the arena is flushed whole when full, which keeps
   ownership trivial while animation frames are decoded once instead of every presentation. */
typedef struct AmigaSpriteEntry
{
   DeceptusU32 offset, start, size;
   DeceptusU16* pixels;
   DeceptusU8 flip;
} AmigaSpriteEntry;

typedef struct AmigaUiBand
{
   DeceptusRect screen;
} AmigaUiBand;

static DeceptusU16* ui_mask;
static AmigaUiBand ui_bands[UI_BANDS];
static DeceptusU8 ui_band_count, ui_mask_valid;
static DeceptusU16 ui_mask_signature, ui_mask_room;

static AmigaSpriteEntry sprite_entries[SPRITE_ENTRIES];
static DeceptusU16 sprite_entry_count;
static DeceptusU32 sprite_arena_bytes, sprite_arena_used;
/* padded to whole longs and long-aligned: scans skip four unmarked cells per read. */
#define MARK_BYTES ((TILE_CELLS + 3) & ~3)
static DeceptusU8 dirty[MARK_BYTES], foreground_dirty[MARK_BYTES];
/* marked cells are also listed: painting visits and clearing resets only those, not the window. */
static DeceptusU16 dirty_list[TILE_CELLS], foreground_list[TILE_CELLS];
static DeceptusU16 dirty_count, foreground_count;

static void markDirty(DeceptusU16 index)
{
   if (!dirty[index])
   {
      dirty[index] = 1;
      dirty_list[dirty_count++] = index;
   }
}

/* foreground marks: 1 redraws foreground animations only, 2 also restores the static foreground. */
static void markForeground(DeceptusU16 index, DeceptusU8 value)
{
   if (foreground_dirty[index] < value)
   {
      if (!foreground_dirty[index])
      {
         foreground_list[foreground_count++] = index;
      }
      foreground_dirty[index] = value;
   }
}
static DeceptusRect previous_ui[2][2];
static DeceptusU16 ui_signature[2];
static DeceptusI16 ui_camera_x[2], ui_camera_y[2];
static DeceptusU8 ui_counts[2], active, loading, error, draw_buffer;
static const AmigaGraphics* graphics;
static const AmigaTileArchive* tiles;
static DeceptusPlayerAnimation animation = DECEPTUS_PLAYER_ANIMATION_INITIALIZER;
static volatile DeceptusI16 pending_buffer = -1;
static volatile DeceptusU32 video_frames;
static DeceptusU16 palette[32], last_ticks, last_flags;
/* the room palette with its thunderstorm-lit sky colours (storm_entries) at lit_level, and the level
   each copper list's colour moves show. */
static DeceptusU16 lit_palette[32];
static DeceptusU8 storm_entries[32], storm_count, lit_level, copper_level[2];
static DeceptusU8 last_mode, last_selection, last_page, last_prompt, last_label;
static const DeceptusInteraction* last_dialogue;
static DeceptusI16 camera_x, camera_y;
/* the camera's tile column and row, set once per draw. */
static DeceptusI16 camera_column, camera_row;

static DeceptusU16 readWord(const DeceptusU8* bytes)
{
   return ((DeceptusU16)bytes[0] << 8) | bytes[1];
}

static DeceptusU32 readLong(const DeceptusU8* bytes)
{
   return ((DeceptusU32)readWord(bytes) << 16) | readWord(bytes + 2);
}

static DeceptusI16 tileFloor(DeceptusI16 value)
{
   return value < 0 ? (DeceptusI16)(-((-(DeceptusI32)value + 15) / 16)) : value / 16;
}

static void waitBlitter(void)
{
   (void)hardware->dmaconr;
   while (hardware->dmaconr & DMAF_BLTDONE)
   {
   }
}

static void __attribute__((interrupt)) verticalBlank(void)
{
   if (pending_buffer >= 0)
   {
      hardware->cop1lc = (ULONG)copper[pending_buffer];
      hardware->copjmp1 = 0;
      pending_buffer = -1;
   }
   if (!loading)
   {
      ++video_frames;
   }
   audioVerticalBlank();
   hardware->intreq = INTF_VERTB;
   hardware->intreq = INTF_VERTB;
}

ROOM_CODE DeceptusU8 amigaOpeningVideoAllocate(void)
{
   pages[0] = (DeceptusU16*)amigaSystemAllocate(PAGE_BYTES, 1);
   pages[1] = (DeceptusU16*)amigaSystemAllocate(PAGE_BYTES, 1);
   if (!pages[0] || !pages[1])
   {
      amigaSystemReportMemory("Deceptus display", PAGE_BYTES, 1);
      amigaOpeningVideoRelease();
      return 0;
   }
   return 1;
}

ROOM_CODE void amigaOpeningVideoRelease(void)
{
   amigaSystemFree(pages[0], pages[0] ? PAGE_BYTES : 0);
   amigaSystemFree(pages[1], pages[1] ? PAGE_BYTES : 0);
   pages[0] = pages[1] = 0;
}

DeceptusU8 amigaOpeningVideoUsesTiles(void)
{
   return 1;
}

DeceptusU32 amigaOpeningVideoMessageBoxBytes(DeceptusU8 room)
{
   return room < AMIGA_BOX_ROOMS ? amiga_box_room_bytes[room] : 0;
}

DeceptusU32 amigaOpeningVideoFrames(void)
{
   return video_frames;
}

#ifdef AMIGA_PERFCOUNT
/* PAL beam position as a running scanline count; re-read if a vertical blank lands in between. */
static DeceptusU32 beamStamp(void)
{
   DeceptusU32 frames;
   DeceptusU16 line;
   do
   {
      frames = video_frames;
      line = (DeceptusU16)(((hardware->vposr & 1) << 8) | (hardware->vhposr >> 8));
   } while (frames != video_frames);
   return frames * 313 + line;
}
#endif

static void waitFrame(void)
{
   DeceptusU32 before;
   before = video_frames;
   while (before == video_frames)
   {
   }
}

ROOM_CODE void amigaOpeningVideoSuspend(void)
{
   if (active)
   {
      while (pending_buffer >= 0)
      {
      }
      waitBlitter();
      amigaSystemReleaseDisplay();
      /* the music plays on while a room loads. */
      amigaSystemAddVerticalBlankServer(audioVerticalBlank);
      /* while a room loads the screen stays black: no bitplane or sprite output, and the blank copper
         list instead of the system's, which would show its own colours. */
      hardware->dmacon = DMAF_RASTER | DMAF_SPRITE;
      hardware->color[0] = 0;
      amigaSystemShowCopper(blank_copper);
      active = 0;
   }
}

ROOM_CODE void amigaOpeningVideoClose(void)
{
   amigaOpeningVideoSuspend();
   hardware->dmacon = DMAF_SETCLR | DMAF_RASTER | DMAF_SPRITE;
   amigaSystemClose();
}

/* a fetched row may cross the physical end; mirror its short prefix guard. */
static void repairGuard(DeceptusU16 last_row, DeceptusU16 width)
{
   DeceptusU16 index;
   waitBlitter();
   if ((DeceptusU32)last_row + width > SURFACE_WORDS)
   {
      for (index = 0; index < last_row + width - SURFACE_WORDS; ++index)
      {
         pages[draw_buffer][index] = pages[draw_buffer][SURFACE_WORDS + index];
      }
   }
   for (index = 0; index < FETCH_WORDS; ++index)
   {
      pages[draw_buffer][SURFACE_WORDS + index] = pages[draw_buffer][index];
   }
}

/* only blits touching the ring start or crossing its end need the guard repaired; skipping the
   others lets the cpu prepare the next blit while the blitter runs. */
static void repairGuardAfter(DeceptusU16 offset, DeceptusU16 last_row, DeceptusU16 width)
{
   if (offset < FETCH_WORDS || (DeceptusU32)last_row + width > SURFACE_WORDS)
   {
      repairGuard(last_row, width);
   }
}

static DeceptusU16 ringAdvance(DeceptusU16 offset, DeceptusU16 rows)
{
   DeceptusU32 next;
   next = (DeceptusU32)offset + (DeceptusU32)rows * SURFACE_COLUMNS;
   while (next >= SURFACE_WORDS)
   {
      next -= SURFACE_WORDS;
   }
   return (DeceptusU16)next;
}

static void copyRows(DeceptusU16 offset, DeceptusU16* linear, DeceptusU16 width, DeceptusU16 rows, DeceptusU8 restore)
{
   DeceptusU16 count, last;
   while (rows)
   {
      count = amigaScrollSurfaceRows(&surface, offset, rows);
      if (count > 1023)
      {
         count = 1023;
      }
      waitBlitter();
      blit_setup = 0;
      hardware->bltcon0 = SRCA | DEST | A_TO_D;
      hardware->bltcon1 = 0;
      hardware->bltafwm = hardware->bltalwm = 65535U;
      hardware->bltamod = restore ? 0 : (SURFACE_COLUMNS - width) * 2;
      hardware->bltdmod = restore ? (SURFACE_COLUMNS - width) * 2 : 0;
      hardware->bltapt = restore ? (APTR)linear : (APTR)(pages[draw_buffer] + offset);
      hardware->bltdpt = restore ? pages[draw_buffer] + offset : linear;
      hardware->bltsize = (count << 6) | width;
      last = offset + (count - 1) * SURFACE_COLUMNS;
      if (restore)
      {
         repairGuardAfter(offset, last, width);
      }
      linear += (DeceptusU32)count * width;
      offset = ringAdvance(offset, count);
      rows -= count;
   }
   waitBlitter();
}

static void blitPixels(
   DeceptusU16 offset,
   const DeceptusU16* pixels,
   const DeceptusU16* mask,
   DeceptusU16 words,
   DeceptusU16 rows,
   DeceptusU16 shift,
   DeceptusU8 guard_word
)
{
   DeceptusU16 count, last;
   while (rows)
   {
      count = amigaScrollSurfaceRows(&surface, offset, rows);
      if (count > 1023)
      {
         count = 1023;
      }
      waitBlitter();
      blit_setup = 0;
      hardware->bltcon0 = shift | (mask ? SRCA | SRCB | SRCC | DEST | 0xca : SRCA | DEST | A_TO_D);
      hardware->bltcon1 = mask ? shift : 0;
      hardware->bltafwm = 65535U;
      hardware->bltalwm = guard_word ? 0 : 65535U;
      hardware->bltamod = hardware->bltbmod = 0;
      hardware->bltcmod = hardware->bltdmod = (SURFACE_COLUMNS - words) * 2;
      hardware->bltapt = (APTR)(mask ? mask : pixels);
      hardware->bltbpt = (APTR)pixels;
      hardware->bltcpt = pages[draw_buffer] + offset;
      hardware->bltdpt = pages[draw_buffer] + offset;
      hardware->bltsize = (count << 6) | words;
      last = offset + (count - 1) * SURFACE_COLUMNS;
      repairGuardAfter(offset, last, words);
      pixels += (DeceptusU32)count * words;
      if (mask)
      {
         mask += (DeceptusU32)count * words;
      }
      offset = ringAdvance(offset, count);
      rows -= count;
   }
}

/* returns the decoded bank's slot, decoding into the least recently used slot on a miss. neighbouring
   view tiles come from many banks, so a single decoded bank re-ran lz4 for nearly every tile. */
static DeceptusU16* bankSlot(AmigaBankCache* cache, DeceptusU8 mask_bank, DeceptusU16 bank, DeceptusU16 bank_words)
{
   DeceptusU16 slot, oldest;
   ++bank_clock;
   if (bank >= BANK_INDEX_MAX)
   {
      error = 1;
      amiga_debug[30] = 1; /* failure site */
      return 0;
   }
   slot = cache->slot_of[bank];
   if (slot)
   {
      cache->ages[slot - 1] = bank_clock;
      return cache->pixels + (DeceptusU32)(slot - 1) * bank_words;
   }
   oldest = 0;
   for (slot = 1; slot < cache->count; ++slot)
   {
      if ((DeceptusU16)(bank_clock - cache->ages[slot]) > (DeceptusU16)(bank_clock - cache->ages[oldest]))
      {
         oldest = slot;
      }
   }
   AMIGA_PERF(++amiga_debug[mask_bank ? 26 : 25];) /* bank decodes */
   if (cache->banks[oldest] < BANK_INDEX_MAX)
   {
      cache->slot_of[cache->banks[oldest]] = 0;
   }
   cache->banks[oldest] = 65535U;
   if (!amigaTileArchiveDecode(
          tiles, mask_bank, bank, (DeceptusU8*)(cache->pixels + (DeceptusU32)oldest * bank_words), (DeceptusU32)bank_words * 2
       ))
   {
      error = 1;
      amiga_debug[30] = 2; /* failure site */
      return 0;
   }
   cache->banks[oldest] = bank;
   cache->ages[oldest] = bank_clock;
   cache->slot_of[bank] = (DeceptusU8)(oldest + 1);
   return cache->pixels + (DeceptusU32)oldest * bank_words;
}

/* clears a world rectangle to colour 0 in every plane with one masked blit per ring segment,
   keeping the cpu out of large ui boxes. */
static void clearRectangle(const DeceptusRect* rectangle)
{
   DeceptusU16 offset, rows, count, words, last;
   DeceptusI16 first, right;
   if (rectangle->width <= 0 || rectangle->height <= 0 || rectangle->x < 0)
   {
      return;
   }
   first = rectangle->x >> 4;
   right = rectangle->x + rectangle->width - 1;
   words = (DeceptusU16)((right >> 4) - first + 1);
   offset = amigaScrollSurfaceOffset(&surface, first, rectangle->y, 0);
   rows = (DeceptusU16)rectangle->height * PLANES;
   while (rows)
   {
      count = amigaScrollSurfaceRows(&surface, offset, rows);
      if (count > 1023)
      {
         count = 1023;
      }
      waitBlitter();
      /* d = c and not a; a is the constant all-ones word clipped by the edge masks. */
      blit_setup = 0;
      hardware->bltcon0 = SRCC | DEST | 0x0a;
      hardware->bltcon1 = 0;
      hardware->bltafwm = (DeceptusU16)(65535U >> (rectangle->x & 15));
      hardware->bltalwm = (DeceptusU16)(65535U << (15 - (right & 15)));
      hardware->bltadat = 65535U;
      hardware->bltcmod = hardware->bltdmod = (SURFACE_COLUMNS - words) * 2;
      hardware->bltcpt = pages[draw_buffer] + offset;
      hardware->bltdpt = pages[draw_buffer] + offset;
      hardware->bltsize = (count << 6) | words;
      last = offset + (count - 1) * SURFACE_COLUMNS;
      repairGuardAfter(offset, last, words);
      offset = ringAdvance(offset, count);
      rows -= count;
   }
   /* ui text is drawn by the cpu over this box next. */
   waitBlitter();
}

/* byte pointers to a cell's four colour quarters; a failed decode paints colour 0. */
static void quarterColors(const DeceptusU16* ids, const DeceptusU8** quarters)
{
   DeceptusU8 quarter;
   const DeceptusU8* raw;
   for (quarter = 0; quarter < 4; ++quarter)
   {
      if (ids[quarter] >= tiles->colors)
      {
         quarters[quarter] = blank_quarter;
         continue;
      }
      raw = raw_colors[ids[quarter] / AMIGA_NATIVE_TILES_PER_BANK];
      if (!raw)
      {
         raw = (const DeceptusU8*)bankSlot(&color_cache, 0, ids[quarter] / AMIGA_NATIVE_TILES_PER_BANK, AMIGA_NATIVE_BANK_BYTES / 2);
      }
      quarters[quarter] = raw ? raw + (ids[quarter] % AMIGA_NATIVE_TILES_PER_BANK) * AMIGA_NATIVE_TILE_BYTES : blank_quarter;
   }
}

/* returns 0 when all four quarters use the empty mask (id 0), so the cell needs no work. */
static DeceptusU8 quarterMasks(const DeceptusU16* ids, const DeceptusU8** quarters)
{
   DeceptusU8 quarter, visible;
   const DeceptusU8* raw;
   visible = 0;
   for (quarter = 0; quarter < 4; ++quarter)
   {
      quarters[quarter] = blank_quarter;
      if (!ids[quarter])
      {
         continue;
      }
      raw = raw_masks[ids[quarter] / AMIGA_NATIVE_TILES_PER_BANK];
      if (!raw)
      {
         raw = (const DeceptusU8*)bankSlot(
            &mask_cache, 1, ids[quarter] / AMIGA_NATIVE_TILES_PER_BANK, AMIGA_NATIVE_TILES_PER_BANK * AMIGA_NATIVE_MASK_BYTES / 2
         );
      }
      if (raw)
      {
         quarters[quarter] = raw + (ids[quarter] % AMIGA_NATIVE_TILES_PER_BANK) * AMIGA_NATIVE_MASK_BYTES;
         visible = 1;
      }
   }
   return visible;
}

/* composed 16x16 cells, interleaved (16 scanlines x 5 planes, one word each), cached in Chip RAM so a
   repaint is one blit; the cpu only composes a cell the first time its tile ids appear. masked cells
   keep a mask repeated per plane ahead of the colours for a cookie-cut blit. two ways per set. */
#define CELL_WORDS (16 * PLANES)
#define PLAIN_SETS_MAX 1024U
#define MASKED_SETS_MAX 512U
/* chip bytes per set: two cells, two keys and the set's recent-way byte (two, keeping words aligned). */
#define PLAIN_SET_BYTES (CELL_WORDS * 4 + 2 * 4 * 2 + 2 + 4)
#define MASKED_SET_BYTES (CELL_WORDS * 8 + 2 * 8 * 2 + 2)
#define CELL_SETS_MIN 16U

typedef struct AmigaCellCache
{
   DeceptusU16* pixels;
   DeceptusU16* keys;
   DeceptusU8* recent;
   /* per slot, bumped whenever the slot takes a new cell (0 when not tracked). */
   DeceptusU16* generation;
   DeceptusU16 sets, slot_words;
   DeceptusU8 key_words;
} AmigaCellCache;

/* per map cell, the plain cache slot + 1 it was last painted from and that slot's generation:
   a repaint of an unchanged cell skips the archive read and the hash lookup. */
static DeceptusU16* map_slots;
static DeceptusU16 map_columns, map_rows;

/* pixels, keys and recent ways are carved from the room's Chip scratch at reset: slow RAM holds
   the room data, and animated rooms need far more masked cells than static ones. */
static AmigaCellCache plain_cells = {0, 0, 0, 0, 0, CELL_WORDS, 4};
static AmigaCellCache masked_cells = {0, 0, 0, 0, 0, CELL_WORDS * 2, 8};

static ROOM_CODE void resetCells(AmigaCellCache* cache)
{
   DeceptusU16 slot;
   for (slot = 0; slot < cache->sets * 2; ++slot)
   {
      cache->keys[(DeceptusU32)slot * cache->key_words] = 65535U;
   }
   memset(cache->recent, 0, cache->sets);
}

/* returns the cell's slot and whether it already holds the cell; a miss claims the older way. */
static DeceptusU16* cellSlot(AmigaCellCache* cache, const DeceptusU16* ids, DeceptusU8* hit, DeceptusU16* found)
{
   DeceptusU16 hash, set, slot, *key;
   DeceptusU8 index, way;
   hash = (DeceptusU16)(ids[0] ^ (ids[1] << 3) ^ (ids[2] << 6) ^ (ids[3] << 9) ^ (ids[1] >> 13) ^ (ids[2] >> 10) ^ (ids[3] >> 7));
   if (cache->key_words == 8)
   {
      hash ^= (DeceptusU16)(ids[4] ^ (ids[5] << 2) ^ (ids[6] << 4) ^ (ids[7] << 11) ^ 0x2c1b);
   }
   hash ^= (DeceptusU16)(hash << 7);
   hash ^= (DeceptusU16)(hash >> 9);
   hash ^= (DeceptusU16)(hash << 8);
   set = (DeceptusU16)(((DeceptusU32)hash * cache->sets) >> 16);
   for (way = 0; way < 2; ++way)
   {
      slot = set * 2 + way;
      key = cache->keys + (DeceptusU32)slot * cache->key_words;
      if (((const DeceptusU32*)key)[0] == ((const DeceptusU32*)ids)[0] && ((const DeceptusU32*)key)[1] == ((const DeceptusU32*)ids)[1] &&
          (cache->key_words == 4 ||
           (((const DeceptusU32*)key)[2] == ((const DeceptusU32*)ids)[2] && ((const DeceptusU32*)key)[3] == ((const DeceptusU32*)ids)[3])))
      {
         cache->recent[set] = way;
         *hit = 1;
         *found = slot;
         return cache->pixels + (DeceptusU32)slot * cache->slot_words;
      }
   }
   way = (DeceptusU8)(cache->recent[set] ^ 1);
   cache->recent[set] = way;
   slot = set * 2 + way;
   key = cache->keys + (DeceptusU32)slot * cache->key_words;
   for (index = 0; index < cache->key_words; ++index)
   {
      key[index] = ids[index];
   }
   *hit = 0;
   *found = slot;
   if (cache->generation)
   {
      ++cache->generation[slot];
   }
   AMIGA_PERF(++amiga_debug[25];) /* cell compositions */
   /* an earlier blit may still read the slot being replaced. */
   waitBlitter();
   return cache->pixels + (DeceptusU32)slot * cache->slot_words;
}

/* dictionary rows are already scanline-major with five plane bytes each, the slot's own word order. */
static void composeColors(DeceptusU16* slot, const DeceptusU8** colors)
{
   const DeceptusU8 *left, *right;
   DeceptusU8 half, count;
#if defined(__mc68000__) && (8 * PLANES) % 4 == 0
   DeceptusU16 steps;
   /* movep scatters a long's four bytes to every other address: left quarter bytes land in the even
      (high) bytes of four slot words, right quarter bytes in the odd ones. about a third of the byte
      loop's cycles; quarters at odd addresses keep the byte loop. */
   if (!(((ULONG)colors[0] | (ULONG)colors[1] | (ULONG)colors[2] | (ULONG)colors[3]) & 1))
   {
      for (half = 0; half < 4; half += 2)
      {
         left = colors[half];
         right = colors[half + 1];
         steps = 8 * PLANES / 4 - 1;
         __asm__ volatile(
            "1:\n\t"
            "move.l (%1)+,%%d0\n\t"
            "movep.l %%d0,0(%0)\n\t"
            "move.l (%2)+,%%d0\n\t"
            "movep.l %%d0,1(%0)\n\t"
            "addq.l #8,%0\n\t"
            "dbra %3,1b"
            : "+a"(slot), "+a"(left), "+a"(right), "+d"(steps)
            :
            : "d0", "memory", "cc"
         );
      }
      return;
   }
#endif
   for (half = 0; half < 4; half += 2)
   {
      left = colors[half];
      right = colors[half + 1];
      for (count = 0; count < 8 * PLANES; ++count)
      {
         *slot++ = ((DeceptusU16)*left++ << 8) | *right++;
      }
   }
}

static void composeMask(DeceptusU16* slot, const DeceptusU8** masks)
{
   DeceptusU16 bits;
   DeceptusU8 half, scanline, plane;
   for (half = 0; half < 4; half += 2)
   {
      for (scanline = 0; scanline < 8; ++scanline)
      {
         bits = ((DeceptusU16)masks[half][scanline] << 8) | masks[half + 1][scanline];
         for (plane = 0; plane < PLANES; ++plane)
         {
            *slot++ = bits;
         }
      }
   }
}

/* runs of cell blits share every register but the pointers and size: they set the constant ones
   once, and any other blit marks them stale. */
static void blitCell(DeceptusI16 column, DeceptusI16 row, const DeceptusU16* slot, DeceptusU8 masked)
{
   DeceptusU16 offset;
   DeceptusU16* destination;
   offset = amigaScrollSurfaceOffset(&surface, column, row * 16, 0);
   if ((DeceptusU32)offset + (CELL_WORDS - 1) * SURFACE_COLUMNS >= SURFACE_WORDS)
   {
      blitPixels(offset, masked ? slot + CELL_WORDS : slot, masked ? slot : 0, 1, CELL_WORDS, 0, 0);
      return;
   }
   destination = pages[draw_buffer] + offset;
   waitBlitter();
   if (blit_setup != masked + 1)
   {
      hardware->bltcon0 = masked ? SRCA | SRCB | SRCC | DEST | 0xca : SRCA | DEST | A_TO_D;
      hardware->bltcon1 = 0;
      hardware->bltafwm = hardware->bltalwm = 65535U;
      hardware->bltamod = hardware->bltbmod = 0;
      hardware->bltcmod = hardware->bltdmod = (SURFACE_COLUMNS - 1) * 2;
      blit_setup = masked + 1;
   }
   if (masked)
   {
      hardware->bltapt = (APTR)slot;
      hardware->bltbpt = (APTR)(slot + CELL_WORDS);
      hardware->bltcpt = destination;
   }
   else
   {
      hardware->bltapt = (APTR)slot;
   }
   hardware->bltdpt = destination;
   hardware->bltsize = (CELL_WORDS << 6) | 1;
   if (offset < FETCH_WORDS)
   {
      repairGuard(0, 0);
   }
}

static DeceptusU16 plainTile(DeceptusI16 column, DeceptusI16 row, const DeceptusU16* color_ids)
{
   const DeceptusU8* colors[4];
   DeceptusU16* slot;
   DeceptusU16 found;
   DeceptusU8 hit;
   slot = cellSlot(&plain_cells, color_ids, &hit, &found);
   if (!hit)
   {
      quarterColors(color_ids, colors);
      composeColors(slot, colors);
   }
   blitCell(column, row, slot, 0);
   return found;
}

static void maskedTile(DeceptusI16 column, DeceptusI16 row, const DeceptusU16* color_ids, const DeceptusU16* mask_ids)
{
   const DeceptusU8 *colors[4], *masks[4];
   DeceptusU16 ids[8], *slot, found;
   DeceptusU8 quarter, hit;
   if (!(mask_ids[0] | mask_ids[1] | mask_ids[2] | mask_ids[3]))
   {
      return;
   }
   for (quarter = 0; quarter < 4; ++quarter)
   {
      ids[quarter] = color_ids[quarter];
      ids[quarter + 4] = mask_ids[quarter];
   }
   slot = cellSlot(&masked_cells, ids, &hit, &found);
   if (!hit)
   {
      quarterMasks(mask_ids, masks);
      quarterColors(color_ids, colors);
      composeMask(slot, masks);
      composeColors(slot + CELL_WORDS, colors);
   }
   blitCell(column, row, slot, 1);
}

/* animation records hold four colour ids, then four mask ids, per cell and frame. */
#define CLIP_CELL_BYTES 16

static const DeceptusU8* clipCell(const AmigaTileClip* clip, DeceptusI16 x, DeceptusI16 y, DeceptusU16 frame)
{
   DeceptusU32 index;
   x -= clip->column;
   y -= clip->row;
   if (x < 0 || y < 0 || x >= clip->columns || y >= clip->rows)
   {
      return 0;
   }
   /* frames x rows x columns of one clip stay below 65536 cells (checked at reset): two
      16x16 multiplies instead of 32-bit library calls on every painted cell. */
   index = (DeceptusU32)(DeceptusU16)((DeceptusU16)frame * clip->rows + (DeceptusU16)y) * clip->columns + (DeceptusU16)x;
   return clip->cells + index * CLIP_CELL_BYTES;
}

static void clipTile(DeceptusI16 x, DeceptusI16 y, const DeceptusU8* cell)
{
   DeceptusU16 colors[4], masks[4];
   DeceptusU8 quarter;
   for (quarter = 0; quarter < 4; ++quarter)
   {
      colors[quarter] = readWord(cell + quarter * 2);
      masks[quarter] = readWord(cell + 8 + quarter * 2);
   }
   maskedTile(x, y, colors, masks);
}

static DeceptusU8 sameClipCell(const DeceptusU8* before, const DeceptusU8* after)
{
   const DeceptusU32 *first, *second;
   first = (const DeceptusU32*)before;
   second = (const DeceptusU32*)after;
   return first[0] == second[0] && first[1] == second[1] && first[2] == second[2] && first[3] == second[3];
}

/* per clip plane (0 background, 1 foreground), the map cell rectangle all its clips can touch,
   right and bottom exclusive (empty when left == right): a painted cell outside it needs no clip
   lookups, and outside the foreground one no foreground mark either. */
static DeceptusI16 cover_left[2], cover_top[2], cover_right[2], cover_bottom[2];

static DeceptusU8 clipCovered(DeceptusU8 plane, DeceptusI16 x, DeceptusI16 y)
{
   return x >= cover_left[plane] && x < cover_right[plane] && y >= cover_top[plane] && y < cover_bottom[plane];
}

static DeceptusU8 paintClips(DeceptusI16 x, DeceptusI16 y);

static DeceptusU8 paintTile(void* context, DeceptusI16 x, DeceptusI16 y)
{
   DeceptusU16 color_ids[4], mask_ids[4], index, *entry;
   (void)context;
   AMIGA_PERF(++amiga_debug[23];)
   entry = map_slots && x >= 0 && y >= 0 && x < (DeceptusI16)map_columns && y < (DeceptusI16)map_rows
              ? map_slots + ((DeceptusU32)(DeceptusU16)y * map_columns + (DeceptusU16)x) * 2
              : 0;
   if (entry && entry[0] && plain_cells.generation[entry[0] - 1] == entry[1])
   {
      blitCell(x, y, plain_cells.pixels + (DeceptusU32)(DeceptusU16)(entry[0] - 1) * (DeceptusU16)CELL_WORDS, 0);
   }
   else
   {
      if (x < 0 || y < 0 || !amigaTileArchiveCell(tiles, (DeceptusU16)x, (DeceptusU16)y, color_ids, mask_ids))
      {
         /* dictionary tile 0 is never cached as blank; colour 0 comes from an empty mask cell instead. */
         color_ids[0] = color_ids[1] = color_ids[2] = color_ids[3] = 65534U;
      }
      index = plainTile(x, y, color_ids);
      if (entry)
      {
         entry[0] = (DeceptusU16)(index + 1);
         entry[1] = plain_cells.generation[index];
      }
   }
   return paintClips(x, y);
}

/* the animated layers over a freshly painted cell: background clip tiles now, and a foreground
   mark when a foreground clip may cover it. */
static DeceptusU8 paintClips(DeceptusI16 x, DeceptusI16 y)
{
   DeceptusU16 index;
   const DeceptusU8* cell;
   for (index = 0; clipCovered(0, x, y) && index < graphics->clip_count; ++index)
   {
      if (clips[index].enabled && !clips[index].clip.plane)
      {
         cell = clipCell(&clips[index], x, y, clips[index].frame);
         if (cell)
         {
            clipTile(x, y, cell);
         }
      }
   }
   /* the painted tile holds the static foreground already; only foreground animations repaint. */
   if (!clipCovered(1, x, y))
   {
      return !error;
   }
   x -= camera_column - 1;
   y -= camera_row;
   if (x >= 0 && x < SURFACE_COLUMNS && y >= 0 && y < TILE_ROWS)
   {
      markForeground((DeceptusU16)(y * SURFACE_COLUMNS + x), 1);
   }
   return !error;
}

/* cells left up to right (exclusive) of map row y. a cell the map memo still holds is one blit
   whose constant registers stay set for the whole run, with the ring offset stepped instead of
   recomputed, and clip work only inside a clip area; anything else goes through paintTile. */
static DeceptusU8 paintRun(void* context, DeceptusI16 left, DeceptusI16 right, DeceptusI16 y)
{
   DeceptusU16 offset, slot, *entry, *page;
   const DeceptusU16* generation;
   const DeceptusU16* pixels;
   DeceptusI16 x;
   DeceptusU8 covered;
   if (!map_slots || left < 0 || y < 0 || right > (DeceptusI16)map_columns || y >= (DeceptusI16)map_rows)
   {
      for (x = left; x < right; ++x)
      {
         if (!paintTile(context, x, y))
         {
            return 0;
         }
      }
      return 1;
   }
   covered = (y >= cover_top[0] && y < cover_bottom[0] && left < cover_right[0] && right > cover_left[0]) ||
             (y >= cover_top[1] && y < cover_bottom[1] && left < cover_right[1] && right > cover_left[1]);
   AMIGA_PERF(amiga_debug[23] += (DeceptusU16)(right - left);)
   offset = amigaScrollSurfaceOffset(&surface, left, y * 16, 0);
   entry = map_slots + ((DeceptusU32)(DeceptusU16)y * map_columns + (DeceptusU16)left) * 2;
   page = pages[draw_buffer];
   generation = plain_cells.generation;
   pixels = plain_cells.pixels;
   for (x = left; x < right; ++x, entry += 2)
   {
      slot = entry[0];
      if (!slot || generation[slot - 1] != entry[1] || offset < FETCH_WORDS || offset >= SURFACE_WORDS - (CELL_WORDS - 1) * SURFACE_COLUMNS)
      {
         AMIGA_PERF(--amiga_debug[23];) /* paintTile counts itself */
         if (!paintTile(context, x, y))
         {
            return 0;
         }
      }
      else
      {
         waitBlitter();
         if (blit_setup != 1)
         {
            hardware->bltcon0 = SRCA | DEST | A_TO_D;
            hardware->bltcon1 = 0;
            hardware->bltafwm = hardware->bltalwm = 65535U;
            hardware->bltamod = hardware->bltbmod = 0;
            hardware->bltcmod = hardware->bltdmod = (SURFACE_COLUMNS - 1) * 2;
            blit_setup = 1;
         }
         hardware->bltapt = (APTR)(pixels + (DeceptusU32)(DeceptusU16)(slot - 1) * (DeceptusU16)CELL_WORDS);
         hardware->bltdpt = page + offset;
         hardware->bltsize = (CELL_WORDS << 6) | 1;
         if (covered && (clipCovered(0, x, y) || clipCovered(1, x, y)) && !paintClips(x, y))
         {
            return 0;
         }
      }
      if (++offset == SURFACE_WORDS)
      {
         offset = 0;
      }
   }
   return 1;
}

/* while the previous page still waits for its vertical blank, composes the cells the coming
   scroll paint will need, so that paint finds them in the map memo; stops at the swap. */
static DeceptusU8 warmTile(void* context, DeceptusI16 x, DeceptusI16 y)
{
   DeceptusU16 color_ids[4], mask_ids[4], *entry, *slot, found;
   const DeceptusU8* colors[4];
   DeceptusU8 hit;
   (void)context;
   if (pending_buffer < 0)
   {
      return 0;
   }
   if (!map_slots || x < 0 || y < 0 || x >= (DeceptusI16)map_columns || y >= (DeceptusI16)map_rows)
   {
      return 1;
   }
   entry = map_slots + ((DeceptusU32)(DeceptusU16)y * map_columns + (DeceptusU16)x) * 2;
   if ((entry[0] && plain_cells.generation[entry[0] - 1] == entry[1]) ||
       !amigaTileArchiveCell(tiles, (DeceptusU16)x, (DeceptusU16)y, color_ids, mask_ids))
   {
      return 1;
   }
   slot = cellSlot(&plain_cells, color_ids, &hit, &found);
   if (!hit)
   {
      quarterColors(color_ids, colors);
      composeColors(slot, colors);
   }
   entry[0] = (DeceptusU16)(found + 1);
   entry[1] = plain_cells.generation[found];
   return !error;
}

/* foreground marks: 1 redraws foreground animations only (painted tiles already hold the static
   composite), 2 also restores the static foreground over a bob. */
static void markRectangle(const DeceptusRect* rectangle, DeceptusU8 foreground, DeceptusU8 value)
{
   DeceptusI16 x, y, left, top, right, bottom;
   DeceptusU16 index;
   AmigaScrollWindow resident;
   AmigaScrollWindow* window;
   resident.column = tileFloor(camera_x) - 1;
   resident.row = tileFloor(camera_y);
   window = &resident;
   left = tileFloor(rectangle->x);
   top = tileFloor(rectangle->y);
   right = tileFloor(rectangle->x + rectangle->width - 1);
   bottom = tileFloor(rectangle->y + rectangle->height - 1);
   if (left < window->column)
   {
      left = window->column;
   }
   if (top < window->row)
   {
      top = window->row;
   }
   if (right >= window->column + SURFACE_COLUMNS)
   {
      right = window->column + SURFACE_COLUMNS - 1;
   }
   if (bottom >= window->row + TILE_ROWS)
   {
      bottom = window->row + TILE_ROWS - 1;
   }
   for (y = top; y <= bottom; ++y)
   {
      for (x = left; x <= right; ++x)
      {
         index = (DeceptusU16)((y - window->row) * SURFACE_COLUMNS + x - window->column);
         if (foreground)
         {
            markForeground(index, value);
         }
         else
         {
            markDirty(index);
         }
      }
   }
}

static void markStrip(DeceptusI16 x, DeceptusI16 y, DeceptusI16 right, DeceptusI16 bottom)
{
   DeceptusRect strip;
   if (x < right && y < bottom)
   {
      strip.x = x;
      strip.y = y;
      strip.width = right - x;
      strip.height = bottom - y;
      markRectangle(&strip, 0, 1);
   }
}

/* repaint only the pixels an opaque box covered before and no longer covers (before minus after);
   a box that stayed put needs no tiles, even over partly covered cells. */
static void markUncovered(const DeceptusRect* before, const DeceptusRect* after)
{
   DeceptusI16 right, bottom, after_right, after_bottom, top, low;
   right = before->x + before->width;
   bottom = before->y + before->height;
   after_right = after->x + after->width;
   after_bottom = after->y + after->height;
   markStrip(before->x, before->y, right, after->y < bottom ? after->y : bottom);
   markStrip(before->x, after_bottom > before->y ? after_bottom : before->y, right, bottom);
   top = before->y > after->y ? before->y : after->y;
   low = bottom < after_bottom ? bottom : after_bottom;
   markStrip(before->x, top, after->x < right ? after->x : right, low);
   markStrip(after_right > before->x ? after_right : before->x, top, right, low);
}

/* value / divisor with the remainder, from two 32/16 divu steps instead of the library's bit loop. */
static DeceptusU32 divideLong(DeceptusU32 value, DeceptusU16 divisor, DeceptusU16* remainder)
{
#if defined(__mc68000__)
   DeceptusU32 high, low;
   high = value >> 16;
   __asm__("divu.w %1,%0" : "+d"(high) : "d"(divisor) : "cc");
   low = (high & 0xffff0000UL) | (value & 0xffffUL);
   __asm__("divu.w %1,%0" : "+d"(low) : "d"(divisor) : "cc");
   *remainder = (DeceptusU16)(low >> 16);
   return (high << 16) | (low & 0xffffUL);
#else
   *remainder = (DeceptusU16)(value % divisor);
   return value / divisor;
#endif
}

static void updateClips(const DeceptusAdventure* game)
{
   DeceptusU16 index, frame, duration, flags;
   DeceptusU32 total, phase;
   const DeceptusU8* bytes;
   AmigaTileClip* state;
   flags = deceptusAdventurePresentationFlags(game);
   cover_left[0] = cover_left[1] = cover_right[0] = cover_right[1] = 0;
   for (index = 0; index < graphics->clip_count; ++index)
   {
      DeceptusU8 plane;
      state = &clips[index];
      plane = state->clip.plane ? 1 : 0;
      if (cover_left[plane] == cover_right[plane])
      {
         cover_left[plane] = state->column;
         cover_top[plane] = state->row;
         cover_right[plane] = (DeceptusI16)(state->column + (DeceptusI16)state->columns);
         cover_bottom[plane] = (DeceptusI16)(state->row + (DeceptusI16)state->rows);
      }
      else
      {
         cover_left[plane] = state->column < cover_left[plane] ? state->column : cover_left[plane];
         cover_top[plane] = state->row < cover_top[plane] ? state->row : cover_top[plane];
         cover_right[plane] = (DeceptusI16)(state->column + (DeceptusI16)state->columns) > cover_right[plane]
                                 ? (DeceptusI16)(state->column + (DeceptusI16)state->columns)
                                 : cover_right[plane];
         cover_bottom[plane] = (DeceptusI16)(state->row + (DeceptusI16)state->rows) > cover_bottom[plane]
                                  ? (DeceptusI16)(state->row + (DeceptusI16)state->rows)
                                  : cover_bottom[plane];
      }
      state->enabled = (flags & state->clip.required_flags) == state->clip.required_flags && !(flags & state->clip.forbidden_flags);
      if (!state->clip.durations)
      {
         (void)divideLong(divideLong(game->campaign.engine.ticks, state->clip.frame_ticks, &duration), state->clip.frames, &frame);
         state->frame = frame;
         continue;
      }
      bytes = graphics->world + state->clip.durations;
      total = 0;
      for (frame = 0; frame < state->clip.frames; ++frame)
      {
         duration = readWord(bytes + frame * 2);
         if (!duration)
         {
            error = 1;
            amiga_debug[30] = 3; /* failure site */
            return;
         }
         total += duration;
      }
      if (total <= 65535UL)
      {
         (void)divideLong(game->campaign.engine.ticks, (DeceptusU16)total, &duration);
         phase = duration;
      }
      else
      {
         phase = game->campaign.engine.ticks % total;
      }
      state->frame = 0;
      for (frame = 0; frame < state->clip.frames; ++frame)
      {
         duration = readWord(bytes + frame * 2);
         if (phase < duration)
         {
            state->frame = frame;
            break;
         }
         phase -= duration;
      }
   }
}

/* animated cells repainted per draw at most (the first clip in the rotation always advances): a
   long animation cycle misses every cache, and unbounded it made frames spike past 1000 lines. */
#define ANIMATION_CELL_BUDGET 32
static DeceptusU16 animation_cells[TILE_CELLS];
static DeceptusU8 animation_rotation;

static void markAnimatedCells(void)
{
   DeceptusU16 index, turn, count, used, cell;
   DeceptusI16 x, y, left, top, right, bottom;
   const DeceptusU8 *before, *after;
   AmigaTileClip* clip;
   AmigaScrollWindow* window;
   window = &windows[draw_buffer];
   used = 0;
   if (graphics->clip_count)
   {
      animation_rotation = (DeceptusU8)((animation_rotation + 1) % graphics->clip_count);
   }
   for (turn = 0; turn < graphics->clip_count; ++turn)
   {
      index = (DeceptusU16)((animation_rotation + turn) % graphics->clip_count);
      clip = &clips[index];
      if (previous_enabled[draw_buffer][index] == clip->enabled && previous_frames[draw_buffer][index] == clip->frame)
      {
         continue;
      }
      /* only cells the clip covers can change; scanning the whole window per clip cost ~10% of a frame. */
      left = clip->column > window->column ? clip->column : window->column;
      top = clip->row > window->row ? clip->row : window->row;
      right = clip->column + (DeceptusI16)clip->columns < window->column + SURFACE_COLUMNS ? clip->column + (DeceptusI16)clip->columns
                                                                                           : window->column + SURFACE_COLUMNS;
      bottom =
         clip->row + (DeceptusI16)clip->rows < window->row + TILE_ROWS ? clip->row + (DeceptusI16)clip->rows : window->row + TILE_ROWS;
      count = 0;
      for (y = top; y < bottom; ++y)
      {
         /* records of one clip row are consecutive: step through both frames' rows by pointer. */
         before = previous_enabled[draw_buffer][index] && left < right ? clipCell(clip, left, y, previous_frames[draw_buffer][index]) : 0;
         after = clip->enabled && left < right ? clipCell(clip, left, y, clip->frame) : 0;
         cell = (DeceptusU16)((y - window->row) * SURFACE_COLUMNS + left - window->column);
         for (x = left; x < right; ++x, ++cell)
         {
            if ((before || after) && (!before || !after || !sameClipCell(before, after)))
            {
               animation_cells[count++] = cell;
            }
            if (before)
            {
               before += CLIP_CELL_BYTES;
            }
            if (after)
            {
               after += CLIP_CELL_BYTES;
            }
         }
      }
      if (turn && used + count > ANIMATION_CELL_BUDGET)
      {
         /* this page keeps showing the clip's previous frame; a later draw advances it. */
         clip->frame = previous_frames[draw_buffer][index];
         clip->enabled = previous_enabled[draw_buffer][index];
         continue;
      }
      used += count;
      while (count)
      {
         markDirty(animation_cells[--count]);
      }
      previous_frames[draw_buffer][index] = clip->frame;
      previous_enabled[draw_buffer][index] = clip->enabled;
   }
}

/* the room's widest reach of any sprite frame from its anchor: an anchor outside the screen by more
   than this cannot show any frame, so requestBob skips the metadata read for it. */
static DeceptusI16 reach_left, reach_right, reach_top, reach_bottom;

static ROOM_CODE void measureReach(void)
{
   AmigaGraphicsSprite sprite;
   DeceptusU16 resource;
   DeceptusI16 left, right;
   reach_left = reach_top = 32767;
   reach_right = reach_bottom = -32767;
   for (resource = 0; resource < AMIGA_GRAPHICS_RESOURCES; ++resource)
   {
      if (!amigaGraphicsSpriteInfo(graphics, resource, 0, &sprite))
      {
         continue;
      }
      left = sprite.trim_x < sprite.flip_trim_x ? sprite.trim_x : sprite.flip_trim_x;
      right = (DeceptusI16)((sprite.trim_x > sprite.flip_trim_x ? sprite.trim_x : sprite.flip_trim_x) + (DeceptusI16)sprite.width);
      reach_left = left < reach_left ? left : reach_left;
      reach_right = right > reach_right ? right : reach_right;
      reach_top = sprite.trim_y < reach_top ? sprite.trim_y : reach_top;
      reach_bottom = (DeceptusI16)(sprite.trim_y + (DeceptusI16)sprite.height) > reach_bottom
                        ? (DeceptusI16)(sprite.trim_y + (DeceptusI16)sprite.height)
                        : reach_bottom;
   }
}

static DeceptusU8 requestBob(void* context, const DeceptusSpriteAppearance* appearance)
{
   AmigaNativeBob* bob;
   DeceptusU16 resource;
   (void)context;
   if (appearance->family >= DECEPTUS_SPRITE_FAMILY_COUNT)
   {
      return 0;
   }
   resource = amiga_sprite_families[appearance->family] + appearance->frame;
   if (resource >= AMIGA_GRAPHICS_RESOURCES || bob_count == BOB_CAPACITY)
   {
      return 0;
   }
   /* every frame of an anchor this far out is culled below (or by prepareSprites for the player);
      the worn item keeps the full path, it can widen the player's composed sprite. */
   if (appearance->family != DECEPTUS_SPRITE_HEADTORCH && (appearance->x + reach_left >= 320 || appearance->x + reach_right <= 0 ||
                                                           appearance->y + reach_top >= 224 || appearance->y + reach_bottom <= 0))
   {
      return 1;
   }
   bob = &bobs[bob_count];
   if (!amigaGraphicsSpriteInfo(graphics, resource, appearance->flip_x, &bob->graphic))
   {
      return 0;
   }
   if (bob->graphic.hardware)
   {
      /* the player is one hardware sprite object in screen coordinates (no save, blit or re-mask);
         a worn item (the headtorch) is an overlay drawn into the player's sprite, in front of her. */
      AmigaNativeBob* target;
      target =
         appearance->family == DECEPTUS_SPRITE_HEADTORCH ? (overlay_used ? 0 : &hardware_overlay) : (hardware_used ? 0 : &hardware_bob);
      if (target)
      {
         target->graphic = bob->graphic;
         target->x = appearance->x + (appearance->flip_x ? bob->graphic.flip_trim_x : bob->graphic.trim_x);
         target->y = appearance->y + bob->graphic.trim_y;
         if (target == &hardware_overlay)
         {
            overlay_used = 1;
         }
         else
         {
            hardware_used = 1;
            front_bob_count = bob_count;
         }
         return 1;
      }
   }
   bob->x = appearance->x + (appearance->flip_x ? bob->graphic.flip_trim_x : bob->graphic.trim_x);
   bob->y = appearance->y + bob->graphic.trim_y;
   if (bob->x >= 320 || bob->y >= 224 || bob->x + bob->graphic.width <= 0 || bob->y + bob->graphic.height <= 0)
   {
      return 1;
   }
   bob->x += camera_x;
   bob->y += camera_y;
   if (appearance->family == SPRITE_ITEM_ICON)
   {
      item_bob = *bob;
      item_bob_used = 1;
      return 1;
   }
   ++bob_count;
   AMIGA_PERF(++amiga_debug[28];)
   return 1;
}

static DeceptusU8 shiftedByte(const DeceptusU8* row, DeceptusI16 column, DeceptusU8 shift)
{
   DeceptusU16 value;
   value = column < 0 ? 0 : (DeceptusU16)row[column] << 8;
   value |= row[column + 1];
   return (DeceptusU8)((DeceptusU16)(value << shift) >> 8);
}

static void clippedBob(const AmigaNativeBob* bob, const DeceptusU16* sprite, const DeceptusRect* bounds)
{
   const DeceptusU8 *pixels, *mask, *source;
   DeceptusU8* destination;
   DeceptusI16 x, y, plane, first, last, source_first, source_x, bit_x;
   DeceptusU32 row, plane_bytes;
   DeceptusU8 alpha, shift;
   pixels = (const DeceptusU8*)sprite;
   plane_bytes = (DeceptusU32)bob->graphic.row_bytes * bob->graphic.height * PLANES;
   mask = pixels + plane_bytes;
   first = bounds->x / 8;
   last = (bounds->x + bounds->width - 1) / 8;
   bit_x = first * 8 - bob->x;
   shift = (DeceptusU8)(bit_x < 0 ? bit_x + 8 : bit_x & 7);
   source_first = bit_x < 0 ? -1 : bit_x / 8;
   for (y = bounds->y; y < bounds->y + bounds->height; ++y)
   {
      for (plane = 0; plane < PLANES; ++plane)
      {
         row = ((DeceptusU32)(y - bob->y) * PLANES + plane) * bob->graphic.row_bytes;
         source = pixels + row;
         source_x = source_first;
         for (x = first; x <= last; ++x, ++source_x)
         {
            alpha = shiftedByte(mask + row, source_x, shift);
            if (x == first)
            {
               alpha &= (DeceptusU8)(255U >> (bounds->x & 7));
            }
            if (x == last)
            {
               alpha &= (DeceptusU8)(255U << (7 - ((bounds->x + bounds->width - 1) & 7)));
            }
            if (!alpha)
            {
               continue;
            }
            destination = (DeceptusU8*)pages[draw_buffer] + amigaScrollSurfaceOffset(&surface, x / 2, y, (DeceptusU8)plane) * 2 + (x & 1);
            *destination = (*destination & (DeceptusU8)~alpha) | (shiftedByte(source, source_x, shift) & alpha);
         }
      }
   }
   repairGuard(0, 0);
}

static const DeceptusU16* bobPixels(const AmigaGraphicsSprite* graphic)
{
   DeceptusU16 index;
   DeceptusU32 size;
   DeceptusU16* pixels;
   for (index = 0; index < sprite_entry_count; ++index)
   {
      if (sprite_entries[index].offset == graphic->offset && sprite_entries[index].flip == graphic->flip)
      {
         return sprite_entries[index].pixels;
      }
   }
   size = ((DeceptusU32)graphic->row_bytes * graphic->height * PLANES * 2 + 3) & ~3UL;
   if (size > NATIVE_SPRITE_BYTES)
   {
      return 0;
   }
   /* the arena is a ring: new frames go after the newest one and evict only the oldest frames they
      overlap, so a room whose frames exceed the arena keeps its recent ones instead of all being
      flushed together. */
   if (sprite_arena_used + size > sprite_arena_bytes)
   {
      sprite_arena_used = 0;
   }
   /* earlier blits may still read a frame about to be overwritten. */
   waitBlitter();
   for (index = 0; index < sprite_entry_count;)
   {
      if (sprite_entries[index].start < sprite_arena_used + size &&
          sprite_arena_used < sprite_entries[index].start + sprite_entries[index].size)
      {
         sprite_entries[index] = sprite_entries[--sprite_entry_count];
      }
      else
      {
         ++index;
      }
   }
   if (sprite_entry_count == SPRITE_ENTRIES)
   {
      /* every entry is small; drop one so the table never overflows. */
      sprite_entries[0] = sprite_entries[--sprite_entry_count];
   }
   pixels = (DeceptusU16*)((DeceptusU8*)sprite_pixels + sprite_arena_used);
   AMIGA_PERF(++amiga_debug[27];)
   if (!amigaGraphicsSpriteDecode(graphics, graphic, (DeceptusU8*)pixels, size))
   {
      return 0;
   }
   if (graphic->flip)
   {
      amigaGraphicsSpriteFlip((DeceptusU8*)pixels, graphic);
   }
   AMIGA_PERF(amiga_debug[24] += size;)
   sprite_entries[sprite_entry_count].offset = graphic->offset;
   sprite_entries[sprite_entry_count].flip = graphic->flip;
   sprite_entries[sprite_entry_count].pixels = pixels;
   sprite_entries[sprite_entry_count].start = sprite_arena_used;
   sprite_entries[sprite_entry_count].size = size;
   ++sprite_entry_count;
   sprite_arena_used += size;
   return pixels;
}

static void drawBob(const AmigaNativeBob* bob)
{
   DeceptusRect bounds;
   AmigaSavedBob* saved;
   DeceptusI16 right, bottom, first;
   DeceptusU16 rows, words;
   DeceptusU32 needed, plane_words, skip;
   const DeceptusU16* sprite;
   bounds.x = bob->x;
   bounds.y = bob->y;
   right = bob->x + bob->graphic.width;
   bottom = bob->y + bob->graphic.height;
   if (bounds.x < windows[draw_buffer].column * 16)
   {
      bounds.x = windows[draw_buffer].column * 16;
   }
   if (bounds.y < windows[draw_buffer].row * 16)
   {
      bounds.y = windows[draw_buffer].row * 16;
   }
   if (right > (windows[draw_buffer].column + SURFACE_COLUMNS) * 16)
   {
      right = (windows[draw_buffer].column + SURFACE_COLUMNS) * 16;
   }
   if (bottom > (windows[draw_buffer].row + TILE_ROWS) * 16)
   {
      bottom = (windows[draw_buffer].row + TILE_ROWS) * 16;
   }
   if (bounds.x >= right || bounds.y >= bottom)
   {
      return;
   }
   bounds.width = right - bounds.x;
   bounds.height = bottom - bounds.y;
   first = tileFloor(bounds.x);
   words = tileFloor(right - 1) - first + 1;
   rows = bounds.height * PLANES;
   needed = (DeceptusU32)words * rows;
   if (needed > (DeceptusU32)(saved_capacity - saved_used[draw_buffer]) || saved_counts[draw_buffer] == BOB_CAPACITY)
   {
      error = 1;
      amiga_debug[30] = 4; /* failure site */
      return;
   }
   saved = &saved_bobs[draw_buffer][saved_counts[draw_buffer]++];
   saved->bounds = bounds;
   saved->offset = amigaScrollSurfaceOffset(&surface, first, bounds.y, 0);
   saved->width = words;
   saved->rows = rows;
   saved->pixels = saved_used[draw_buffer];
   copyRows(saved->offset, saved_pixels[draw_buffer] + saved->pixels, words, rows, 0);
   saved_used[draw_buffer] += (DeceptusU16)needed;
   sprite = bobPixels(&bob->graphic);
   if (!sprite)
   {
      error = 1;
      amiga_debug[30] = 5; /* failure site */
      return;
   }
   if (bounds.x != bob->x || bounds.width != bob->graphic.width)
   {
      /* horizontal clips (rare: the surface keeps a column of margin) stay on the cpu. */
      clippedBob(bob, sprite, &bounds);
   }
   else
   {
      /* vertical clips skip whole interleaved lines of pixels and mask: still one blit. */
      plane_words = (DeceptusU32)bob->graphic.row_bytes * bob->graphic.height * PLANES / 2;
      skip = (DeceptusU32)(bounds.y - bob->y) * PLANES * (bob->graphic.row_bytes / 2);
      blitPixels(
         amigaScrollSurfaceOffset(&surface, tileFloor(bob->x), bounds.y, 0),
         sprite + skip,
         sprite + plane_words + skip,
         bob->graphic.row_bytes / 2,
         bounds.height * PLANES,
         (bob->x & 15) << 12,
         1
      );
   }
   markRectangle(&bounds, 1, 2);
}

/* hardware sprite frames are decoded once into channel words (per channel: two words per line) in a
   small ring cache; showing a cached frame is a copy of its visible lines plus position words. */
#define SPRITE_COMPACT_BYTES ((SPRITE_PAIRS * 2) * SPRITE_LINES * (PLANES + 1))
#define SPRITE_CACHE_WORDS (16UL * 1024)
#define SPRITE_CACHE_ENTRIES 64
typedef struct AmigaSpriteFrame
{
   DeceptusU32 offset;
   DeceptusU16 start, words;
   DeceptusU8 flip;
} AmigaSpriteFrame;
static DeceptusU16* sprite_cache;
static AmigaSpriteFrame sprite_frames[SPRITE_CACHE_ENTRIES];
static DeceptusU16 sprite_frame_count, sprite_cache_used;
static DeceptusU8 sprite_compact[SPRITE_COMPACT_BYTES];
static DeceptusU8 sprite_mirror[256];

static ROOM_CODE void spriteMirrorTable(void)
{
   DeceptusU16 value;
   DeceptusU8 bit, result;
   for (value = 0; value < 256; ++value)
   {
      result = 0;
      for (bit = 0; bit < 8; ++bit)
      {
         result = (DeceptusU8)((result << 1) | ((value >> bit) & 1));
      }
      sprite_mirror[value] = result;
   }
   sprite_frame_count = sprite_cache_used = 0;
}

/* one line's plane row as pixels, mirrored when the frame faces left. only frames whose rows the
   word copies in spriteFrame cannot take come here, once per frame into the cache: kept small. */
static BOX_CODE void spriteRow(const DeceptusU8* source, DeceptusU8* row, DeceptusU8 bytes, DeceptusU8 flip, DeceptusU8 shift)
{
   DeceptusU8 index, skip, current, next;
   if (!flip)
   {
      for (index = 0; index < bytes; ++index)
      {
         row[index] = source[index];
      }
      return;
   }
   for (index = 0; index < bytes; ++index)
   {
      row[index] = sprite_mirror[source[bytes - 1 - index]];
   }
   /* the row's padding, at its right end, is at the left after the mirror: move the pixels left by
      it, whole bytes first. a frame narrower than its row by 8 or more pixels (the 22 and 23 pixel
      run frames) would otherwise come out empty, and the player would vanish facing left. */
   skip = (DeceptusU8)(shift >> 3);
   shift &= 7;
   for (index = 0; index < bytes; ++index)
   {
      current = index + skip < bytes ? row[index + skip] : 0;
      next = index + skip + 1 < bytes ? row[index + skip + 1] : 0;
      row[index] = shift ? (DeceptusU8)((current << shift) | (next >> (8 - shift))) : current;
   }
}

/* the cached channel words of a frame, decoding it into the ring on a miss. */
static const DeceptusU16* spriteFrame(const AmigaGraphicsSprite* graphic, DeceptusU16 pairs)
{
   const DeceptusU8* line_bytes;
   DeceptusU16* output;
   DeceptusU8 row[PLANES][SPRITE_PAIRS * 2];
   DeceptusU16 index, words, line, channel, stored, start;
   DeceptusU8 plane, shift;
   for (index = 0; index < sprite_frame_count; ++index)
   {
      if (sprite_frames[index].offset == graphic->offset && sprite_frames[index].flip == graphic->flip)
      {
         return sprite_cache + sprite_frames[index].start;
      }
   }
   words = (DeceptusU16)(pairs * 4 * graphic->height);
   if (words > SPRITE_CACHE_WORDS)
   {
      return 0;
   }
   start = sprite_cache_used + words > SPRITE_CACHE_WORDS ? 0 : sprite_cache_used;
   /* evict the frames the new one overwrites (oldest first, as the ring wraps). */
   for (index = 0; index < sprite_frame_count;)
   {
      if (sprite_frames[index].start < start + words && start < sprite_frames[index].start + sprite_frames[index].words)
      {
         sprite_frames[index] = sprite_frames[--sprite_frame_count];
      }
      else
      {
         ++index;
      }
   }
   if (sprite_frame_count == SPRITE_CACHE_ENTRIES)
   {
      sprite_frames[0] = sprite_frames[--sprite_frame_count];
   }
   stored = graphic->row_bytes - 2;
   AMIGA_PERF(++amiga_debug[27];) /* hardware frame decodes share the bob decode counter */
   if (!amigaGraphicsSpriteCompact(graphics, graphic, sprite_compact, sizeof(sprite_compact)))
   {
      return 0;
   }
   shift = (DeceptusU8)(stored * 8 - graphic->width);
   /* attached pairs: the even channel shows planes 0-1, the odd one planes 2-3 of each 16 pixels. */
   line_bytes = sprite_compact;
   if (stored == pairs * 2 && (!graphic->flip || shift < 8) && !((ULONG)sprite_compact & 1))
   {
      /* each channel word straight from the compact rows: unflipped it is the plane row's word as
         stored, flipped it is two mirrored bytes from the row's far end, shifted left by the
         sub-byte remainder with the following byte's bits (zero past the row end). */
      DeceptusU16 stride, *channel_output;
      const DeceptusU8 *ends[PLANES], *end;
      DeceptusU8 next;
      stride = (DeceptusU16)(graphic->height * 2);
      for (line = 0; line < graphic->height; ++line, line_bytes += stored * PLANES)
      {
         output = sprite_cache + start + line * 2;
         if (!graphic->flip)
         {
            const DeceptusU16* words = (const DeceptusU16*)line_bytes;
            for (channel = 0; channel < pairs; ++channel, ++words)
            {
               channel_output = output + (DeceptusU32)channel * 2 * stride;
               channel_output[0] = words[0];
               channel_output[1] = words[pairs];
               channel_output[stride] = words[pairs * 2];
               channel_output[stride + 1] = words[pairs * 3];
            }
            continue;
         }
         for (plane = 0; plane < PLANES; ++plane)
         {
            ends[plane] = line_bytes + (plane + 1) * stored;
         }
         for (channel = 0; channel < pairs * 2; ++channel, output += stride)
         {
            plane = (DeceptusU8)((channel & 1) * 2);
            for (index = 0; index < 2; ++index)
            {
               end = ends[plane + index] - (channel >> 1) * 2;
               next = (channel >> 1) * 2 + 2 < stored ? sprite_mirror[end[-3]] : 0;
               output[index] =
                  shift
                     ? (DeceptusU16)((((DeceptusU16)sprite_mirror[end[-1]] << 8 | sprite_mirror[end[-2]]) << shift) | (next >> (8 - shift)))
                     : (DeceptusU16)((DeceptusU16)sprite_mirror[end[-1]] << 8 | sprite_mirror[end[-2]]);
            }
         }
      }
   }
   else
   {
      for (line = 0; line < graphic->height; ++line, line_bytes += stored * PLANES)
      {
         for (plane = 0; plane < PLANES; ++plane)
         {
            spriteRow(line_bytes + plane * stored, row[plane], (DeceptusU8)stored, graphic->flip, shift);
         }
         for (channel = 0; channel < pairs * 2; ++channel)
         {
            output = sprite_cache + start + (DeceptusU32)channel * 2 * graphic->height + line * 2;
            plane = (DeceptusU8)((channel & 1) * 2);
            output[0] = ((DeceptusU16)row[plane][(channel >> 1) * 2] << 8) | row[plane][(channel >> 1) * 2 + 1];
            output[1] = ((DeceptusU16)row[plane + 1][(channel >> 1) * 2] << 8) | row[plane + 1][(channel >> 1) * 2 + 1];
         }
      }
   }
   sprite_frames[sprite_frame_count].offset = graphic->offset;
   sprite_frames[sprite_frame_count].flip = graphic->flip;
   sprite_frames[sprite_frame_count].start = start;
   sprite_frames[sprite_frame_count].words = words;
   ++sprite_frame_count;
   sprite_cache_used = start + words;
   return sprite_cache + start;
}

static void spriteBlit(const DeceptusU16* source, DeceptusU16* destination, DeceptusU16 lines)
{
   waitBlitter();
   blit_setup = 0;
   hardware->bltcon0 = source ? SRCA | DEST | A_TO_D : DEST;
   hardware->bltcon1 = 0;
   hardware->bltafwm = hardware->bltalwm = 65535U;
   hardware->bltamod = hardware->bltdmod = 0;
   hardware->bltadat = 0;
   if (source)
   {
      hardware->bltapt = (APTR)source;
   }
   hardware->bltdpt = destination;
   hardware->bltsize = (DeceptusU16)((lines << 6) | 2);
}

/* draws the overlay's opaque pixels over the composed player lines of this page. each overlay pair
   is two cached channels (planes 0-1, then 2-3) of height lines, two words per line; its 16 pixels
   straddle at most two sprite columns. pointers step by addition: no multiply per word. */
static void spriteOverlay(
   const AmigaNativeBob* overlay,
   const DeceptusU16* frame,
   DeceptusI16 left,
   DeceptusI16 top,
   DeceptusU16 first,
   DeceptusU16 lines,
   DeceptusU16 pairs
)
{
   DeceptusU16 overlay_pairs, height, pair, mask, bits, target, high_target, w0, w1, w2, w3;
   DeceptusI16 x, start, stop, line;
   DeceptusU8 shift, back, low, high;
   const DeceptusU16 *even, *odd;
   DeceptusU16 *low0, *low1, *high0, *high1;
   height = overlay->graphic.height;
   overlay_pairs = (overlay->graphic.width + 15) / 16;
   /* overlay line l lands on composed row l - start; only rows 0 .. lines-1 exist. */
   start = (DeceptusI16)(top + (DeceptusI16)first - overlay->y);
   stop = (DeceptusI16)(start + (DeceptusI16)lines);
   if (start < 0)
   {
      start = 0;
   }
   if (stop > (DeceptusI16)height)
   {
      stop = (DeceptusI16)height;
   }
   if (start >= stop)
   {
      return;
   }
   even = frame;
   for (pair = 0; pair < overlay_pairs; ++pair, even += height * 4)
   {
      x = (DeceptusI16)(overlay->x - left + (DeceptusI16)pair * 16);
      if (x <= -16)
      {
         continue;
      }
      /* a column up to 15 pixels left of the sprite still reaches its first column. */
      shift = (DeceptusU8)(x & 15);
      back = (DeceptusU8)(16 - shift);
      if (x < 0)
      {
         target = 0;
         high_target = 0;
         low = 0;
         high = (DeceptusU8)((shift != 0) && (pairs > 0));
      }
      else
      {
         target = (DeceptusU16)(x >> 4);
         high_target = (DeceptusU16)(target + 1);
         low = target < pairs;
         high = shift && high_target < pairs;
      }
      if (!low && !high)
      {
         continue;
      }
      odd = even + height * 2;
      /* composed row r of a channel is at +2 + 2r; overlay line l is row l - (top + first - overlay->y). */
      line = (DeceptusI16)(overlay->y - top - (DeceptusI16)first + start);
      low0 = low1 = high0 = high1 = sprite_dma[draw_buffer][0]; /* unused unless the side is drawn */
      if (low)
      {
         low0 = sprite_dma[draw_buffer][target * 2] + 2 + line * 2;
         low1 = sprite_dma[draw_buffer][target * 2 + 1] + 2 + line * 2;
      }
      if (high)
      {
         high0 = sprite_dma[draw_buffer][high_target * 2] + 2 + line * 2;
         high1 = sprite_dma[draw_buffer][high_target * 2 + 1] + 2 + line * 2;
      }
      for (line = start; line < stop; ++line)
      {
         w0 = even[line * 2];
         w1 = even[line * 2 + 1];
         w2 = odd[line * 2];
         w3 = odd[line * 2 + 1];
         mask = w0 | w1 | w2 | w3;
         if (mask)
         {
            /* every plane word is inside the mask, so the shifted word needs no masking. */
            if (low)
            {
               bits = (DeceptusU16) ~(mask >> shift);
               low0[0] = (DeceptusU16)((low0[0] & bits) | (w0 >> shift));
               low0[1] = (DeceptusU16)((low0[1] & bits) | (w1 >> shift));
               low1[0] = (DeceptusU16)((low1[0] & bits) | (w2 >> shift));
               low1[1] = (DeceptusU16)((low1[1] & bits) | (w3 >> shift));
            }
            if (high)
            {
               bits = (DeceptusU16) ~(mask << back);
               high0[0] = (DeceptusU16)((high0[0] & bits) | (DeceptusU16)(w0 << back));
               high0[1] = (DeceptusU16)((high0[1] & bits) | (DeceptusU16)(w1 << back));
               high1[0] = (DeceptusU16)((high1[0] & bits) | (DeceptusU16)(w2 << back));
               high1[1] = (DeceptusU16)((high1[1] & bits) | (DeceptusU16)(w3 << back));
            }
         }
         low0 += 2;
         low1 += 2;
         high0 += 2;
         high1 += 2;
      }
   }
}

/* the static foreground mask of the 16x16 map cell at column, row, one word per scanline (set bits
   cover what is behind them); returns 0 when the cell has no foreground. */
static DeceptusU8 foregroundCellMask(DeceptusI16 column, DeceptusI16 row, DeceptusU16* scanlines)
{
   DeceptusU16 color_ids[4], mask_ids[4];
   const DeceptusU8* masks[4];
   DeceptusU8 half, scanline;
   if ((column < 0) || (row < 0) || !amigaTileArchiveCell(tiles, (DeceptusU16)column, (DeceptusU16)row, color_ids, mask_ids) ||
       !quarterMasks(mask_ids, masks))
   {
      return 0;
   }
   for (half = 0; half < 4; half += 2)
   {
      for (scanline = 0; scanline < 8; ++scanline)
      {
         *scanlines++ = (DeceptusU16)(((DeceptusU16)masks[half][scanline] << 8) | masks[half + 1][scanline]);
      }
   }
   return 1;
}

/* clears the pixels under mask in one line of a pair of the player's sprite channels. */
static void clearSpritePixels(DeceptusU16 line, DeceptusU16 pair, DeceptusU16 mask)
{
   DeceptusU16 *low, *high;
   low = sprite_dma[draw_buffer][pair * 2] + 2 + line * 2;
   high = sprite_dma[draw_buffer][pair * 2 + 1] + 2 + line * 2;
   low[0] &= (DeceptusU16)~mask;
   low[1] &= (DeceptusU16)~mask;
   high[0] &= (DeceptusU16)~mask;
   high[1] &= (DeceptusU16)~mask;
}

/* the player's hardware sprite is drawn in front of the whole playfield, so foreground tiles cannot
   cover her by priority: clear her pixels wherever the static foreground is set instead. lines of
   the sprite start at screen row y; its pairs of channels cover 16 columns each from screen column x. */
static void hideSpriteBehindForeground(DeceptusI16 x, DeceptusI16 y, DeceptusU16 lines, DeceptusU16 pairs)
{
   DeceptusU16 cells[SPRITE_PAIRS + 1][16];
   DeceptusU16 line, band, step, pair, mask, shift;
   DeceptusI16 world_x, world_y, column, row;
   DeceptusU8 scan, covered, waited;
   waited = 0;
   world_x = (DeceptusI16)(camera_x + x);
   world_y = (DeceptusI16)(camera_y + y);
   if ((world_x < 0) || (world_y < 0))
   {
      return;
   }
   column = (DeceptusI16)(world_x >> 4);
   shift = (DeceptusU16)(world_x & 15);
   for (line = 0; line < lines; line += band)
   {
      row = (DeceptusI16)((world_y + line) >> 4);
      scan = (DeceptusU8)((world_y + line) & 15);
      band = (DeceptusU16)(16 - scan);
      if (band > lines - line)
      {
         band = lines - line;
      }
      covered = 0;
      for (pair = 0; pair <= pairs; ++pair)
      {
         if (foregroundCellMask((DeceptusI16)(column + pair), row, cells[pair]))
         {
            covered = 1;
         }
         else
         {
            memset(cells[pair], 0, sizeof(cells[pair]));
         }
      }
      if (!covered)
      {
         continue;
      }
      if (!waited)
      {
         /* the cpu changes lines the blitter may still be copying. */
         waitBlitter();
         waited = 1;
      }
      for (step = 0; step < band; ++step)
      {
         for (pair = 0; pair < pairs; ++pair)
         {
            mask = (DeceptusU16)(cells[pair][scan + step] << shift);
            if (shift)
            {
               mask |= (DeceptusU16)(cells[pair + 1][scan + step] >> (16 - shift));
            }
            if (mask)
            {
               clearSpritePixels((DeceptusU16)(line + step), pair, mask);
            }
         }
      }
   }
}

/* 16 mask bits of a bob's row from column on; columns outside the row are clear. */
static DeceptusU16 bobMaskBits(const DeceptusU16* row, DeceptusI16 words, DeceptusI16 column)
{
   DeceptusI16 word;
   DeceptusU16 shift, bits;
   word = (DeceptusI16)(column >> 4);
   shift = (DeceptusU16)(column & 15);
   bits = 0;
   if ((word >= 0) && (word < words))
   {
      bits = (DeceptusU16)(row[word] << shift);
   }
   if (shift && (word + 1 >= 0) && (word + 1 < words))
   {
      bits |= (DeceptusU16)(row[word + 1] >> (16 - shift));
   }
   return bits;
}

/* the bobs requested before the player are drawn in front of her (sprites emitted earlier are in
   front), but her hardware sprite covers every bob: clear her pixels under their masks instead. */
static void hideSpriteBehindBobs(DeceptusI16 x, DeceptusI16 y, DeceptusU16 lines, DeceptusU16 pairs)
{
   const AmigaNativeBob* bob;
   const DeceptusU16 *pixels, *row;
   DeceptusU32 plane_words;
   DeceptusI16 world_x, world_y, first, last, line, words;
   DeceptusU16 index, pair, mask;
   world_x = (DeceptusI16)(camera_x + x);
   world_y = (DeceptusI16)(camera_y + y);
   for (index = 0; index < front_bob_count; ++index)
   {
      bob = &bobs[index];
      first = (DeceptusI16)(bob->y > world_y ? bob->y - world_y : 0);
      last = (DeceptusI16)(bob->y + (DeceptusI16)bob->graphic.height - world_y);
      if (last > (DeceptusI16)lines)
      {
         last = (DeceptusI16)lines;
      }
      if ((first >= last) || (bob->x >= world_x + (DeceptusI16)pairs * 16) || (bob->x + (DeceptusI16)bob->graphic.width <= world_x))
      {
         continue;
      }
      pixels = bobPixels(&bob->graphic);
      if (!pixels)
      {
         continue;
      }
      /* bobPixels may wait for the blitter, but the sprite lines need it finished in any case. */
      waitBlitter();
      words = (DeceptusI16)(bob->graphic.row_bytes / 2);
      plane_words = (DeceptusU32)words * bob->graphic.height * PLANES;
      for (line = first; line < last; ++line)
      {
         /* the mask follows the colour planes, one row per plane: its first plane row serves. */
         row = pixels + plane_words + (DeceptusU32)(world_y + line - bob->y) * PLANES * words;
         for (pair = 0; pair < pairs; ++pair)
         {
            mask = bobMaskBits(row, words, (DeceptusI16)(world_x + pair * 16 - bob->x));
            if (mask)
            {
               clearSpritePixels((DeceptusU16)line, pair, mask);
            }
         }
      }
   }
}

static void prepareSprites(void)
{
   const AmigaNativeBob *bob, *overlay;
   const DeceptusU16 *frame, *overlay_frame;
   DeceptusU16* data;
   DeceptusU16 channel, pairs, player_pairs, lead, wanted, first, last, vstart, vstop, hstart, lines, offset, count;
   DeceptusI16 top, bottom, left, column;
   bob = &hardware_bob;
   overlay = overlay_used ? &hardware_overlay : 0;
   hardware_shown[draw_buffer] = 0;
   if (!hardware_used || bob->y >= 224 || bob->y + (DeceptusI16)bob->graphic.height <= 0 || bob->x >= 320 ||
       bob->x + (DeceptusI16)bob->graphic.width <= 0)
   {
      return;
   }
   pairs = (bob->graphic.width + 15) / 16;
   if (pairs > SPRITE_PAIRS || bob->graphic.planes != PLANES || bob->graphic.row_bytes - 2 != pairs * 2)
   {
      return;
   }
   frame = spriteFrame(&bob->graphic, pairs);
   player_pairs = pairs;
   left = bob->x;
   lead = 0;
   overlay_frame = 0;
   top = bob->y;
   bottom = bob->y + (DeceptusI16)bob->graphic.height;
   if (overlay && overlay->graphic.row_bytes - 2 == (overlay->graphic.width + 15) / 16 * 2 &&
       (overlay->graphic.width + 15) / 16 <= SPRITE_PAIRS)
   {
      /* the item may stand out on any side of the player's own lines: facing left, the helmet starts
         a few pixels left of her, and the composed sprite then starts a column further left when
         one is free. */
      overlay_frame = spriteFrame(&overlay->graphic, (overlay->graphic.width + 15) / 16);
      top = overlay->y < top ? overlay->y : top;
      bottom = overlay->y + (DeceptusI16)overlay->graphic.height > bottom ? overlay->y + (DeceptusI16)overlay->graphic.height : bottom;
      if (overlay->x < bob->x)
      {
         count = (DeceptusU16)((overlay->x + (DeceptusI16)overlay->graphic.width - (bob->x - 16) + 15) / 16);
         wanted = count > player_pairs + 1 ? count : player_pairs + 1;
         if (wanted <= SPRITE_PAIRS)
         {
            lead = 1;
            left = (DeceptusI16)(bob->x - 16);
            pairs = wanted;
         }
      }
      if (!lead)
      {
         count = (DeceptusU16)((overlay->x + (DeceptusI16)overlay->graphic.width - bob->x + 15) / 16);
         pairs = count > pairs ? (count > SPRITE_PAIRS ? SPRITE_PAIRS : count) : pairs;
      }
   }
   if (!frame)
   {
      error = 1;
      return;
   }
   /* the display window clips sprites; only the top edge needs clipping here. */
   first = top < 0 ? (DeceptusU16)-top : 0;
   last = bottom > 224 ? (DeceptusU16)(224 - top) : (DeceptusU16)(bottom - top);
   if (last - first > SPRITE_LINES)
   {
      last = first + SPRITE_LINES;
   }
   lines = last - first;
   vstart = (DeceptusU16)(0x3c + top + first);
   vstop = (DeceptusU16)(0x3c + top + last);
   for (channel = 0; channel < pairs * 2; ++channel)
   {
      hstart = (DeceptusU16)(0x80 + left + (channel >> 1) * 16);
      /* the player's own column in this channel; the lead column holds only the item. */
      column = (DeceptusI16)((channel >> 1) - lead);
      data = sprite_dma[draw_buffer][channel];
      *data++ = (DeceptusU16)(((vstart & 255) << 8) | ((hstart >> 1) & 255));
      *data++ = (DeceptusU16)(((vstop & 255) << 8) | ((vstart & 256) ? 4 : 0) | ((vstop & 256) ? 2 : 0) | (hstart & 1) |
                              ((channel & 1) ? 0x80 : 0));
      data[lines * 2] = data[lines * 2 + 1] = 0;
      if (overlay_frame || (column >= (DeceptusI16)player_pairs))
      {
         /* composed lines start empty: rows outside the player and columns beside her. */
         spriteBlit(0, data, lines);
      }
      if ((column < 0) || (column >= (DeceptusI16)player_pairs))
      {
         continue;
      }
      /* the player's visible lines (two words each) are one blitter copy into their rows. */
      offset = (DeceptusU16)(bob->y - top);
      count = (DeceptusU16)(first > offset ? first - offset : 0);
      if (count >= bob->graphic.height)
      {
         continue;
      }
      count = (DeceptusU16)(bob->graphic.height - count < (DeceptusU16)(last - (offset > first ? offset : first))
                               ? bob->graphic.height - count
                               : last - (offset > first ? offset : first));
      if (count)
      {
         spriteBlit(
            frame + (DeceptusU32)(channel - lead * 2) * 2 * bob->graphic.height + (first > offset ? first - offset : 0) * 2,
            data + (offset > first ? offset - first : 0) * 2,
            count
         );
      }
   }
   if (overlay_frame)
   {
      waitBlitter();
      spriteOverlay(overlay, overlay_frame, left, top, first, lines, pairs);
   }
   hideSpriteBehindForeground(left, (DeceptusI16)(top + first), lines, pairs);
   hideSpriteBehindBobs(left, (DeceptusI16)(top + first), lines, pairs);
   hardware_shown[draw_buffer] = (DeceptusU8)(pairs * 2);
}

static void copperWrite(DeceptusU16** cursor, DeceptusU16 reg, DeceptusU16 value)
{
   *(*cursor)++ = reg;
   *(*cursor)++ = value;
}

static void planePointer(DeceptusU16** cursor, DeceptusU16 plane, DeceptusU16 offset)
{
   DeceptusU32 address;
   address = (DeceptusU32)(pages[draw_buffer] + offset);
   copperWrite(cursor, offsetof(struct Custom, bplpt) + plane * 4, address >> 16);
   copperWrite(cursor, offsetof(struct Custom, bplpt) + plane * 4 + 2, address);
}

/* waits for the start of a screen row; the first row past line 255 needs the wrap wait. */
static void copperWaitRow(DeceptusU16** cursor, DeceptusU16 row, DeceptusU8* crossed)
{
   DeceptusU16 line;
   line = row + 0x3c;
   if (line >= 256 && !*crossed)
   {
      copperWrite(cursor, 0xffdf, 0xfffe);
      *crossed = 1;
   }
   copperWrite(cursor, ((line & 255) << 8) | 1, 0xfffe);
}

/* the message box rows. stage 0, on two empty region rows: pointers to the region (written before
   the line's fetch starts), playfield in front of the sprites so the box hides the player while
   the sprites keep their colours in the transparent corners, then the box palette while the
   region shows only colour 0. stage 1, on the two empty rows below the frame: the room palette.
   stage 2: the room's pointers, modulos, scroll and sprite priority for the rest of the screen. */
static BOX_CODE void boxSplit(
   DeceptusU16** cursor,
   DeceptusU8 stage,
   const DeceptusU16* start,
   const DeceptusU16* room_wraps,
   DeceptusU16 delay
)
{
   DeceptusU16 plane, row;
   DeceptusU32 address;
   if (stage == 0)
   {
      for (plane = 0; plane < PLANES; ++plane)
      {
         address = (DeceptusU32)box_region + (DeceptusU32)plane * AMIGA_BOX_ROW_BYTES;
         copperWrite(cursor, offsetof(struct Custom, bplpt) + plane * 4, address >> 16);
         copperWrite(cursor, offsetof(struct Custom, bplpt) + plane * 4 + 2, address);
      }
      copperWrite(cursor, offsetof(struct Custom, bpl1mod), (PLANES - 1) * AMIGA_BOX_ROW_BYTES);
      copperWrite(cursor, offsetof(struct Custom, bpl2mod), (PLANES - 1) * AMIGA_BOX_ROW_BYTES);
      copperWrite(cursor, offsetof(struct Custom, bplcon1), 0);
      copperWrite(cursor, offsetof(struct Custom, bplcon2), 0);
   }
   if (stage < 2)
   {
      /* colour 0 stays the room's: it shows in the region's empty corners and margins. */
      if (stage == 0)
      {
         box_palette_moves[draw_buffer] = *cursor + 1;
      }
      for (plane = 1; plane < 16; ++plane)
      {
         copperWrite(cursor, offsetof(struct Custom, color) + plane * 2, stage == 0 ? box_palette[plane] : lit_palette[plane]);
      }
      return;
   }
   row = AMIGA_BOX_TOP + AMIGA_BOX_ROWS;
   for (plane = 0; plane < PLANES; ++plane)
   {
      address = (DeceptusU32)start[plane] + (DeceptusU32)row * surface.stride;
      if (row >= room_wraps[plane])
      {
         address -= SURFACE_WORDS;
      }
      planePointer(cursor, plane, (DeceptusU16)address);
   }
   copperWrite(cursor, offsetof(struct Custom, bpl1mod), surface.stride * 2 - 42);
   copperWrite(cursor, offsetof(struct Custom, bpl2mod), surface.stride * 2 - 42);
   copperWrite(cursor, offsetof(struct Custom, bplcon1), delay | (delay << 4));
   copperWrite(cursor, offsetof(struct Custom, bplcon2), 0x0024);
}

/* the plane pointer events of a list that shows the box: the room's wraps plus the box splits;
   inside the box the region's pointers run, the restore below the box accounts for the wrap. */
static BOX_CODE void boxCopper(DeceptusU16** cursor, const DeceptusU16* start, DeceptusU16* wraps, DeceptusU16 delay)
{
   static const DeceptusU16 box_rows[3] = {
      AMIGA_BOX_TOP, AMIGA_BOX_TOP + AMIGA_BOX_BUBBLE_ROW + AMIGA_BOX_BUBBLE_ROWS, AMIGA_BOX_TOP + AMIGA_BOX_ROWS
   };
   DeceptusU16 plane, room_wraps[PLANES], minimum, box_row;
   DeceptusU8 box_stage, inside, crossed;
   for (plane = 0; plane < PLANES; ++plane)
   {
      room_wraps[plane] = wraps[plane];
   }
   crossed = 0;
   box_stage = 0;
   for (;;)
   {
      minimum = 224;
      for (plane = 0; plane < PLANES; ++plane)
      {
         if (wraps[plane] < minimum)
         {
            minimum = wraps[plane];
         }
      }
      box_row = box_stage < 3 ? box_rows[box_stage] : 224;
      if (minimum == 224 && box_row == 224)
      {
         break;
      }
      if (box_row <= minimum)
      {
         copperWaitRow(cursor, box_row, &crossed);
         boxSplit(cursor, box_stage, start, room_wraps, delay);
         ++box_stage;
         continue;
      }
      inside = minimum >= AMIGA_BOX_TOP && minimum <= AMIGA_BOX_TOP + AMIGA_BOX_ROWS;
      if (!inside)
      {
         copperWaitRow(cursor, minimum, &crossed);
      }
      for (plane = 0; plane < PLANES; ++plane)
      {
         if (wraps[plane] == minimum)
         {
            if (!inside)
            {
               planePointer(
                  cursor, plane, (DeceptusU16)((DeceptusU32)start[plane] + (DeceptusU32)minimum * surface.stride - SURFACE_WORDS)
               );
            }
            wraps[plane] = 224;
         }
      }
   }
}

/* one RGB444 colour at a flash level. */
static DeceptusU16 stormColour(DeceptusU16 colour, DeceptusU8 level)
{
   return (DeceptusU16)(((DeceptusU16)thunderstormChannel((DeceptusU8)((colour >> 8) & 15), 15, level) << 8) |
                        ((DeceptusU16)thunderstormChannel((DeceptusU8)((colour >> 4) & 15), 15, level) << 4) |
                        thunderstormChannel((DeceptusU8)(colour & 15), 15, level));
}

/* thunderstormoverlay.cpp lights only the sky and far forest: their colours follow the flash. */
static void prepareStorm(const DeceptusAdventure* game)
{
   DeceptusU8 level, index, entry;
   level = thunderstormLevel(&game->thunderstorm);
   if (level == lit_level)
   {
      return;
   }
   lit_level = level;
   for (index = 0; index < storm_count; ++index)
   {
      entry = storm_entries[index];
      lit_palette[entry] = stormColour(palette[entry], level);
   }
}

static void prepareCopper(void)
{
   DeceptusU16* cursor;
   DeceptusU16 plane, delay, start[PLANES], wraps[PLANES], event, line, minimum;
   DeceptusI16 column;
   DeceptusU32 address;
   DeceptusU8 crossed;
   /* a list keeps its constant moves (display window, modulos, the room palette) from its first
      build in a room; later builds rewrite only the moves that follow the camera and the sprites. */
   cursor = copper[draw_buffer];
   box_palette_moves[draw_buffer] = 0;
   if (!copper_built[draw_buffer])
   {
      copperWrite(&cursor, offsetof(struct Custom, diwstrt), 0x3c81);
      copperWrite(&cursor, offsetof(struct Custom, diwstop), 0x1cc1);
      copperWrite(&cursor, offsetof(struct Custom, ddfstrt), 0x0030);
      copperWrite(&cursor, offsetof(struct Custom, ddfstop), 0x00d0);
      copperWrite(&cursor, offsetof(struct Custom, bplcon0), (PLANES << 12) | 0x0200);
   }
   else
   {
      cursor += 5 * 2;
   }
   delay = (16 - (camera_x & 15)) & 15;
   copperWrite(&cursor, offsetof(struct Custom, bplcon1), delay | (delay << 4));
   /* sprites in front of the playfield. */
   if (!copper_built[draw_buffer])
   {
      copperWrite(&cursor, offsetof(struct Custom, bplcon2), 0x0024);
   }
   else
   {
      cursor += 2;
   }
   for (plane = 0; plane < 8; ++plane)
   {
      address = plane < hardware_shown[draw_buffer] ? (DeceptusU32)sprite_dma[draw_buffer][plane] : (DeceptusU32)null_sprite;
      copperWrite(&cursor, offsetof(struct Custom, sprpt) + plane * 4, address >> 16);
      copperWrite(&cursor, offsetof(struct Custom, sprpt) + plane * 4 + 2, address);
   }
   if (!copper_built[draw_buffer])
   {
      copperWrite(&cursor, offsetof(struct Custom, bpl1mod), surface.stride * 2 - 42);
      copperWrite(&cursor, offsetof(struct Custom, bpl2mod), surface.stride * 2 - 42);
   }
   else
   {
      cursor += 2 * 2;
   }
   column = tileFloor(camera_x) - ((camera_x & 15) == 0 ? 1 : 0);
   for (plane = 0; plane < PLANES; ++plane)
   {
      start[plane] = amigaScrollSurfaceOffset(&surface, column, camera_y, (DeceptusU8)plane);
      wraps[plane] = amigaScrollSurfaceWrap(&surface, start[plane]);
      planePointer(&cursor, plane, start[plane]);
   }
   if (!copper_built[draw_buffer] || (copper_level[draw_buffer] != lit_level))
   {
      for (plane = 0; plane < 32; ++plane)
      {
         copperWrite(&cursor, offsetof(struct Custom, color) + plane * 2, lit_palette[plane]);
      }
      copper_built[draw_buffer] = 1;
      copper_level[draw_buffer] = lit_level;
   }
   else
   {
      cursor += 32 * 2;
   }
   if (box_visible)
   {
      boxCopper(&cursor, start, wraps, delay);
   }
   else
   {
      crossed = 0;
      for (event = 0; event < PLANES; ++event)
      {
         minimum = 224;
         for (plane = 0; plane < PLANES; ++plane)
         {
            if (wraps[plane] < minimum)
            {
               minimum = wraps[plane];
            }
         }
         if (minimum == 224)
         {
            break;
         }
         line = minimum + 0x3c;
         if (line >= 256 && !crossed)
         {
            copperWrite(&cursor, 0xffdf, 0xfffe);
            crossed = 1;
         }
         copperWrite(&cursor, ((line & 255) << 8) | 1, 0xfffe);
         for (plane = 0; plane < PLANES; ++plane)
         {
            if (wraps[plane] == minimum)
            {
               planePointer(
                  &cursor, plane, (DeceptusU16)((DeceptusU32)start[plane] + (DeceptusU32)minimum * surface.stride - SURFACE_WORDS)
               );
               wraps[plane] = 224;
            }
         }
      }
   }
   copperWrite(&cursor, 0xffff, 0xfffe);
}

ROOM_CODE void amigaOpeningVideoReset(void)
{
   DeceptusU8* memory;
   const DeceptusU8 *data, *record;
   DeceptusU32 size, start, end, index, cells, fixed, saved, mask_bytes, spare, extra, cache_size, minimum;
   DeceptusU8* cache_memory;
   DeceptusU16 clip;
   amigaOpeningVideoSuspend();
   graphics = amigaRoomGraphics();
   tiles = amigaRoomTiles();
   error = 0;
   if (!amigaScrollSurfaceInit(&surface, SURFACE_COLUMNS, SURFACE_HEIGHT, PLANES) || !graphics->interleaved_sprites || !tiles->bytes ||
       !amigaGraphicsPalette(graphics, palette) || !amigaGraphicsStormEntries(graphics, storm_entries, &storm_count))
   {
      error = 1;
      amiga_debug[30] = 6; /* failure site */
      return;
   }
   memcpy(lit_palette, palette, sizeof(lit_palette));
   lit_level = 0;
   memory = amigaRoomChipScratch(&size);
   cache_memory = amigaRoomScratch(&cache_size);
   amiga_debug[31] = size;
   mask_bytes = AMIGA_NATIVE_TILES_PER_BANK * AMIGA_NATIVE_MASK_BYTES;
   /* dictionary banks are cpu-read and prefer non-chip scratch; one cell can name four banks of each. */
   minimum = 4 * (AMIGA_NATIVE_BANK_BYTES + mask_bytes);
   fixed = NATIVE_SPRITE_BYTES + UI_MASK_BYTES + SPRITE_CACHE_WORDS * 2 + CELL_SETS_MIN * (PLAIN_SET_BYTES + MASKED_SET_BYTES) +
           (cache_size < minimum ? minimum : 0);
   if (size < fixed + 8192)
   {
      amigaSystemReportMemory("Deceptus renderer scratch", fixed + 8192, 1);
      error = 1;
      amiga_debug[30] = 13; /* failure site */
      return;
   }
   saved = size - fixed;
   if (saved > MAX_SAVED_BYTES * 2)
   {
      saved = MAX_SAVED_BYTES * 2;
   }
   saved &= ~3UL;
   spare = size - fixed - saved;
   /* decoded bob frames: a flushed arena re-runs lz4 for every visible frame, so it takes a third of
      the spare Chip RAM before the cell caches. */
   extra = spare / 3 < SPRITE_ARENA_MAX - NATIVE_SPRITE_BYTES ? spare / 3 : SPRITE_ARENA_MAX - NATIVE_SPRITE_BYTES;
   extra &= ~3UL;
   spare -= extra;
   sprite_arena_bytes = NATIVE_SPRITE_BYTES + extra;
   sprite_entry_count = 0;
   measureReach();
   sprite_arena_used = 0;
   /* a new room starts without a box; its file came with the room. */
   box_lent_from = 0;
   box_page = BOX_NO_PAGE;
   box_template = 255;
   box_palette_moves[0] = box_palette_moves[1] = 0;
   box_file = amigaRoomMessageBoxes(&box_file_bytes);
   /* blitter sources: ui text mask, decoded sprite frames and saved backgrounds stay in Chip RAM. */
   ui_mask = (DeceptusU16*)memory;
   memory += UI_MASK_BYTES;
   /* decoded hardware sprite frames: cpu-read, but slow RAM is already the room data's. */
   sprite_cache = (DeceptusU16*)memory;
   memory += SPRITE_CACHE_WORDS * 2;
   ui_mask_valid = 0;
   sprite_pixels = (DeceptusU16*)memory;
   memory += sprite_arena_bytes;
   saved_capacity = (DeceptusU16)(saved / 4);
   saved_pixels[0] = (DeceptusU16*)memory;
   saved_pixels[1] = saved_pixels[0] + saved_capacity;
   memory += (DeceptusU32)saved_capacity * 4;
   /* composed cells turn repaints into blits and take most of the remaining Chip RAM; the dictionary
      banks only feed cell misses. one plain set (two cells) costs 320 bytes, one masked set 640. */
   /* three fifths of the spare Chip RAM: two thirds plain cells, one third masked cells. */
   index = spare / 5 * 3 / 3 * 2 / PLAIN_SET_BYTES;
   plain_cells.sets = (DeceptusU16)(CELL_SETS_MIN + (index < PLAIN_SETS_MAX - CELL_SETS_MIN ? index : PLAIN_SETS_MAX - CELL_SETS_MIN));
   index = spare / 5 * 3 / 3 / MASKED_SET_BYTES;
   masked_cells.sets = (DeceptusU16)(CELL_SETS_MIN + (index < MASKED_SETS_MAX - CELL_SETS_MIN ? index : MASKED_SETS_MAX - CELL_SETS_MIN));
   spare -= ((DeceptusU32)plain_cells.sets - CELL_SETS_MIN) * PLAIN_SET_BYTES +
            ((DeceptusU32)masked_cells.sets - CELL_SETS_MIN) * MASKED_SET_BYTES;
   plain_cells.pixels = (DeceptusU16*)memory;
   memory += (DeceptusU32)plain_cells.sets * CELL_WORDS * 4;
   masked_cells.pixels = (DeceptusU16*)memory;
   memory += (DeceptusU32)masked_cells.sets * CELL_WORDS * 8;
   plain_cells.keys = (DeceptusU16*)memory;
   memory += (DeceptusU32)plain_cells.sets * 2 * 4 * 2;
   masked_cells.keys = (DeceptusU16*)memory;
   memory += (DeceptusU32)masked_cells.sets * 2 * 8 * 2;
   plain_cells.recent = memory;
   memory += (DeceptusU32)plain_cells.sets * 2;
   plain_cells.generation = (DeceptusU16*)memory;
   memory += (DeceptusU32)plain_cells.sets * 4;
   memset(plain_cells.generation, 0, (DeceptusU32)plain_cells.sets * 4);
   /* the map memo takes spare Chip RAM only when the room leaves enough for it. */
   map_columns = tiles->columns;
   map_rows = tiles->rows;
   map_slots = 0;
   if (spare > (DeceptusU32)map_columns * map_rows * 4 + 16384UL)
   {
      map_slots = (DeceptusU16*)memory;
      memory += (DeceptusU32)map_columns * map_rows * 4;
      spare -= (DeceptusU32)map_columns * map_rows * 4;
      memset(map_slots, 0, (DeceptusU32)map_columns * map_rows * 4);
   }
   masked_cells.recent = memory;
   memory += (DeceptusU32)masked_cells.sets * 2;
   resetCells(&plain_cells);
   resetCells(&masked_cells);
   /* the decoded-bank cache takes whichever scratch is larger; a minimal non-chip remainder must not
      strand the Chip RAM left over after the blitter buffers. */
   if (cache_size < minimum)
   {
      /* the Chip layout above reserved the minimum banks. */
      cache_memory = memory;
      cache_size = minimum + spare;
   }
   else if (spare > cache_size)
   {
      cache_memory = memory;
      cache_size = spare;
   }
   /* colour banks dominate scrolling cost: fill them first, then masks, then more colour banks. */
   index = (cache_size - minimum) / AMIGA_NATIVE_BANK_BYTES;
   color_cache.count = 4 + (DeceptusU16)(index < COLOR_SLOTS_FIRST - 4 ? index : COLOR_SLOTS_FIRST - 4);
   cache_size -= (DeceptusU32)color_cache.count * AMIGA_NATIVE_BANK_BYTES;
   index = (cache_size - 4 * mask_bytes) / mask_bytes;
   mask_cache.count = 4 + (DeceptusU16)(index < MASK_SLOTS_MAX - 4 ? index : MASK_SLOTS_MAX - 4);
   cache_size -= (DeceptusU32)mask_cache.count * mask_bytes;
   index = cache_size / AMIGA_NATIVE_BANK_BYTES;
   color_cache.count +=
      (DeceptusU16)(index < (DeceptusU32)(BANK_SLOTS_MAX - color_cache.count) ? index : BANK_SLOTS_MAX - color_cache.count);
   mask_cache.pixels = (DeceptusU16*)cache_memory;
   cache_memory += (DeceptusU32)mask_cache.count * mask_bytes;
   color_cache.pixels = (DeceptusU16*)cache_memory;
   for (index = 0; index < BANK_SLOTS_MAX; ++index)
   {
      color_cache.banks[index] = mask_cache.banks[index] = 65535U;
      color_cache.ages[index] = mask_cache.ages[index] = 0;
   }
   for (index = 0; index < BANK_INDEX_MAX; ++index)
   {
      raw_colors[index] = amigaTileArchiveRawBank(tiles, 0, (DeceptusU16)index);
      raw_masks[index] = amigaTileArchiveRawBank(tiles, 1, (DeceptusU16)index);
   }
   memset(color_cache.slot_of, 0, sizeof(color_cache.slot_of));
   memset(mask_cache.slot_of, 0, sizeof(mask_cache.slot_of));
   amiga_debug[31] = ((DeceptusU32)plain_cells.sets << 16) | masked_cells.sets | (map_slots ? 0x8000UL : 0); /* cell sets, memo */
   amiga_debug[29] = ((DeceptusU32)color_cache.count << 16) | ((DeceptusU32)mask_cache.count << 8) | (sprite_arena_bytes >> 10);
   memset(windows, 0, sizeof(windows));
   copper_built[0] = copper_built[1] = 0;
   memset(saved_counts, 0, sizeof(saved_counts));
   memset(saved_used, 0, sizeof(saved_used));
   memset(ui_counts, 0, sizeof(ui_counts));
   memset(previous_enabled, 0, sizeof(previous_enabled));
   memset(previous_frames, 0, sizeof(previous_frames));
   data = amigaRoomTileAnimations(&size);
   start = 8 + ((DeceptusU32)graphics->clip_count + 1) * 4;
   if (!data || size < start || data[0] != 'A' || data[1] != 'A' || data[2] != '0' || data[3] != '2' ||
       readWord(data + 4) != graphics->clip_count || graphics->clip_count > AMIGA_OPENING_CLIPS || readLong(data + 8))
   {
      error = 1;
      amiga_debug[30] = 8; /* failure site */
      return;
   }
   for (clip = 0; clip < graphics->clip_count; ++clip)
   {
      index = readLong(data + 8 + (DeceptusU32)clip * 4);
      end = readLong(data + 12 + (DeceptusU32)clip * 4);
      if (index > end || end > size - start || end - index < 10 || !amigaGraphicsClip(graphics, clip, &clips[clip].clip))
      {
         error = 1;
         amiga_debug[30] = 9; /* failure site */
         return;
      }
      record = data + start + index;
      clips[clip].column = (DeceptusI16)readWord(record);
      clips[clip].row = (DeceptusI16)readWord(record + 2);
      clips[clip].columns = readWord(record + 4);
      clips[clip].rows = readWord(record + 6);
      clips[clip].cells = record + 10;
      cells = (DeceptusU32)clips[clip].columns * clips[clip].rows;
      if (!cells || cells * clips[clip].clip.frames > 65535UL || readWord(record + 8) != clips[clip].clip.frames ||
          cells > (end - index - 10) / CLIP_CELL_BYTES / clips[clip].clip.frames ||
          cells * clips[clip].clip.frames * CLIP_CELL_BYTES != end - index - 10)
      {
         error = 1;
         amiga_debug[30] = 10; /* failure site */
         return;
      }
      for (index = 0; index < cells * clips[clip].clip.frames; ++index)
      {
         record = clips[clip].cells + index * CLIP_CELL_BYTES;
         if (readWord(record) >= tiles->colors || readWord(record + 2) >= tiles->colors || readWord(record + 4) >= tiles->colors ||
             readWord(record + 6) >= tiles->colors || readWord(record + 8) >= tiles->masks || readWord(record + 10) >= tiles->masks ||
             readWord(record + 12) >= tiles->masks || readWord(record + 14) >= tiles->masks)
         {
            error = 1;
            amiga_debug[30] = 11; /* failure site */
            return;
         }
      }
   }
   if (readLong(data + 8 + (DeceptusU32)graphics->clip_count * 4) != size - start)
   {
      error = 1;
      amiga_debug[30] = 12; /* failure site */
      return;
   }
   pending_buffer = -1;
   draw_buffer = 0;
   last_ticks = 65535U;
   spriteMirrorTable();
}

/* renders gameplay ui text into one-plane bands when its content changes; 0 if it does not fit them. */
static DeceptusU8 prepareUiMask(const DeceptusAdventure* game, DeceptusU16 signature)
{
   DeceptusRect rectangles[2];
   DeceptusU8 count, index;
   if (game->campaign.mode != DECEPTUS_CAMPAIGN_PLAYING)
   {
      return 0;
   }
   if (ui_mask_valid && ui_mask_signature == signature && ui_mask_room == game->campaign.room_index)
   {
      return 1;
   }
   ui_mask_valid = 0;
   count = amigaOpeningUiDrawMask(game, 0, UI_BAND_WORDS * 2, 0, 0, rectangles);
   if (count > UI_BANDS)
   {
      return 0;
   }
   for (index = 0; index < count; ++index)
   {
      if (rectangles[index].height > UI_BAND_ROWS || rectangles[index].x < 0 || rectangles[index].x + rectangles[index].width > 320)
      {
         return 0;
      }
   }
   waitBlitter();
   memset(ui_mask, 0, UI_MASK_BYTES);
   for (index = 0; index < count; ++index)
   {
      ui_bands[index].screen = rectangles[index];
      amigaOpeningUiDrawMask(
         game,
         (DeceptusU8*)(ui_mask + (DeceptusU32)index * UI_BAND_ROWS * UI_BAND_WORDS),
         UI_BAND_WORDS * 2,
         rectangles[index].y,
         rectangles[index].y + rectangles[index].height,
         rectangles
      );
   }
   ui_band_count = count;
   ui_mask_signature = signature;
   ui_mask_room = game->campaign.room_index;
   ui_mask_valid = 1;
   return 1;
}

/* ors a cached text band into all five planes; the barrel shifter absorbs the camera's sub-word scroll. */
static void blitUiBand(const AmigaUiBand* band, const DeceptusU16* mask)
{
   DeceptusU16 plane, offset, rows, count, available, last;
   DeceptusI16 world_y;
   const DeceptusU16* source;
   world_y = camera_y + band->screen.y;
   /* a source-only blit of one masked-out word leaves zero in the shifter, so the first shifted
      line takes no bits from the box clear blit; each band line then ends on a masked word. */
   waitBlitter();
   blit_setup = 0;
   hardware->bltcon0 = SRCA;
   hardware->bltcon1 = 0;
   hardware->bltafwm = hardware->bltalwm = 0;
   hardware->bltapt = (APTR)mask;
   hardware->bltsize = (1 << 6) | 1;
   for (plane = 0; plane < PLANES; ++plane)
   {
      offset = amigaScrollSurfaceOffset(&surface, camera_x >> 4, world_y, (DeceptusU8)plane);
      source = mask;
      rows = (DeceptusU16)band->screen.height;
      while (rows)
      {
         available = (DeceptusU16)((SURFACE_WORDS - 1 - offset) / (SURFACE_COLUMNS * PLANES) + 1);
         count = rows < available ? rows : available;
         waitBlitter();
         blit_setup = 0;
         hardware->bltcon0 = (DeceptusU16)(((camera_x & 15) << 12) | SRCA | SRCC | DEST | 0xfa);
         hardware->bltcon1 = 0;
         hardware->bltafwm = 65535U;
         hardware->bltalwm = 0;
         hardware->bltamod = (DeceptusU16)-2;
         hardware->bltcmod = hardware->bltdmod = (SURFACE_COLUMNS * PLANES - (UI_BAND_WORDS + 1)) * 2;
         hardware->bltapt = (APTR)source;
         hardware->bltcpt = pages[draw_buffer] + offset;
         hardware->bltdpt = pages[draw_buffer] + offset;
         hardware->bltsize = (count << 6) | (UI_BAND_WORDS + 1);
         last = offset + (count - 1) * SURFACE_COLUMNS * PLANES;
         repairGuardAfter(offset, last, UI_BAND_WORDS + 1);
         source += (DeceptusU32)count * UI_BAND_WORDS;
         /* count never passes the ring end by more than one row: one subtraction wraps it. */
         last = (DeceptusU16)(offset + count * (SURFACE_COLUMNS * PLANES));
         offset = last >= SURFACE_WORDS ? (DeceptusU16)(last - SURFACE_WORDS) : last;
         rows -= count;
      }
   }
}

/* the open dialogue's page in the room's box file, or BOX_NO_PAGE when no box shows. the file's
   actions follow the room's interactions; a count that disagrees with the shared content means a
   stale file, and the plain surface box shows instead. */
static BOX_CODE DeceptusU16 messageBoxPage(const DeceptusAdventure* game)
{
   const DeceptusAdventureRoom* room;
   const AmigaBoxHeader* header;
   const DeceptusU16* actions;
   DeceptusU16 action, first;
   if ((game->campaign.mode != DECEPTUS_DIALOGUE && game->campaign.mode != DECEPTUS_NOTIFICATION) || !game->dialogue ||
       game->page >= game->dialogue->page_count || !box_file)
   {
      return BOX_NO_PAGE;
   }
   room = &game->definition->rooms[game->campaign.room_index];
   if (game->dialogue < room->interactions || game->dialogue >= room->interactions + room->interaction_count)
   {
      return BOX_NO_PAGE;
   }
   header = (const AmigaBoxHeader*)box_file;
   action = (DeceptusU16)(game->dialogue - room->interactions);
   if (action >= header->actions)
   {
      return BOX_NO_PAGE;
   }
   actions = (const DeceptusU16*)(box_file + sizeof(AmigaBoxHeader) + (DeceptusU32)header->templates * sizeof(AmigaBoxTemplate));
   first = actions[action];
   if (actions[action + 1] - first != game->dialogue->page_count || actions[action + 1] > header->pages)
   {
      return BOX_NO_PAGE;
   }
   return (DeceptusU16)(first + game->page);
}

/* lends the end of the sprite arena to the box: decoded frames there are dropped (the arena is a
   cache) and the arena keeps at least one frame's worth. */
static BOX_CODE DeceptusU8 lendBoxMemory(void)
{
   DeceptusU32 keep;
   DeceptusU16 index;
   if (box_lent_from)
   {
      return 1;
   }
   if (sprite_arena_bytes < NATIVE_SPRITE_BYTES + BOX_BLOCK_BYTES + 4)
   {
      return 0;
   }
   keep = (sprite_arena_bytes - BOX_BLOCK_BYTES) & ~3UL;
   /* blits may still read frames from the lent end. */
   waitBlitter();
   for (index = 0; index < sprite_entry_count;)
   {
      if (sprite_entries[index].start + sprite_entries[index].size > keep)
      {
         sprite_entries[index] = sprite_entries[--sprite_entry_count];
      }
      else
      {
         ++index;
      }
   }
   if (sprite_arena_used > keep)
   {
      sprite_arena_used = 0;
   }
   box_lent_from = sprite_arena_bytes;
   sprite_arena_bytes = keep;
   box_region = (DeceptusU16*)((DeceptusU8*)sprite_pixels + keep);
   box_staging = box_region + BOX_REGION_BYTES / 2;
   box_compose = box_staging + BOX_REGION_BYTES / 2;
   box_text = box_compose + BOX_TEXT_BYTES / 2;
   box_page = BOX_NO_PAGE;
   box_template = 255;
   return 1;
}

/* gives the memory back once no list that can still be on screen shows the box: the list being
   built does not, and the other one is the list shown since the last swap. */
static BOX_CODE void returnBoxMemory(void)
{
   if (box_lent_from && !box_palette_moves[draw_buffer ^ 1])
   {
      sprite_arena_bytes = box_lent_from;
      box_lent_from = 0;
      box_page = BOX_NO_PAGE;
      box_template = 255;
   }
}

/* composes the page into the box region when it changes: the frame template, then the text in
   the template's white. one LZ4 decode per page turn, nothing while the page shows. */
static BOX_CODE DeceptusU8 prepareMessageBox(const DeceptusAdventure* game)
{
   const AmigaBoxHeader* header;
   const AmigaBoxPage* page;
   const AmigaBoxTemplate* frame;
   DeceptusU16 page_id, row, plane, word, *line;
   const DeceptusU16* mask;
   page_id = messageBoxPage(game);
   if (page_id == BOX_NO_PAGE)
   {
      returnBoxMemory();
      return 0;
   }
   if (!lendBoxMemory())
   {
      return 0;
   }
   if (page_id == box_page)
   {
      return 1;
   }
   header = (const AmigaBoxHeader*)box_file;
   page = (const AmigaBoxPage*)(box_file + sizeof(AmigaBoxHeader) + (DeceptusU32)header->templates * sizeof(AmigaBoxTemplate) +
                                (((DeceptusU32)header->actions * 2 + 2 + 3) & ~3UL)) +
          page_id;
   if (page->template_id >= header->templates || page->offset + page->size > box_file_bytes)
   {
      return 0;
   }
   frame = (const AmigaBoxTemplate*)(box_file + sizeof(AmigaBoxHeader)) + page->template_id;
   if (frame->offset + frame->size > box_file_bytes)
   {
      return 0;
   }
   if (!amigaLz4Decode(box_file + page->offset, page->size, (DeceptusU8*)box_text, BOX_TEXT_MASK_BYTES))
   {
      box_page = BOX_NO_PAGE;
      return 0;
   }
   if (page->template_id != box_template)
   {
      box_template = 255;
      if (!amigaLz4Decode(box_file + frame->offset, frame->size, (DeceptusU8*)box_staging, BOX_REGION_BYTES))
      {
         box_page = BOX_NO_PAGE;
         return 0;
      }
   }
   /* the text goes into a copy of the template's text rows first. */
   memcpy(box_compose, box_staging + BOX_TEXT_OFFSET, BOX_TEXT_BYTES);
   mask = box_text;
   for (row = 0; row < AMIGA_BOX_TEXT_ROWS; ++row)
   {
      for (plane = 0; plane < AMIGA_BOX_PLANES; ++plane)
      {
         line = box_compose + ((DeceptusU32)row * AMIGA_BOX_PLANES + plane) * (AMIGA_BOX_ROW_BYTES / 2) + AMIGA_BOX_TEXT_WORD;
         for (word = 0; word < AMIGA_BOX_TEXT_WORDS; ++word)
         {
            if ((frame->text >> plane) & 1)
            {
               line[word] |= mask[word];
            }
            else
            {
               line[word] &= (DeceptusU16)~mask[word];
            }
         }
      }
      mask += AMIGA_BOX_TEXT_WORDS;
   }
   /* the region is single-buffered: while a box shows, rewrite it just after the vertical blank.
      only plain copies run then, top row first, so they stay ahead of the beam and no shown
      frame has a half-written box. */
   if (box_page != BOX_NO_PAGE)
   {
      waitFrame();
   }
   if (box_template == 255)
   {
      /* the shown list keeps the old template's colours until the next page goes up. */
      if (box_page != BOX_NO_PAGE && box_palette_moves[draw_buffer ^ 1])
      {
         for (plane = 1; plane < 16; ++plane)
         {
            box_palette_moves[draw_buffer ^ 1][(plane - 1) * 2] = frame->palette[plane];
         }
      }
      memcpy(box_region, box_staging, BOX_TEXT_OFFSET * 2);
      memcpy(box_region + BOX_TEXT_OFFSET, box_compose, BOX_TEXT_BYTES);
      memcpy(
         box_region + BOX_TEXT_OFFSET + BOX_TEXT_BYTES / 2,
         box_staging + BOX_TEXT_OFFSET + BOX_TEXT_BYTES / 2,
         BOX_REGION_BYTES - BOX_TEXT_OFFSET * 2 - BOX_TEXT_BYTES
      );
      box_template = page->template_id;
   }
   else
   {
      memcpy(box_region + BOX_TEXT_OFFSET, box_compose, BOX_TEXT_BYTES);
   }
   box_palette = frame->palette;
   box_page = page_id;
   return 1;
}

/* mirrors which boxes amigaOpeningUiDrawSurface places; equal signatures mean equal box layout. */
static DeceptusU16 uiSignature(const DeceptusAdventure* game)
{
   DeceptusU16 signature;
   signature = game->campaign.mode;
   if (game->room_label_ticks)
   {
      signature |= 0x100;
   }
   if (game->prompt < game->definition->rooms[game->campaign.room_index].interaction_count)
   {
      signature |= 0x200;
   }
   return signature;
}

DeceptusU8 amigaOpeningVideoDraw(const DeceptusAdventure* game, const DeceptusFrame* frame)
{
   DeceptusU16 index, count, clip, signature, run_end, run_cell, color_ids[4], mask_ids[4];
   DeceptusU8* mark;
#ifdef AMIGA_PERFCOUNT
   DeceptusU32 draw_started;
#endif
   DeceptusU8 cached;
   DeceptusRect kept;
   DeceptusI16 x, y;
   const DeceptusU8* cell;
   AmigaSavedBob* saved;
   if (error)
   {
      return 0;
   }
   if (!active)
   {
      loading = 1;
      amigaSystemRemoveVerticalBlankServer();
      amigaSystemTakeDisplay(verticalBlank, blank_copper);
      active = 1;
      blit_setup = 0;
   }
   if (windows[draw_buffer].valid && game->campaign.mode != DECEPTUS_CAMPAIGN_PLAYING && last_ticks == game->campaign.engine.ticks &&
       last_mode == game->campaign.mode && last_selection == game->selection && last_page == game->page && last_flags == game->flags &&
       last_dialogue == game->dialogue && last_prompt == game->prompt && last_label == (game->room_label_ticks != 0))
   {
      waitFrame();
      return 1;
   }
   AMIGA_PERF(phase_mark = draw_started = beamStamp();)
   /* work that touches no page runs before waiting for the previous page to be shown: the
      camera, the animation clocks and the object list. */
   camera_x = frame->camera_x;
   camera_y = frame->camera_y;
   camera_column = tileFloor(camera_x);
   camera_row = tileFloor(camera_y);
   updateClips(game);
   bob_count = front_bob_count = 0;
   item_bob_used = 0;
   hardware_used = overlay_used = 0;
   if (!deceptusAppearanceVisit(game, frame, &amiga_appearance_definition, &animation, requestBob, 0))
   {
      return 0;
   }
   AMIGA_PERF(amigaPerfPhase(11);)
   if (pending_buffer >= 0)
   {
      AmigaScrollWindow ahead;
      ahead = windows[draw_buffer ^ 1];
      (void)amigaScrollSurfaceVisit(&surface, &ahead, camera_x, camera_y, warmTile, 0);
   }
   while (pending_buffer >= 0)
   {
   }
   __asm__ volatile("" ::: "memory");
   AMIGA_PERF(phaseEnd(0);)
   draw_buffer ^= 1;
   while (dirty_count)
   {
      dirty[dirty_list[--dirty_count]] = 0;
   }
   while (foreground_count)
   {
      foreground_dirty[foreground_list[--foreground_count]] = 0;
   }
   while (saved_counts[draw_buffer])
   {
      saved = &saved_bobs[draw_buffer][--saved_counts[draw_buffer]];
      /* saved pixels already contain the foreground. */
      copyRows(saved->offset, saved_pixels[draw_buffer] + saved->pixels, saved->width, saved->rows, 1);
   }
   saved_used[draw_buffer] = 0;
   AMIGA_PERF(amigaPerfPhase(13);) /* bob restore */
   if (!amigaScrollSurfaceVisitRuns(&surface, &windows[draw_buffer], camera_x, camera_y, paintRun, 0))
   {
      return 0;
   }
   AMIGA_PERF(phaseEnd(1);)
   signature = uiSignature(game);
   for (index = 0; index < ui_counts[draw_buffer]; ++index)
   {
      /* the same ui layout moves with the camera; only what it uncovered needs tiles again. */
      kept = previous_ui[draw_buffer][index];
      kept.x += camera_x - ui_camera_x[draw_buffer];
      kept.y += camera_y - ui_camera_y[draw_buffer];
      if (signature == ui_signature[draw_buffer])
      {
         markUncovered(&previous_ui[draw_buffer][index], &kept);
      }
      else
      {
         markRectangle(&previous_ui[draw_buffer][index], 0, 1);
      }
   }
   markAnimatedCells();
   for (count = 0; count < dirty_count; count = run_end)
   {
      /* marks come from rectangles: neighbours later in the list paint as one run. */
      index = dirty_list[count];
      y = (DeceptusI16)(index / SURFACE_COLUMNS);
      x = (DeceptusI16)(index - y * SURFACE_COLUMNS);
      for (run_end = count + 1; run_end < dirty_count && dirty_list[run_end] == index + (run_end - count) &&
                                x + (DeceptusI16)(run_end - count) < SURFACE_COLUMNS;
           ++run_end)
      {
      }
      paintRun(
         0, windows[draw_buffer].column + x, windows[draw_buffer].column + x + (DeceptusI16)(run_end - count), windows[draw_buffer].row + y
      );
      for (run_cell = count; run_cell < run_end; ++run_cell)
      {
         if (clipCovered(1, windows[draw_buffer].column + x + (DeceptusI16)(run_cell - count), windows[draw_buffer].row + y))
         {
            markForeground(dirty_list[run_cell], 1);
         }
      }
   }
   /* cpu-painted cells may touch the ring start; bob saves can read across the guard. */
   repairGuard(0, 0);
   AMIGA_PERF(phaseEnd(2);)
   AMIGA_PERF(amigaPerfPhase(11);)
   for (index = bob_count; index; --index)
   {
      drawBob(&bobs[index - 1]);
   }
   AMIGA_PERF(amigaPerfPhase(12);)
   /* the cpu restores foreground over cells the last bob blit may still be writing. */
   waitBlitter();
   AMIGA_PERF(phaseEnd(3);)
   for (count = 0; count < foreground_count; ++count)
   {
      index = foreground_list[count];
      mark = foreground_dirty + index;
      y = (DeceptusI16)(index / SURFACE_COLUMNS);
      x = (DeceptusI16)(index - y * SURFACE_COLUMNS);
      x += windows[draw_buffer].column;
      y += windows[draw_buffer].row;
      if (*mark == 2 && x >= 0 && y >= 0 && amigaTileArchiveCell(tiles, (DeceptusU16)x, (DeceptusU16)y, color_ids, mask_ids))
      {
         maskedTile(x, y, color_ids, mask_ids);
      }
      for (clip = 0; clipCovered(1, x, y) && clip < graphics->clip_count; ++clip)
      {
         if (!clips[clip].enabled || !clips[clip].clip.plane)
         {
            continue;
         }
         cell = clipCell(&clips[clip], x, y, clips[clip].frame);
         if (cell)
         {
            clipTile(x, y, cell);
         }
      }
   }
   waitBlitter();
   AMIGA_PERF(phaseEnd(4);)
   ui_signature[draw_buffer] = signature;
   ui_camera_x[draw_buffer] = camera_x;
   ui_camera_y[draw_buffer] = camera_y;
   box_visible = prepareMessageBox(game);
   amigaOpeningUiSetDialogueOverlay(box_visible);
   cached = prepareUiMask(game, signature);
   ui_counts[draw_buffer] = amigaOpeningUiDrawSurface(
      game, (DeceptusU8*)pages[draw_buffer], &surface, camera_x, camera_y, clearRectangle, !cached, previous_ui[draw_buffer]
   );
   for (index = 0; cached && index < ui_band_count; ++index)
   {
      blitUiBand(&ui_bands[index], ui_mask + (DeceptusU32)index * UI_BAND_ROWS * UI_BAND_WORDS);
   }
   if (item_bob_used)
   {
      drawBob(&item_bob);
   }
   AMIGA_PERF(phaseEnd(5);)
   repairGuard(0, 0);
   if (error)
   {
      return 0;
   }
   AMIGA_PERF(amigaPerfPhase(14);) /* guard repair (waits for the blitter) */
   prepareSprites();
   AMIGA_PERF(amigaPerfPhase(15);) /* hardware sprites */
   prepareStorm(game);
   prepareCopper();
   AMIGA_PERF(phaseEnd(6);)
   AMIGA_PERF(amiga_phase_debug[7] += phase_mark - draw_started;)
   AMIGA_PERF(++amiga_phase_debug[8];)
   loading = 0;
   /* the page's sprite lines are blitted; they must be complete before the page shows. */
   waitBlitter();
   __asm__ volatile("" ::: "memory");
   pending_buffer = draw_buffer;
   last_ticks = game->campaign.engine.ticks;
   last_flags = game->flags;
   last_mode = game->campaign.mode;
   last_selection = game->selection;
   last_page = game->page;
   last_prompt = game->prompt;
   last_dialogue = game->dialogue;
   last_label = game->room_label_ticks != 0;
   return 1;
}
