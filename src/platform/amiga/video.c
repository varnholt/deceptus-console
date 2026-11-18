#include "video.h"
#include "amiga_assets.h"
#include "system.h"
#include <exec/execbase.h>
#include <graphics/gfxbase.h>
#include <hardware/blit.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <hardware/intbits.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <stddef.h>
#include <string.h>

#define ROW_WORDS ((AMIGA_ROOM_WIDTH + 32) / 16)
#define PLANE_WORDS (ROW_WORDS * AMIGA_ROOM_HEIGHT)
#define BUFFER_WORDS (PLANE_WORDS * AMIGA_PLANES)
#define CHIP __attribute__((section(".MEMF_CHIP")))

static volatile struct Custom *const hardware = (struct Custom *)0xdff000;
static DeceptusU16 buffers[2][BUFFER_WORDS] CHIP;
static DeceptusU16 copper[2][128] CHIP;
static DeceptusFrame previous[2];
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

static void copperWrite(DeceptusU16 **cursor, DeceptusU16 reg, DeceptusU16 value)
{
   *(*cursor)++ = reg;
   *(*cursor)++ = value;
}

static void prepareCopper(DeceptusU16 buffer, DeceptusI16 camera)
{
   DeceptusU16 *cursor;
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
   copperWrite(&cursor, offsetof(struct Custom, bpl1mod), ROW_WORDS * 2 - 42);
   copperWrite(&cursor, offsetof(struct Custom, bpl2mod), ROW_WORDS * 2 - 42);
   for (plane = 0; plane < AMIGA_PLANES; ++plane)
   {
      address = (DeceptusU32)(buffers[buffer] + plane * PLANE_WORDS + (camera + 15) / 16);
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
      memcpy(buffers[buffer], amiga_background, sizeof(buffers[buffer]));
      prepareCopper(buffer, 0);
   }
   amigaSystemTakeDisplay(verticalBlank, copper[0]);
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

static void restoreActor(DeceptusU16 *destination, DeceptusI16 x, DeceptusI16 y)
{
   DeceptusI16 first, last, top, bottom;
   DeceptusU16 plane, offset, width;
   first = x < 0 ? 0 : x / 16;
   last = (x + 31) / 16;
   if (last >= AMIGA_ROOM_WIDTH / 16)
   {
      last = AMIGA_ROOM_WIDTH / 16 - 1;
   }
   top = y < 0 ? 0 : y;
   bottom = y + 48 > AMIGA_ROOM_HEIGHT ? AMIGA_ROOM_HEIGHT : y + 48;
   if (first > last || top >= bottom)
   {
      return;
   }
   width = last - first + 1;
   offset = top * ROW_WORDS + first + 1;
   waitBlitter();
   hardware->bltcon0 = SRCA | DEST | (A_TO_D);
   hardware->bltcon1 = 0;
   hardware->bltafwm = 0xffff;
   hardware->bltalwm = 0xffff;
   hardware->bltamod = ROW_WORDS * 2 - width * 2;
   hardware->bltdmod = ROW_WORDS * 2 - width * 2;
   for (plane = 0; plane < AMIGA_PLANES; ++plane)
   {
      waitBlitter();
      hardware->bltapt = (APTR)(amiga_background + offset + plane * PLANE_WORDS);
      hardware->bltdpt = destination + offset + plane * PLANE_WORDS;
      hardware->bltsize = ((bottom - top) << 6) | width;
   }
}

static void drawClippedActor(DeceptusU16 *destination, const DeceptusDraw *draw, DeceptusI16 x, DeceptusI16 y)
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
   mask_source = amiga_masks + slot * 144;
   source = amiga_actors + slot * 720;
   first = x < 0 ? -((-x + 15) / 16) : x / 16;
   shift = x & 15;
   for (row = 0; row < 48; ++row)
   {
      target_row = y + row;
      if (target_row < 0 || target_row >= AMIGA_ROOM_HEIGHT)
      {
         continue;
      }
      mask = ((DeceptusU32)mask_source[row * 3] << 16) | mask_source[row * 3 + 1];
      for (plane = 0; plane < AMIGA_PLANES; ++plane)
      {
         pixels = ((DeceptusU32)source[plane * 144 + row * 3] << 16) | source[plane * 144 + row * 3 + 1];
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
            slot = plane * PLANE_WORDS + target_row * ROW_WORDS + target_word + 1;
            destination[slot] = (destination[slot] & ~mask_word) | pixel_word;
         }
      }
   }
}

static void drawActor(DeceptusU16 *destination, const DeceptusDraw *draw, DeceptusI16 x, DeceptusI16 y)
{
   DeceptusI16 first, top, height;
   DeceptusU16 plane, slot, shift;
   DeceptusU16 *target;
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
   source = amiga_actors + slot * 720 + top * 3;
   mask = amiga_masks + slot * 144 + top * 3;
   target = destination + (y + top) * ROW_WORDS + first + 1;
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
   for (plane = 0; plane < AMIGA_PLANES; ++plane)
   {
      waitBlitter();
      hardware->bltapt = (APTR)mask;
      hardware->bltbpt = (APTR)(source + plane * 144);
      hardware->bltcpt = target + plane * PLANE_WORDS;
      hardware->bltdpt = target + plane * PLANE_WORDS;
      hardware->bltsize = (height << 6) | 3;
   }
}

void amigaVideoDraw(const DeceptusFrame *frame)
{
   DeceptusU16 index;
   const DeceptusDraw *draw;
   DeceptusFrame *old;
   draw_buffer ^= 1;
   old = &previous[draw_buffer];
   for (index = 0; index < old->count; ++index)
   {
      draw = &old->draws[index];
      restoreActor(buffers[draw_buffer], draw->x + old->camera_x, draw->y + old->camera_y);
   }
   for (index = 0; index < frame->count; ++index)
   {
      draw = &frame->draws[index];
      drawActor(buffers[draw_buffer], draw, draw->x + frame->camera_x, draw->y + frame->camera_y);
   }
   waitBlitter();
   *old = *frame;
   prepareCopper(draw_buffer, frame->camera_x);
   amigaVideoWait();
   hardware->cop1lc = (ULONG)copper[draw_buffer];
   hardware->copjmp1 = 0;
}
