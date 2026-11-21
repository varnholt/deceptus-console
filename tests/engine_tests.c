#include "deceptus/game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                                                                   \
   do                                                                                                                                      \
   {                                                                                                                                       \
      if (!(condition))                                                                                                                    \
      {                                                                                                                                    \
         fprintf(stderr, "failure at line %d: %s\n", __LINE__, #condition);                                                                \
         exit(1);                                                                                                                          \
      }                                                                                                                                    \
   } while (0)

static DeceptusU8 cells[64 * 28];
static DeceptusRoom room = {cells, 64, 28, 40, 192, 300, 192, 464, 192};
static DeceptusVideoCaps video = {256, 224, DECEPTUS_DRAW_CAPACITY};
static DeceptusEngine engine;

static void reset(void)
{
   DeceptusU16 column;
   memset(cells, 0, sizeof(cells));
   for (column = 0; column < 64; ++column)
   {
      cells[24 * 64 + column] = DECEPTUS_SOLID;
   }
   deceptusInit(&engine, &room, &video);
   engine.entities[1].active = 0;
   deceptusTick(&engine, 0);
}

static void run(DeceptusU16 ticks, DeceptusU16 buttons)
{
   while (ticks)
   {
      deceptusTick(&engine, buttons);
      --ticks;
   }
}

static void testContactInvulnerability(void)
{
   static DeceptusRoom contact_room;
   static const DeceptusMechanismDef damage = {100, {24, 144, 32, 48}, 0, 0, 4, DECEPTUS_MECH_DAMAGE, 1, 0};
   reset();
   contact_room = room;
   contact_room.patrol_x = -1;
   contact_room.door_x = -1;
   contact_room.max_health = 100;
   contact_room.mechanisms = &damage;
   contact_room.mechanism_count = 1;
   deceptusInit(&engine, &contact_room, &video);
   deceptusTick(&engine, 0);
   CHECK(engine.health == 96 && engine.hurt_ticks == 60);
   run(59, 0);
   CHECK(engine.health == 96 && engine.hurt_ticks == 1);
   deceptusTick(&engine, 0);
   CHECK(engine.health == 92 && engine.hurt_ticks == 60);
}

int main(void)
{
   DeceptusFixed held_height, released_height;
   DeceptusU16 index, ticks;
   DeceptusFrame frame;
   CHECK(sizeof(DeceptusI8) == 1 && sizeof(DeceptusI16) == 2 && sizeof(DeceptusI32) == 4);
   CHECK(deceptusFloor(-1) == -1 && deceptusFloor(-16) == -1 && deceptusFloor(-17) == -2);
   CHECK(deceptusFloor(-32768) == -2048 && deceptusFloor(32767) == 2047);
   reset();
   CHECK(engine.entities[0].grounded);
   CHECK(deceptusFloor(engine.entities[0].y) == 192);
   run(30, DECEPTUS_RIGHT);
   CHECK(deceptusFloor(engine.entities[0].x) > 85);
   CHECK(deceptusFloor(engine.entities[0].y) == 192);
   run(20, DECEPTUS_LEFT | DECEPTUS_RIGHT);
   CHECK(engine.entities[0].velocity_x == 0);

   /* held jump must rise higher than a tapped jump. */
   reset();
   run(12, DECEPTUS_JUMP);
   held_height = engine.entities[0].y;
   reset();
   run(1, DECEPTUS_JUMP);
   run(11, 0);
   released_height = engine.entities[0].y;
   CHECK(held_height < released_height - DECEPTUS_FIXED(8));
   run(90, 0);
   CHECK(engine.entities[0].grounded && deceptusFloor(engine.entities[0].y) == 192);

   /* wall collisions stop motion and leave the body outside the solid cell. */
   reset();
   for (index = 16; index < 24; ++index)
   {
      cells[index * 64 + 10] = DECEPTUS_SOLID;
   }
   run(90, DECEPTUS_RIGHT);
   CHECK(deceptusFloor(engine.entities[0].x) == 72);

   /* one-way platforms catch downward motion, and allow upward passage. */
   reset();
   for (index = 0; index < 64; ++index)
   {
      cells[16 * 64 + index] = DECEPTUS_ONE_WAY;
   }
   engine.entities[0].y = DECEPTUS_FIXED(120);
   engine.entities[0].grounded = 0;
   run(30, 0);
   CHECK(deceptusFloor(engine.entities[0].y) == 128);
   engine.entities[0].y = DECEPTUS_FIXED(160);
   engine.entities[0].velocity_y = -62;
   engine.entities[0].grounded = 0;
   run(8, DECEPTUS_JUMP);
   CHECK(deceptusFloor(engine.entities[0].y) < 140);

   /* six-frame coyote time and landing jump buffering. */
   reset();
   engine.entities[0].grounded = 0;
   engine.entities[0].coyote_ticks = 4;
   engine.entities[0].y = DECEPTUS_FIXED(180);
   deceptusTick(&engine, DECEPTUS_JUMP);
   CHECK(engine.entities[0].velocity_y < 0);
   reset();
   engine.entities[0].grounded = 0;
   engine.entities[0].coyote_ticks = 0;
   engine.entities[0].y = DECEPTUS_FIXED(190);
   engine.entities[0].velocity_y = 25;
   deceptusTick(&engine, DECEPTUS_JUMP);
   run(2, DECEPTUS_JUMP);
   CHECK(engine.entities[0].velocity_y < 0);

   reset();
   deceptusTick(&engine, DECEPTUS_PAUSE);
   ticks = engine.ticks;
   run(30, DECEPTUS_RIGHT);
   CHECK(engine.ticks == ticks && engine.paused);
   deceptusTick(&engine, DECEPTUS_PAUSE);
   CHECK(!engine.paused);
   deceptusTick(&engine, DECEPTUS_RESTART);
   CHECK(engine.video.width == 256 && engine.video.height == 224);
   CHECK(deceptusFloor(engine.entities[0].x) == room.spawn_x);

   /* contact damage returns to the checkpoint and has an invulnerability window. */
   reset();
   engine.entities[1].active = 1;
   engine.entities[1].x = engine.entities[0].x;
   engine.entities[1].y = engine.entities[0].y;
   deceptusTick(&engine, 0);
   CHECK(engine.health == 2 && engine.hurt_ticks > 0);
   CHECK(engine.events[0].kind == DECEPTUS_EVENT_HURT);
   deceptusTick(&engine, 0);
   CHECK(engine.health == 2);

   reset();
   engine.entities[0].x = DECEPTUS_FIXED(room.door_x);
   deceptusTick(&engine, DECEPTUS_UP);
   CHECK(engine.door_entries == 1);
   CHECK(engine.event_count == 1 && engine.events[0].kind == DECEPTUS_EVENT_DOOR);
   CHECK(deceptusFloor(engine.entities[0].x) == room.spawn_x);

   /* fixed pools and render queues must never overflow. */
   reset();
   engine.entities[1].active = 1;
   for (index = 3; index < DECEPTUS_ENTITY_CAPACITY; ++index)
   {
      CHECK(deceptusSpawn(&engine, DECEPTUS_PATROL, 80, 192) != 0);
   }
   CHECK(deceptusSpawn(&engine, DECEPTUS_PATROL, 80, 192) == 0);
   engine.video.max_draws = 2;
   deceptusRender(&engine, &frame);
   CHECK(frame.count == 2);
   engine.entities[0].x = DECEPTUS_FIXED(510);
   deceptusTick(&engine, 0);
   CHECK(engine.camera_x == 256);
   /* the smaller GBA viewport follows vertically without rescaling the world. */
   reset();
   engine.video.width = 240;
   engine.video.height = 160;
   deceptusTick(&engine, 0);
   CHECK(engine.camera_y == 64);
   deceptusRender(&engine, &frame);
   CHECK(frame.camera_y == 64 && frame.draws[0].y == 80);
   testContactInvulnerability();
   printf("engine checks passed; state uses %lu bytes on this host\n", (unsigned long)sizeof(engine));
   return 0;
}
