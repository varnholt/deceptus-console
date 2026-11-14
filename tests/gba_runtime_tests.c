#include "deceptus/types.h"
#include <assert.h>

deceptus_u32 deceptus_gba_unsigned_divide(deceptus_u32 numerator, deceptus_u32 denominator);

static void check(deceptus_u32 numerator, deceptus_u32 denominator)
{
    deceptus_u32 expected;
    expected = denominator ? numerator / denominator : 0;
    assert(deceptus_gba_unsigned_divide(numerator, denominator) == expected);
}

int main(void)
{
    deceptus_u32 edges[100], power, random, numerator, denominator;
    deceptus_u16 count, index, other;
    deceptus_u32 iteration;
    count = 0;
    edges[count++] = 0;
    edges[count++] = (deceptus_u32)0xffffffffUL;
    edges[count++] = (deceptus_u32)0xfffffffeUL;
    power = 1;
    for (index = 0; index < 32; ++index) {
        edges[count++] = power - 1;
        edges[count++] = power;
        edges[count++] = power + 1;
        power <<= 1;
    }
    for (index = 0; index < count; ++index) {
        for (other = 0; other < count; ++other) {
            check(edges[index], edges[other]);
        }
    }
    random = (deceptus_u32)0x6d2b79f5UL;
    for (iteration = 0; iteration < 100000; ++iteration) {
        random = random * (deceptus_u32)1664525UL + (deceptus_u32)1013904223UL;
        numerator = random;
        random = random * (deceptus_u32)1664525UL + (deceptus_u32)1013904223UL;
        denominator = random;
        check(numerator, denominator);
        /* small denominators exercise full-width quotients as well. */
        check(numerator, denominator & 255);
    }
    return 0;
}
