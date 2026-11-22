#ifndef PLATFORM_ARITH_H
#define PLATFORM_ARITH_H

#include "deceptus/types.h"

/* 68000 versions of the primitives in include/arith.h. gcc can leave even a 16x16 product or a
   division that fits the hardware divide to its 32-bit library calls; these use mulu, muls and divs
   where they are exact and the reference otherwise. */

static __inline__ DeceptusU32 arithSquareU16(DeceptusU16 magnitude)
{
   DeceptusU32 result;
   result = magnitude;
   __asm__("mulu.w %0,%0" : "+d"(result) : : "cc");
   return result;
}

static __inline__ DeceptusI32 arithMulS16(DeceptusI16 left, DeceptusI16 right)
{
   DeceptusI32 result;
   result = left;
   __asm__("muls.w %1,%0" : "+d"(result) : "d"(right) : "cc");
   return result;
}

static __inline__ DeceptusI32 arithDivS32U16(DeceptusI32 numerator, DeceptusU16 denominator)
{
   DeceptusI32 result, range;
   /* divs.w needs a quotient that fits 16 signed bits: |numerator| < 32768 * denominator, written
      as a shift since the multiply would be a library call itself. */
   range = (DeceptusI32)denominator << 15;
   if (denominator <= 32767U && numerator > -range && numerator < range)
   {
      result = numerator;
      __asm__("divs.w %1,%0" : "+d"(result) : "dmi"((DeceptusI16)denominator) : "cc");
      return (DeceptusI16)result;
   }
   return numerator / (DeceptusI32)denominator;
}

#define arithRootU32(value) arithRootU32Reference(value)

#endif
