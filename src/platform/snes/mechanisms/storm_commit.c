/* the S-CPU half of the thunderstorm (storm.h). the stagings may live in SA-1 BW-RAM, so they are
 * reached through their 24-bit address. */
#include <snes.h>
#include "storm.h"

static SnesStormStaging* const staging_table[SNES_STORM_STAGINGS] = {&snes_storm_stagings[0], &snes_storm_stagings[1]};
typedef char SnesStormTableComplete[(SNES_STORM_STAGINGS == 2) ? 1 : -1];

DeceptusU16 snesStormCommitLines(DeceptusU8 buffer)
{
   return staging_table[buffer]->pending ? SNES_STORM_COMMIT_LINES : 0;
}

void snesStormCommit(DeceptusU8 buffer)
{
   SnesStormStaging* staging;
   staging = staging_table[buffer];
   if (!staging->pending)
   {
      return;
   }
   staging->pending = 0;
   dmaCopyCGram((u8*)staging->colours, SNES_STORM_FIRST_COLOUR, SNES_STORM_COLOURS * 2);
}
