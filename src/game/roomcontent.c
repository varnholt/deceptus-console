#include "deceptus/roomcontent.h"
#include <string.h>

static DeceptusU16 readWord(const DeceptusU8 **cursor)
{
   DeceptusU16 value;
   value = (DeceptusU16)((DeceptusU16)(*cursor)[0] << 8) | (*cursor)[1];
   *cursor += 2;
   return value;
}

static DeceptusI16 readSignedWord(const DeceptusU8 **cursor)
{
   DeceptusU16 value;
   value = readWord(cursor);
   if (value & 32768U)
   {
      return (DeceptusI16)(-1 - (DeceptusI16)(65535U - value));
   }
   return (DeceptusI16)value;
}

DeceptusU8 deceptusRoomContentLoad(DeceptusRoomContent *content, const DeceptusU8 *bytes, DeceptusU32 size)
{
   DeceptusRoom room;
   const DeceptusU8 *cursor;
   DeceptusMechanismDef *mechanism;
   DeceptusMechanismTiming *timing;
   DeceptusEnemyDef *enemy;
   DeceptusU32 collision_size, expected;
   DeceptusU16 index, field;
   DeceptusU8 has_water;
   memset(&content->room, 0, sizeof(content->room));
   if (!bytes || size < 35 || bytes[0] != 'D' || bytes[1] != 'R' || bytes[2] != 'M' || bytes[3] != '1')
   {
      return 0;
   }
   memset(&room, 0, sizeof(room));
   cursor = bytes + 4;
   room.columns = readWord(&cursor);
   room.rows = readWord(&cursor);
   room.spawn_x = readSignedWord(&cursor);
   room.spawn_y = readSignedWord(&cursor);
   room.patrol_x = readSignedWord(&cursor);
   room.patrol_y = readSignedWord(&cursor);
   room.door_x = readSignedWord(&cursor);
   room.door_y = readSignedWord(&cursor);
   room.mechanism_count = readWord(&cursor);
   room.mechanism_target_count = readWord(&cursor);
   room.enemy_count = readWord(&cursor);
   room.enemy_path_count = readWord(&cursor);
   room.max_health = *cursor++;
   room.skills = *cursor++;
   room.player_gravity = *cursor++;
   room.player_gravity_fraction = *cursor++;
   room.player_speed = *cursor++;
   room.player_fall_gravity = *cursor++;
   has_water = *cursor++;
   if (!room.columns || !room.rows || room.columns > 255 || room.rows > 255 || has_water > 1 ||
       room.mechanism_count > content->mechanism_capacity || room.mechanism_target_count > content->target_capacity ||
       room.enemy_count > content->enemy_capacity || room.enemy_path_count > content->path_capacity ||
       (room.mechanism_count && (!content->mechanisms || !content->timings)) || (room.mechanism_target_count && !content->targets) ||
       (room.enemy_count && !content->enemies) || (room.enemy_path_count && !content->paths))
   {
      return 0;
   }
   collision_size = (DeceptusU32)room.columns * room.rows;
   expected = 35 + collision_size + (DeceptusU32)room.mechanism_count * 30 + (DeceptusU32)room.mechanism_target_count * 2 +
              (DeceptusU32)room.enemy_count * 35 + (DeceptusU32)room.enemy_path_count * 4 + (has_water ? 26 : 0);
   if (expected != size)
   {
      return 0;
   }
   room.collision = cursor;
   cursor += collision_size;
   for (index = 0; index < room.mechanism_count; ++index)
   {
      mechanism = &content->mechanisms[index];
      mechanism->source_id = readWord(&cursor);
      mechanism->bounds.x = readSignedWord(&cursor);
      mechanism->bounds.y = readSignedWord(&cursor);
      mechanism->bounds.width = readSignedWord(&cursor);
      mechanism->bounds.height = readSignedWord(&cursor);
      mechanism->target_first = readWord(&cursor);
      mechanism->target_count = readWord(&cursor);
      mechanism->value = readWord(&cursor);
      mechanism->kind = *cursor++;
      mechanism->enabled = *cursor++;
      mechanism->flags = *cursor++;
      if (mechanism->target_first > room.mechanism_target_count ||
          mechanism->target_count > room.mechanism_target_count - mechanism->target_first)
      {
         return 0;
      }
      timing = &content->timings[index];
      timing->on_ticks = readWord(&cursor);
      timing->off_ticks = readWord(&cursor);
      timing->collapse_ticks = readWord(&cursor);
      timing->respawn_ticks = readWord(&cursor);
      timing->fade_ticks = readWord(&cursor);
      timing->flags = *cursor++;
   }
   for (index = 0; index < room.mechanism_target_count; ++index)
   {
      content->targets[index] = readWord(&cursor);
   }
   for (index = 0; index < room.enemy_count; ++index)
   {
      enemy = &content->enemies[index];
      enemy->source_id = readWord(&cursor);
      enemy->x = readSignedWord(&cursor);
      enemy->y = readSignedWord(&cursor);
      enemy->path_first = readWord(&cursor);
      enemy->path_count = readWord(&cursor);
      enemy->time_offset = readWord(&cursor);
      enemy->speed = readSignedWord(&cursor);
      enemy->acceleration = readSignedWord(&cursor);
      enemy->damage = readWord(&cursor);
      enemy->period = readWord(&cursor);
      for (field = 0; field < 3; ++field)
      {
         enemy->delays[field] = readWord(&cursor);
      }
      enemy->kind = *cursor++;
      enemy->alignment = *cursor++;
      for (field = 0; field < 5; ++field)
      {
         enemy->cycles[field] = *cursor++;
      }
      enemy->animation_fps = *cursor++;
      enemy->idle_ticks = *cursor++;
      if (enemy->path_first > room.enemy_path_count || enemy->path_count > room.enemy_path_count - enemy->path_first)
      {
         return 0;
      }
   }
   for (index = 0; index < room.enemy_path_count; ++index)
   {
      content->paths[index].x = readSignedWord(&cursor);
      content->paths[index].y = readSignedWord(&cursor);
   }
   if (has_water)
   {
      content->water.speed = *cursor++;
      content->water.rise_speed = *cursor++;
      content->water.sink_speed = *cursor++;
      content->water.stroke_delay_ticks = *cursor++;
      content->water.gravity = *cursor++;
      content->water.stroke_force = *cursor++;
      memcpy(content->water.splash_durations, cursor, 20);
      room.water = &content->water;
   }
   room.mechanisms = content->mechanisms;
   room.mechanism_timings = content->timings;
   room.mechanism_targets = content->targets;
   room.enemy_definitions = content->enemies;
   room.enemy_paths = content->paths;
   content->room = room;
   return 1;
}
