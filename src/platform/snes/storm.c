/* the preparation half of the thunderstorm (storm.h): the room's colours with its lit sky entries
 * (tools/storm_sky.py) brightened to the flash level. */
#include "storm.h"
#include "snes_assets.h"
#include "deceptus/thunderstorm.h"

SnesStormStaging snes_storm_stagings[SNES_STORM_STAGINGS];
/* the flash level CGRAM shows once the staged colours are uploaded. */
static DeceptusU8 staged_level;

void snesStormReset(void)
{
   staged_level = 0;
}

static DeceptusU16 litColour(DeceptusU16 colour, DeceptusU8 level)
{
   return (DeceptusU16)(thunderstormChannel((DeceptusU8)(colour & 31), 31, level) |
                        ((DeceptusU16)thunderstormChannel((DeceptusU8)((colour >> 5) & 31), 31, level) << 5) |
                        ((DeceptusU16)thunderstormChannel((DeceptusU8)((colour >> 10) & 31), 31, level) << 10));
}

void snesStormStage(const DeceptusAdventure *game, DeceptusU8 buffer)
{
   const SnesStage *stage;
   const DeceptusU8 *palette;
   SnesStormStaging *staging;
   DeceptusU8 level, index, entry;
   staging = &snes_storm_stagings[buffer];
   staging->pending = 0;
   level = thunderstormLevel(&game->thunderstorm);
   if (level == staged_level)
   {
      return;
   }
   staged_level = level;
   stage = &snes_stages[game->campaign.room_index];
   palette = stage->palette + SNES_STORM_FIRST_COLOUR * 2;
   for (index = 0; index < SNES_STORM_COLOURS; ++index)
   {
      staging->colours[index] = (DeceptusU16)(palette[index * 2] | (palette[index * 2 + 1] << 8));
   }
   for (index = 0; index < stage->storm_count; ++index)
   {
      entry = (DeceptusU8)(stage->storm_entries[index] - SNES_STORM_FIRST_COLOUR);
      staging->colours[entry] = litColour(staging->colours[entry], level);
   }
   staging->pending = 1;
}
