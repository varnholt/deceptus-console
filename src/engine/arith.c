/* portable reference implementations of the primitives in include/arith.h. */
#include "arith.h"

DeceptusU32 arithSquareU16Reference(DeceptusU16 magnitude)
{
   return (DeceptusU32)magnitude * magnitude;
}

DeceptusI32 arithMulS16Reference(DeceptusI16 left, DeceptusI16 right)
{
   return (DeceptusI32)left * right;
}

DeceptusU32 arithMulU16Reference(DeceptusU16 left, DeceptusU16 right)
{
   return (DeceptusU32)left * right;
}

DeceptusI32 arithDivS32U16Reference(DeceptusI32 numerator, DeceptusU16 denominator)
{
   return numerator / (DeceptusI32)denominator;
}

DeceptusU16 arithRootU32Reference(DeceptusU32 value)
{
   DeceptusU32 result, bit;
   DeceptusU16 short_value, short_result, short_bit;
   if (value <= 65535UL)
   {
      /* the same digit-by-digit root in 16-bit words, which 16-bit cpus run without 32-bit shift and
         compare sequences; short rope links stay in this range. */
      short_value = (DeceptusU16)value;
      short_result = 0;
      short_bit = (DeceptusU16)1 << 14;
      while (short_bit > short_value)
      {
         short_bit >>= 2;
      }
      while (short_bit)
      {
         if (short_value >= short_result + short_bit)
         {
            short_value -= short_result + short_bit;
            short_result = (short_result >> 1) + short_bit;
         }
         else
         {
            short_result >>= 1;
         }
         short_bit >>= 2;
      }
      return short_result;
   }
   result = 0;
   bit = (DeceptusU32)1 << 30;
   while (bit > value)
   {
      bit >>= 2;
   }
   while (bit)
   {
      if (value >= result + bit)
      {
         value -= result + bit;
         result = (result >> 1) + bit;
      }
      else
      {
         result >>= 1;
      }
      bit >>= 2;
   }
   return (DeceptusU16)result;
}
