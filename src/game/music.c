/* the original's music zones for the consoles (include/deceptus/music.h). */
#include "deceptus/music.h"

static DeceptusU8 isZoneCheckpoint(const DeceptusMusicZone* zone, DeceptusU16 checkpoint_id)
{
   DeceptusU8 index;
   for (index = 0; index < zone->checkpoint_count; ++index)
   {
      if (zone->checkpoints[index] == checkpoint_id)
      {
         return 1;
      }
   }
   return 0;
}

static void followZoneEvents(DeceptusMusic* music, const DeceptusEngine* engine, const DeceptusMusicZone* zone)
{
   const DeceptusMechanismEvent* event;
   DeceptusU8 index;
   for (index = 0; index < engine->mechanisms.event_count; ++index)
   {
      event = &engine->mechanisms.events[index];
      if ((event->kind == DECEPTUS_MECH_ENTER) && (event->source_id == zone->sensor_id))
      {
         music->zone_reached = 1;
      }
   }
   for (index = 0; index < engine->event_count; ++index)
   {
      if (engine->events[index].kind == DECEPTUS_EVENT_CHECKPOINT)
      {
         music->zone_checkpoint = isZoneCheckpoint(zone, engine->checkpoint_id);
         if (music->zone_checkpoint)
         {
            music->zone_reached = 1;
         }
      }
   }
}

void deceptusMusicShowMenu(DeceptusMusic* music, DeceptusU8 menu_track)
{
   music->track = menu_track;
}

void deceptusMusicTick(
   DeceptusMusic* music,
   const DeceptusEngine* engine,
   DeceptusU8 level_track,
   const DeceptusMusicZone* zone,
   DeceptusU8 died
)
{
   if (zone == 0)
   {
      music->track = level_track;
      return;
   }
   followZoneEvents(music, engine, zone);
   /* the original restarts the level: its start track follows the last checkpoint. */
   if (died)
   {
      music->zone_reached = music->zone_checkpoint;
   }
   music->track = music->zone_reached ? zone->track : level_track;
}
