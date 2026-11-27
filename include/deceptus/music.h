#ifndef DECEPTUS_MUSIC_H
#define DECEPTUS_MUSIC_H
#include "deceptus/engine.h"

/* which track the game wants to hear, as the original's music zones decide it
 * (data/scripts/music_zones.lua): a level plays its default track until the player enters a music
 * zone, which switches to the zone's track for good; after a death the level starts again with the
 * zone's track only when the last checkpoint reached is one of the zone's. menus play the menu track.
 * tracks are numbered as tools/music_assets.py numbers them; 0 is silence. the platform plays what
 * DeceptusMusic.track names. */

typedef struct DeceptusMusicZone
{
   /* the checkpoints from which the level starts with the zone's track. */
   const DeceptusU16* checkpoints;
   DeceptusU16 sensor_id;
   DeceptusU8 checkpoint_count, track;
} DeceptusMusicZone;

typedef struct DeceptusMusic
{
   DeceptusU8 track;
   /* the player has entered the zone, or reached one of its checkpoints. */
   DeceptusU8 zone_reached;
   /* the last checkpoint reached is one of the zone's. */
   DeceptusU8 zone_checkpoint;
} DeceptusMusic;

/* a menu is open: the menu track plays. */
void deceptusMusicShowMenu(DeceptusMusic* music, DeceptusU8 menu_track);
/* after a gameplay tick in a room of a level with level_track and zone (0 for none); died tells
 * whether the player died in that tick. */
void deceptusMusicTick(
   DeceptusMusic* music,
   const DeceptusEngine* engine,
   DeceptusU8 level_track,
   const DeceptusMusicZone* zone,
   DeceptusU8 died
);

#endif
