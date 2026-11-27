#ifndef DECEPTUS_AMIGA_PERFCOUNT_H
#define DECEPTUS_AMIGA_PERFCOUNT_H

#include "deceptus/types.h"

/* per-frame timing and cache counters for measuring the renderer from a debugger attached to the
   emulator. builds made with AMIGA_PERFCOUNT=1 in the environment (tools/build_amiga.py adds
   -DAMIGA_PERFCOUNT) keep them; every other build compiles them out.

   amiga_phase_debug holds scanlines per draw phase: 0 swap wait, 1 scroll paint, 2 dirty and
   animated paint, 3 bobs, 4 foreground, 5 ui, 6 sprites and copper, 7 whole draw, 8 draw count,
   9 and 10 ui parts, 11 object list, 12 bob blits, 13 bob restore, 14 guard repair, 15 hardware
   sprites. amiga_debug slots 23-28 count painted cells, decoded sprite bytes, cell compositions and
   bank decodes, mask bank decodes, sprite frame decodes and drawn bobs. */
#ifdef AMIGA_PERFCOUNT
void amigaPerfPhase(DeceptusU8 phase);
#define AMIGA_PERF(statement) statement
#else
#define AMIGA_PERF(statement)
#endif

#endif
