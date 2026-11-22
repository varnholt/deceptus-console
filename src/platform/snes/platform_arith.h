#ifndef PLATFORM_ARITH_H
#define PLATFORM_ARITH_H

#include "deceptus/types.h"

/* 65816 versions of the primitives in include/arith.h, in engine_math.asm. the compiler pays a
   library call for every 16x16 product, 32-bit shift and 32-bit division; these use the cpu's 8x8
   multiplier and hand-written loops. */

DeceptusU32 snesSquareU16(DeceptusU16 magnitude);
DeceptusI32 snesMulS16(DeceptusI16 left, DeceptusI16 right);
DeceptusI32 snesDivS32U16(DeceptusI32 numerator, DeceptusU16 denominator);
DeceptusU16 snesRootU32(DeceptusU32 value);

#define arithSquareU16(magnitude) snesSquareU16(magnitude)
#define arithMulS16(left, right) snesMulS16((left), (right))
#define arithDivS32U16(numerator, denominator) snesDivS32U16((numerator), (denominator))
#define arithRootU32(value) snesRootU32(value)

#endif
