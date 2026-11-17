#include "deceptus/compression.h"

DeceptusU8 deceptusRleDecode(const DeceptusU8 *source, DeceptusU32 source_size, DeceptusU8 *destination, DeceptusU32 destination_size,
                             DeceptusU8 unit_bytes)
{
   DeceptusU32 input, output, bytes;
   DeceptusU16 count;
   DeceptusU8 control, field;
   if ((unit_bytes != 1 && unit_bytes != 2 && unit_bytes != 4) || destination_size % unit_bytes || (source_size && !source) ||
       (destination_size && !destination))
   {
      return 0;
   }
   input = output = 0;
   while (input < source_size)
   {
      control = source[input++];
      count = (control & 127) + 1;
      bytes = (DeceptusU32)count * unit_bytes;
      if (bytes > destination_size - output)
      {
         return 0;
      }
      if (control & 128)
      {
         if (unit_bytes > source_size - input)
         {
            return 0;
         }
         while (count--)
         {
            for (field = 0; field < unit_bytes; ++field)
            {
               destination[output++] = source[input + field];
            }
         }
         input += unit_bytes;
      }
      else
      {
         if (bytes > source_size - input)
         {
            return 0;
         }
         while (bytes--)
         {
            destination[output++] = source[input++];
         }
      }
   }
   return output == destination_size;
}

/* independent blocks: https://github.com/lz4/lz4/blob/dev/doc/lz4_Block_format.md */
static DeceptusU8 lz4Length(const DeceptusU8 *source, DeceptusU32 source_size, DeceptusU32 *input, DeceptusU32 *length, DeceptusU32 limit)
{
   DeceptusU8 extension;
   if (*length > limit)
   {
      return 0;
   }
   do
   {
      if (*input >= source_size)
      {
         return 0;
      }
      extension = source[(*input)++];
      if (extension > limit - *length)
      {
         return 0;
      }
      *length += extension;
   } while (extension == 255);
   return 1;
}

DeceptusU8 deceptusLz4Decode(const DeceptusU8 *source, DeceptusU32 source_size, DeceptusU8 *destination, DeceptusU32 destination_size)
{
   DeceptusU32 input, output, length, offset;
   DeceptusU8 token;
   if (!source || !source_size || (destination_size && !destination))
   {
      return 0;
   }
   input = output = 0;
   while (input < source_size)
   {
      token = source[input++];
      length = token >> 4;
      if (length == 15 && !lz4Length(source, source_size, &input, &length, destination_size - output))
      {
         return 0;
      }
      if (length > source_size - input || length > destination_size - output)
      {
         return 0;
      }
      while (length--)
      {
         destination[output++] = source[input++];
      }
      if (input == source_size)
      {
         return output == destination_size;
      }
      if (source_size - input < 2)
      {
         return 0;
      }
      offset = (DeceptusU32)source[input] | ((DeceptusU32)source[input + 1] << 8);
      input += 2;
      if (!offset || offset > output)
      {
         return 0;
      }
      length = (token & 15) + 4;
      if ((token & 15) == 15 && !lz4Length(source, source_size, &input, &length, destination_size - output))
      {
         return 0;
      }
      if (length > destination_size - output)
      {
         return 0;
      }
      while (length--)
      {
         destination[output] = destination[output - offset];
         ++output;
      }
   }
   return 0;
}
