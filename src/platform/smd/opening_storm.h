/* the thunderstorm lights the room's sky colours (tools/storm_sky.py): prepared outside vblank,
 * written to CRAM after it. */
#define SMD_STORM_COLOURS 32

static DeceptusU16 smd_storm_colours[SMD_STORM_COLOURS];
/* the flash level CRAM shows, and whether smd_storm_colours still has to be written. */
static DeceptusU8 smd_storm_level, smd_storm_dirty;

/* the room's colours were just loaded unlit. */
static void smdStormReset(void)
{
   smd_storm_level = 0;
   smd_storm_dirty = 0;
}

/* one CRAM word (three bits per channel, from bit 1, 5 and 9) at a flash level. */
static DeceptusU16 smdStormColour(DeceptusU16 colour, DeceptusU8 level)
{
   return (DeceptusU16)((thunderstormChannel((DeceptusU8)((colour >> 1) & 7), 7, level) << 1) |
                        (thunderstormChannel((DeceptusU8)((colour >> 5) & 7), 7, level) << 5) |
                        (thunderstormChannel((DeceptusU8)((colour >> 9) & 7), 7, level) << 9));
}

static void smdStormPrepare(const DeceptusAdventure *game)
{
   const SmdStage *stage;
   DeceptusU8 level, index, entry;
   level = thunderstormLevel(&game->thunderstorm);
   if (level == smd_storm_level)
   {
      return;
   }
   smd_storm_level = level;
   stage = &smd_stages[game->campaign.room_index];
   memcpy(smd_storm_colours, stage->palette, sizeof(smd_storm_colours));
   for (index = 0; index < stage->storm_count; ++index)
   {
      entry = stage->storm_entries[index];
      smd_storm_colours[entry] = smdStormColour(smd_storm_colours[entry], level);
   }
   smd_storm_dirty = 1;
}

static void smdStormCommit(void)
{
   if (smd_storm_dirty)
   {
      PAL_setColors(0, smd_storm_colours, SMD_STORM_COLOURS, CPU);
      smd_storm_dirty = 0;
   }
}
