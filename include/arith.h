#ifndef ARITH_H
#define ARITH_H

/* arithmetic primitives the shared engine uses on its hot paths.
 *
 * the contract of each, which every implementation must meet exactly:
 *    arithSquareU16(m)        m * m for 0 <= m <= 65535, as an unsigned 32-bit value.
 *    arithMulS16(a, b)        a * b for 16-bit signed a and b, as a signed 32-bit value.
 *    arithDivS32U16(n, d)     n / d truncated toward zero, as c division, for d > 0.
 *    arithRootU32(v)          floor(sqrt(v)) for any unsigned 32-bit v.
 *
 * the *Reference versions below are portable c and the default. a platform whose cpu does better
 * defines PLATFORM_ARITH on its compiler command line and provides platform_arith.h in its own
 * source folder (on the include path), defining the four names; it may map any of them to the
 * reference. its tests compare each one with the reference. */

#include "deceptus/types.h"

DeceptusU32 arithSquareU16Reference(DeceptusU16 magnitude);
DeceptusI32 arithMulS16Reference(DeceptusI16 left, DeceptusI16 right);
DeceptusI32 arithDivS32U16Reference(DeceptusI32 numerator, DeceptusU16 denominator);
DeceptusU16 arithRootU32Reference(DeceptusU32 value);

#ifdef PLATFORM_ARITH
#include "platform_arith.h"
#else
#define arithSquareU16(magnitude) ((DeceptusU32)(DeceptusU16)(magnitude) * (DeceptusU16)(magnitude))
#define arithMulS16(left, right) ((DeceptusI32)(DeceptusI16)(left) * (DeceptusI16)(right))
#define arithDivS32U16(numerator, denominator) ((DeceptusI32)(numerator) / (DeceptusI32)(DeceptusU16)(denominator))
#define arithRootU32(value) arithRootU32Reference(value)
#endif

#endif
