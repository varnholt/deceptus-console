#include "video.h"
#include <exec/execbase.h>
#include <graphics/gfxbase.h>
#include <hardware/blit.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <hardware/intbits.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <stddef.h>
#include "../system/system.h"
#include "amiga_assets.h"

#define ROW_WORDS ((AMIGA_ROOM_WIDTH + 32) / 16)
#define PLANE_WORDS (ROW_WORDS * AMIGA_ROOM_HEIGHT)
#define BUFFER_WORDS (PLANE_WORDS * AMIGA_PLANES)
#define ACTOR_ROW_WORDS 3
#define ACTOR_HEIGHT 48
#define ACTOR_FRAME_WORDS (ACTOR_ROW_WORDS * ACTOR_HEIGHT * AMIGA_PLANES)
#define CHIP __attribute__((section(".MEMF_CHIP")))

static volatile struct Custom* const hardware = (struct Custom*)0xdff000;
static DeceptusU16 buffers[2][BUFFER_WORDS] CHIP;
static DeceptusU16 copper[2][128] CHIP;
typedef struct AmigaSavedBob
{
   DeceptusU16 offset;
   DeceptusU16 width;
   DeceptusU16 height;
} AmigaSavedBob;

static DeceptusU16 saved_pixels[2][AMIGA_BOB_CAPACITY][ACTOR_FRAME_WORDS] CHIP;
static AmigaSavedBob saved_bobs[2][AMIGA_BOB_CAPACITY];
static DeceptusU16 saved_counts[2];

static void waitBlitter(void);
static void initializeRoom(DeceptusU16* destination);
static DeceptusU16 draw_buffer;
static volatile DeceptusU32 video_frames;

static void __attribute__((interrupt)) verticalBlank(void)
{
   ++video_frames;
   hardware->intreq = INTF_VERTB;
   hardware->intreq = INTF_VERTB;
}

DeceptusU32 amigaVideoFrames(void)
{
   return video_frames;
}

void amigaVideoWait(void)
{
   DeceptusU32 before;
   before = video_frames;
   while (video_frames == before)
   {
   }
}

static void copperWrite(DeceptusU16** cursor, DeceptusU16 reg, DeceptusU16 value)
{
   *(*cursor)++ = reg;
   *(*cursor)++ = value;
}

static void prepareCopper(DeceptusU16 buffer, DeceptusI16 camera)
{
   DeceptusU16* cursor;
   DeceptusU16 plane, delay;
   DeceptusU32 address;
   cursor = copper[buffer];
   /* fetch one guard word before the visible window for fine scrolling. */
   copperWrite(&cursor, offsetof(struct Custom, diwstrt), 0x3c81);
   copperWrite(&cursor, offsetof(struct Custom, diwstop), 0x1cc1);
   copperWrite(&cursor, offsetof(struct Custom, ddfstrt), 0x0030);
   copperWrite(&cursor, offsetof(struct Custom, ddfstop), 0x00d0);
   copperWrite(&cursor, offsetof(struct Custom, bplcon0), 0x5200);
   delay = (16 - (camera & 15)) & 15;
   copperWrite(&cursor, offsetof(struct Custom, bplcon1), delay | (delay << 4));
   copperWrite(&cursor, offsetof(struct Custom, bplcon2), 0);
   copperWrite(&cursor, offsetof(struct Custom, bpl1mod), ROW_WORDS * AMIGA_PLANES * 2 - 42);
   copperWrite(&cursor, offsetof(struct Custom, bpl2mod), ROW_WORDS * AMIGA_PLANES * 2 - 42);
   for (plane = 0; plane < AMIGA_PLANES; ++plane)
   {
      address = (DeceptusU32)(buffers[buffer] + plane * ROW_WORDS + (camera + 15) / 16);
      copperWrite(&cursor, offsetof(struct Custom, bplpt) + plane * 4, address >> 16);
      copperWrite(&cursor, offsetof(struct Custom, bplpt) + plane * 4 + 2, address);
   }
   for (plane = 0; plane < 32; ++plane)
   {
      copperWrite(&cursor, offsetof(struct Custom, color) + plane * 2, amiga_palette[plane]);
   }
   copperWrite(&cursor, 0xffff, 0xfffe);
}

int amigaVideoInit(void)
{
   DeceptusU16 buffer;
   if (!amigaSystemOpen())
   {
      return 0;
   }
   for (buffer = 0; buffer < 2; ++buffer)
   {
      prepareCopper(buffer, 0);
   }
   amigaSystemTakeDisplay(verticalBlank, copper[0]);
   for (buffer = 0; buffer < 2; ++buffer)
   {
      initializeRoom(buffers[buffer]);
   }
   waitBlitter();
   return 1;
}

void amigaVideoClose(void)
{
   amigaVideoWait();
   amigaSystemClose();
}

static void waitBlitter(void)
{
   /* the initial read also supports the original Agnus busy-bit timing. */
   (void)hardware->dmaconr;
   while (hardware->dmaconr & DMAF_BLTDONE)
   {
   }
}

static void initializeRoom(DeceptusU16* destination)
{
   DeceptusU16 x, y, tile;
   for (y = 0; y < AMIGA_ROOM_HEIGHT / 16; ++y)
   {
      for (x = 0; x < AMIGA_ROOM_WIDTH / 16; ++x)
      {
         tile = amiga_tile_map[y * (AMIGA_ROOM_WIDTH / 16) + x];
         waitBlitter();
         hardware->bltcon0 = SRCA | DEST | A_TO_D;
         hardware->bltcon1 = 0;
         hardware->bltafwm = 0xffff;
         hardware->bltalwm = 0xffff;
         hardware->bltamod = 0;
         hardware->bltdmod = ROW_WORDS * 2 - 2;
         hardware->bltapt = (APTR)(amiga_tiles + tile * 16 * AMIGA_PLANES);
         hardware->bltdpt = destination + y * 16 * AMIGA_PLANES * ROW_WORDS + x + 1;
         hardware->bltsize = ((16 * AMIGA_PLANES) << 6) | 1;
      }
   }
}

static void saveActor(DeceptusU16 buffer, DeceptusU16 index, DeceptusI16 x, DeceptusI16 y)
{
   DeceptusI16 first, last, top, bottom;
   AmigaSavedBob* saved;
   saved = &saved_bobs[buffer][index];
   saved->height = 0;
   if (x <= -32 || x >= AMIGA_ROOM_WIDTH || y <= -ACTOR_HEIGHT || y >= AMIGA_ROOM_HEIGHT)
   {
      return;
   }
   first = x < 0 ? 0 : x / 16;
   last = (x + 31) / 16;
   if (last >= AMIGA_ROOM_WIDTH / 16)
   {
      last = AMIGA_ROOM_WIDTH / 16 - 1;
   }
   top = y < 0 ? 0 : y;
   bottom = y + ACTOR_HEIGHT > AMIGA_ROOM_HEIGHT ? AMIGA_ROOM_HEIGHT : y + ACTOR_HEIGHT;
   saved->width = last - first + 1;
   saved->height = (bottom - top) * AMIGA_PLANES;
   saved->offset = top * ROW_WORDS * AMIGA_PLANES + first + 1;
   waitBlitter();
   hardware->bltcon0 = SRCA | DEST | A_TO_D;
   hardware->bltcon1 = 0;
   hardware->bltafwm = 0xffff;
   hardware->bltalwm = 0xffff;
   hardware->bltamod = ROW_WORDS * 2 - saved->width * 2;
   hardware->bltdmod = ACTOR_ROW_WORDS * 2 - saved->width * 2;
   hardware->bltapt = buffers[buffer] + saved->offset;
   hardware->bltdpt = saved_pixels[buffer][index];
   hardware->bltsize = (saved->height << 6) | saved->width;
}

static void restoreActor(DeceptusU16 buffer, DeceptusU16 index)
{
   const AmigaSavedBob* saved;
   saved = &saved_bobs[buffer][index];
   if (!saved->height)
   {
      return;
   }
   waitBlitter();
   hardware->bltcon0 = SRCA | DEST | A_TO_D;
   hardware->bltcon1 = 0;
   hardware->bltafwm = 0xffff;
   hardware->bltalwm = 0xffff;
   hardware->bltamod = ACTOR_ROW_WORDS * 2 - saved->width * 2;
   hardware->bltdmod = ROW_WORDS * 2 - saved->width * 2;
   hardware->bltapt = saved_pixels[buffer][index];
   hardware->bltdpt = buffers[buffer] + saved->offset;
   hardware->bltsize = (saved->height << 6) | saved->width;
}

static void drawClippedActor(DeceptusU16* destination, const DeceptusDraw* draw, DeceptusI16 x, DeceptusI16 y)
{
   DeceptusU16 row, plane, word, slot, shift, mask_word, pixel_word;
   DeceptusI16 target_row, target_word, first;
   DeceptusU32 mask, pixels;
   const DeceptusU16 *source, *mask_source;
   if (draw->frame >= AMIGA_ACTOR_FRAMES)
   {
      return;
   }
   slot = draw->frame * 2 + (draw->flip_x ? 1 : 0);
   mask_source = amiga_masks + slot * ACTOR_FRAME_WORDS;
   source = amiga_actors + slot * ACTOR_FRAME_WORDS;
   first = x < 0 ? -((-x + 15) / 16) : x / 16;
   shift = x & 15;
   for (row = 0; row < 48; ++row)
   {
      target_row = y + row;
      if (target_row < 0 || target_row >= AMIGA_ROOM_HEIGHT)
      {
         continue;
      }
      mask = ((DeceptusU32)mask_source[row * ACTOR_ROW_WORDS * AMIGA_PLANES] << 16) | mask_source[row * ACTOR_ROW_WORDS * AMIGA_PLANES + 1];
      for (plane = 0; plane < AMIGA_PLANES; ++plane)
      {
         pixels = ((DeceptusU32)source[row * ACTOR_ROW_WORDS * AMIGA_PLANES + plane * ACTOR_ROW_WORDS] << 16) |
                  source[row * ACTOR_ROW_WORDS * AMIGA_PLANES + plane * ACTOR_ROW_WORDS + 1];
         for (word = 0; word < 3; ++word)
         {
            target_word = first + word;
            if (target_word < 0 || target_word >= AMIGA_ROOM_WIDTH / 16)
            {
               continue;
            }
            if (word == 0)
            {
               mask_word = (mask >> shift) >> 16;
               pixel_word = (pixels >> shift) >> 16;
            }
            else if (word == 1)
            {
               mask_word = mask >> shift;
               pixel_word = pixels >> shift;
            }
            else
            {
               mask_word = shift ? mask << (16 - shift) : 0;
               pixel_word = shift ? pixels << (16 - shift) : 0;
            }
            slot = (target_row * AMIGA_PLANES + plane) * ROW_WORDS + target_word + 1;
            destination[slot] = (destination[slot] & ~mask_word) | pixel_word;
         }
      }
   }
}

static void drawActor(DeceptusU16* destination, const DeceptusDraw* draw, DeceptusI16 x, DeceptusI16 y)
{
   DeceptusI16 first, top, height;
   DeceptusU16 slot, shift;
   DeceptusU16* target;
   const DeceptusU16 *source, *mask;
   if (draw->frame >= AMIGA_ACTOR_FRAMES || y >= AMIGA_ROOM_HEIGHT || y <= -48)
   {
      return;
   }
   first = x < 0 ? -((-x + 15) / 16) : x / 16;
   if (first < -1 || first + 3 >= ROW_WORDS)
   {
      waitBlitter();
      drawClippedActor(destination, draw, x, y);
      return;
   }
   top = y < 0 ? -y : 0;
   height = y + 48 > AMIGA_ROOM_HEIGHT ? AMIGA_ROOM_HEIGHT - y - top : 48 - top;
   slot = draw->frame * 2 + (draw->flip_x ? 1 : 0);
   shift = (x & 15) << 12;
   source = amiga_actors + slot * ACTOR_FRAME_WORDS + top * ACTOR_ROW_WORDS * AMIGA_PLANES;
   mask = amiga_masks + slot * ACTOR_FRAME_WORDS + top * ACTOR_ROW_WORDS * AMIGA_PLANES;
   target = destination + (y + top) * ROW_WORDS * AMIGA_PLANES + first + 1;
   waitBlitter();
   /* OCS cookie-cut: copy B where mask A is set, otherwise preserve C. */
   hardware->bltcon0 = shift | SRCA | SRCB | SRCC | DEST | 0xca;
   hardware->bltcon1 = shift;
   hardware->bltafwm = 0xffff;
   hardware->bltalwm = 0;
   hardware->bltamod = 0;
   hardware->bltbmod = 0;
   hardware->bltcmod = ROW_WORDS * 2 - 6;
   hardware->bltdmod = ROW_WORDS * 2 - 6;
   hardware->bltapt = (APTR)mask;
   hardware->bltbpt = (APTR)source;
   hardware->bltcpt = target;
   hardware->bltdpt = target;
   hardware->bltsize = ((height * AMIGA_PLANES) << 6) | 3;
}

void amigaVideoDraw(const DeceptusFrame* frame)
{
   DeceptusU16 index, count;
   const DeceptusDraw* draw;
   draw_buffer ^= 1;
   /* undo in reverse order because a later BOB may have saved an earlier one. */
   index = saved_counts[draw_buffer];
   while (index)
   {
      --index;
      restoreActor(draw_buffer, index);
   }
   count = frame->count < AMIGA_BOB_CAPACITY ? frame->count : AMIGA_BOB_CAPACITY;
   for (index = 0; index < count; ++index)
   {
      draw = &frame->draws[index];
      saveActor(draw_buffer, index, draw->x + frame->camera_x, draw->y + frame->camera_y);
      drawActor(buffers[draw_buffer], draw, draw->x + frame->camera_x, draw->y + frame->camera_y);
   }
   saved_counts[draw_buffer] = count;
   waitBlitter();
   prepareCopper(draw_buffer, frame->camera_x);
   amigaVideoWait();
   hardware->cop1lc = (ULONG)copper[draw_buffer];
   hardware->copjmp1 = 0;
}
