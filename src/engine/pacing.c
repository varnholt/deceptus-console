#include "deceptus/pacing.h"

void deceptusPacingInit(DeceptusPacing *pacing, DeceptusU8 display_hz, DeceptusU8 max_ticks)
{
   pacing->phase = 0;
   pacing->dropped_ticks = 0;
   pacing->display_hz = display_hz ? display_hz : DECEPTUS_TICKS_PER_SECOND;
   pacing->max_ticks = max_ticks ? max_ticks : 1;
}

void deceptusPacingReset(DeceptusPacing *pacing)
{
   pacing->phase = 0;
}

DeceptusU8 deceptusPacingTicks(DeceptusPacing *pacing, DeceptusU16 elapsed_frames)
{
#ifdef DECEPTUS_PACING_LOCKED
   (void)pacing;
   (void)elapsed_frames;
   return 1;
#else
   DeceptusU16 ticks;
   /* a stall of seconds (a room load) is limited anyway; capping first keeps the sum in 16 bits. */
   if (elapsed_frames > 255)
   {
      elapsed_frames = 255;
   }
   /* repeated subtraction: no divide on the 65816, and a 68000 divu costs more for these counts. */
   pacing->phase += elapsed_frames * DECEPTUS_TICKS_PER_SECOND;
   ticks = 0;
   while (pacing->phase >= pacing->display_hz)
   {
      pacing->phase -= pacing->display_hz;
      ++ticks;
   }
   if (ticks > pacing->max_ticks)
   {
      pacing->dropped_ticks += ticks - pacing->max_ticks;
      pacing->phase = 0;
      ticks = pacing->max_ticks;
   }
   return (DeceptusU8)ticks;
#endif
}
