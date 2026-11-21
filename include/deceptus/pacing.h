#ifndef DECEPTUS_PACING_H
#define DECEPTUS_PACING_H

#include "deceptus/types.h"

/* the simulation is tuned for exactly 60 ticks per second. a backend measures how many display
 * frames passed since its previous presentation and runs the number of ticks this returns, so the
 * game keeps its speed when drawing takes longer than a frame and on 50 Hz displays.
 *
 * time behind the limit is discarded rather than banked: catching it up would make every
 * following frame heavier (more ticks, more camera travel) and the game would never recover.
 * below 60 / max_ticks presentations per second the game slows down instead. a backend whose
 * renderer streams the background incrementally keeps max_ticks low: each extra tick is extra
 * camera travel that the next presentation has to draw.
 *
 * builds with DECEPTUS_PACING_LOCKED run exactly one tick per presentation. the emulator route
 * verifiers need that: they replay ordinary inputs tick for tick, and a presentation that runs
 * several ticks would apply an input change late. */
#define DECEPTUS_TICKS_PER_SECOND 60

typedef struct DeceptusPacing
{
   DeceptusU16 phase;         /* display time not yet spent on a tick, in 1/(60 x display_hz) s */
   DeceptusU16 dropped_ticks; /* ticks discarded by the limit since init, for diagnostics */
   DeceptusU8 display_hz;     /* 60 (NTSC) or 50 (PAL) */
   DeceptusU8 max_ticks;      /* most ticks one presentation may run */
} DeceptusPacing;

void deceptusPacingInit(DeceptusPacing *pacing, DeceptusU8 display_hz, DeceptusU8 max_ticks);

/* forgets banked time. call after a room load or any other pause the game must not make up for. */
void deceptusPacingReset(DeceptusPacing *pacing);

/* ticks to run for a presentation that came elapsed_frames display frames after the previous one. */
DeceptusU8 deceptusPacingTicks(DeceptusPacing *pacing, DeceptusU16 elapsed_frames);

#endif
