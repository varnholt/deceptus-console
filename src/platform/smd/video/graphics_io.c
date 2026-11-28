#include "opening_video.h"

DeceptusU8 smd_graphics_error;

DeceptusU16 smdGraphicsTile(DeceptusU16 logical)
{
   if (logical < 1408)
   {
      return logical;
   }
   if (logical < 1536)
   {
      return logical + 256;
   }
   return logical + 448;
}

/* runs during vblank for every tile a scroll brings in, so the copies are plain pointer loops (move.b
   with dbra on the 68000) with the bounds checked once per run. */
DeceptusU8 smdDecodeBlock(const SmdRomBlob* blob, DeceptusU16 block, DeceptusU8* destination)
{
   const DeceptusU8 *source, *end, *unit_source;
   DeceptusU8 *out, *out_end;
   DeceptusU16 count, bytes, unit, index;
   DeceptusU8 code, value;
   if (block >= blob->blocks)
   {
      smd_graphics_error = 1;
      return 0;
   }
   source = blob->data + blob->offsets[block];
   end = blob->data + blob->offsets[block + 1];
   out = destination;
   out_end = destination + blob->block_bytes;
   unit = blob->unit_bytes;
   while ((source < end) && (out < out_end))
   {
      code = *source++;
      count = (code < 128) ? code + 1 : code - 127;
      bytes = count * unit;
      if ((bytes > (DeceptusU16)(out_end - out)) || (source + ((code < 128) ? bytes : unit) > end))
      {
         smd_graphics_error = 1;
         return 0;
      }
      if (code < 128)
      {
         do
         {
            *out++ = *source++;
         } while (--bytes);
      }
      else if (unit == 1)
      {
         value = *source++;
         do
         {
            *out++ = value;
         } while (--count);
      }
      else
      {
         unit_source = source;
         do
         {
            for (index = 0; index < unit; ++index)
            {
               *out++ = unit_source[index];
            }
         } while (--count);
         source += unit;
      }
   }
   if ((out != out_end) || (source != end))
   {
      smd_graphics_error = 1;
      return 0;
   }
   return 1;
}

const DeceptusU8* smdCell(const SmdRomBlob* blob, DeceptusU16 row, DeceptusU16 column)
{
   const DeceptusU8 *source, *end;
   DeceptusU16 count, bytes;
   DeceptusU8 code;
   if (row >= blob->blocks || column >= blob->block_bytes / 4 || blob->unit_bytes != 4)
   {
      smd_graphics_error = 1;
      return 0;
   }
   source = blob->data + blob->offsets[row];
   end = blob->data + blob->offsets[row + 1];
   while (source < end)
   {
      code = *source++;
      count = code < 128 ? code + 1 : code - 127;
      bytes = code < 128 ? count * 4 : 4;
      if (source + bytes > end)
      {
         break;
      }
      if (column < count)
      {
         return source + (code < 128 ? column * 4 : 0);
      }
      column -= count;
      source += bytes;
   }
   smd_graphics_error = 1;
   return 0;
}
