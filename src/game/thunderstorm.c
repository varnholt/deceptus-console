/* original counterpart: src/game/layers/thunderstormoverlay.cpp, its lightning and silence cycle.
 * the flash levels come from tools/thunderstorm_curve.py. */
#include "deceptus/thunderstorm.h"
#include "thunderstormcurve.h"

#if THUNDERSTORM_LEVELS != THUNDERSTORM_MAX_LEVEL
#error the generated thunderstorm curve and thunderstorm.h disagree on the levels
#endif

void thunderstormTick(Thunderstorm* storm, DeceptusU8 active)
{
   if (!active)
   {
      storm->active = 0;
      return;
   }
   if (!storm->active)
   {
      /* level.lua strikes as the storm breaks: lightning at full strength. */
      storm->active = 1;
      storm->tick = THUNDERSTORM_LIGHTNING_START;
      return;
   }
   ++storm->tick;
   if (storm->tick == THUNDERSTORM_CURVE_TICKS)
   {
      storm->tick = 0;
   }
}

DeceptusU8 thunderstormLevel(const Thunderstorm* storm)
{
   if (!storm->active)
   {
      return 0;
   }
   return thunderstorm_curve[storm->tick];
}

DeceptusU8 thunderstormChannel(DeceptusU8 channel, DeceptusU8 maximum, DeceptusU8 level)
{
   DeceptusU16 grey;
   grey = (DeceptusU16)((DeceptusU16)level * maximum / THUNDERSTORM_MAX_LEVEL);
   if (grey >= channel)
   {
      return (DeceptusU8)(channel + (grey - channel) * level / THUNDERSTORM_MAX_LEVEL);
   }
   return (DeceptusU8)(channel - (channel - grey) * level / THUNDERSTORM_MAX_LEVEL);
}
