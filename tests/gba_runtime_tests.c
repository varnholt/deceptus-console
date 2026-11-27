#include <assert.h>
#include "deceptus/types.h"

DeceptusU32 deceptusGbaUnsignedDivide(DeceptusU32 numerator, DeceptusU32 denominator);

static void check(DeceptusU32 numerator, DeceptusU32 denominator)
{
   DeceptusU32 expected;
   expected = denominator ? numerator / denominator : 0;
   assert(deceptusGbaUnsignedDivide(numerator, denominator) == expected);
}

int main(void)
{
   DeceptusU32 edges[100], power, random, numerator, denominator;
   DeceptusU16 count, index, other;
   DeceptusU32 iteration;
   count = 0;
   edges[count++] = 0;
   edges[count++] = (DeceptusU32)0xffffffffUL;
   edges[count++] = (DeceptusU32)0xfffffffeUL;
   power = 1;
   for (index = 0; index < 32; ++index)
   {
      edges[count++] = power - 1;
      edges[count++] = power;
      edges[count++] = power + 1;
      power <<= 1;
   }
   for (index = 0; index < count; ++index)
   {
      for (other = 0; other < count; ++other)
      {
         check(edges[index], edges[other]);
      }
   }
   random = (DeceptusU32)0x6d2b79f5UL;
   for (iteration = 0; iteration < 100000; ++iteration)
   {
      random = random * (DeceptusU32)1664525UL + (DeceptusU32)1013904223UL;
      numerator = random;
      random = random * (DeceptusU32)1664525UL + (DeceptusU32)1013904223UL;
      denominator = random;
      check(numerator, denominator);
      /* small denominators exercise full-width quotients as well. */
      check(numerator, denominator & 255);
   }
   return 0;
}
