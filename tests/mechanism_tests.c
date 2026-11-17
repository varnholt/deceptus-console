#include "deceptus/game.h"
#include "deceptus/logic.h"
#include "deceptus/physics.h"
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

static const DeceptusMechanismDef definitions[] = {{10, {24, 144, 32, 48}, 0, 1, 0, DECEPTUS_MECH_LEVER, 1, 0},
                                                   {20, {80, 144, 8, 48}, 0, 0, 0, DECEPTUS_MECH_BLOCK, 1, 0},
                                                   {30, {120, 144, 32, 48}, 0, 0, 1, DECEPTUS_MECH_CHECKPOINT, 1, 0},
                                                   {40, {0, 0, 16, 16}, 0, 0, 0, DECEPTUS_MECH_SENSOR, 1, 0},
                                                   {50, {176, 144, 8, 48}, 0, 0, 1, DECEPTUS_MECH_DAMAGE, 1, 0}};
static const DeceptusU16 targets[] = {20};

int main(void)
{
   DeceptusMechanismSet mechanisms;
   DeceptusRect player = {32, 160, 16, 30};
   DeceptusRect block = {80, 160, 8, 8};
   DeceptusU8 cells[32 * 28];
   DeceptusRoom room;
   DeceptusEntity body;
   DeceptusEngine engine;
   DeceptusVideoCaps video = {256, 224, DECEPTUS_DRAW_CAPACITY};
   const DeceptusU8 malformed[] = {1, 7, 20, 0, 1, 7, 255, 255, 0};
   const DeceptusU8 looping[] = {11, 0, 0};
   const DeceptusU8 branch[] = {3, 0, 10, 12, 0, 2, 7, 20, 0, 11, 16, 0, 1, 7, 20, 0, 0};
   const DeceptusU8 boolean_actions[] = {3, 0, 3, 1, 4, 5, 7, 20, 0, 3, 0, 3, 1, 6, 8, 20, 0, 9, 10, 0, 0};
   DeceptusU16 index;

   CHECK(deceptusMechanismsInit(&mechanisms, definitions, 5, targets, 1));
   CHECK(deceptusMechanismSolid(&mechanisms, &block));
   deceptusMechanismsTick(&mechanisms, &player, 1);
   CHECK(!mechanisms.states[0].enabled && !mechanisms.states[1].enabled);
   CHECK(!deceptusMechanismSolid(&mechanisms, &block));
   deceptusMechanismsTick(&mechanisms, &player, 0);
   CHECK(!mechanisms.states[1].enabled);
   deceptusMechanismsTick(&mechanisms, &player, 1);
   CHECK(mechanisms.states[1].enabled);
   CHECK(deceptusLogicExecute(&mechanisms, malformed, sizeof(malformed), 0) == DECEPTUS_LOGIC_MISSING_TARGET);
   CHECK(mechanisms.states[1].enabled);
   CHECK(deceptusLogicExecute(&mechanisms, looping, sizeof(looping), 0) == DECEPTUS_LOGIC_BUDGET);
   for (index = 0; index < 4; ++index)
   {
      mechanisms.states[0].enabled = 0;
      CHECK(deceptusLogicExecute(&mechanisms, boolean_actions, sizeof(boolean_actions), (DeceptusU8)index) == DECEPTUS_LOGIC_OK);
      CHECK(mechanisms.states[1].enabled == ((index & 1) && !(index & 2)));
      CHECK(mechanisms.states[1].visible == ((index & 1) || (index & 2)));
      CHECK(mechanisms.states[0].enabled);
   }
   CHECK(deceptusLogicExecute(&mechanisms, branch, sizeof(branch), 0) == DECEPTUS_LOGIC_OK);
   CHECK(!mechanisms.states[1].enabled);
   CHECK(deceptusLogicExecute(&mechanisms, branch, sizeof(branch), 1) == DECEPTUS_LOGIC_OK);
   CHECK(mechanisms.states[1].enabled);
   player.x = 0;
   player.y = 0;
   deceptusMechanismsTick(&mechanisms, &player, 0);
   CHECK(mechanisms.event_count == 1 && mechanisms.events[0].kind == DECEPTUS_MECH_ENTER);
   deceptusMechanismsTick(&mechanisms, &player, 0);
   CHECK(mechanisms.event_count == 0);
   player.x = 64;
   deceptusMechanismsTick(&mechanisms, &player, 0);
   CHECK(mechanisms.event_count == 1 && mechanisms.events[0].kind == DECEPTUS_MECH_LEAVE);

   /* a live mechanism participates in collision, not just an enabled flag. */
   memset(cells, 0, sizeof(cells));
   memset(&room, 0, sizeof(room));
   memset(&body, 0, sizeof(body));
   room.collision = cells;
   room.columns = 32;
   room.rows = 28;
   body.x = DECEPTUS_FIXED(71);
   body.y = DECEPTUS_FIXED(192);
   body.velocity_x = DECEPTUS_FIXED(2);
   deceptusMoveHorizontalDynamic(&room, &body, 8, 30, &mechanisms);
   CHECK(deceptusFloor(body.x) == 72 && body.velocity_x == 0);
   deceptusMechanismEnable(&mechanisms, 20, 0);
   body.velocity_x = DECEPTUS_FIXED(2);
   deceptusMoveHorizontalDynamic(&room, &body, 8, 30, &mechanisms);
   CHECK(deceptusFloor(body.x) == 74);

   /* imported checkpoints become the respawn point through actual overlap. */
   for (index = 0; index < 32; ++index)
   {
      cells[24 * 32 + index] = DECEPTUS_SOLID;
   }
   room.spawn_x = 40;
   room.spawn_y = 192;
   room.patrol_x = 220;
   room.patrol_y = 192;
   room.door_x = 240;
   room.door_y = 192;
   room.mechanisms = definitions;
   room.mechanism_count = 5;
   room.mechanism_targets = targets;
   room.mechanism_target_count = 1;
   deceptusInit(&engine, &room, &video);
   engine.entities[1].active = 0;
   engine.entities[0].x = DECEPTUS_FIXED(128);
   deceptusTick(&engine, 0);
   CHECK(engine.checkpoint_id == 30 && engine.checkpoint_x == 136 && engine.checkpoint_y == 192);
   engine.entities[0].x = DECEPTUS_FIXED(240);
   deceptusTick(&engine, DECEPTUS_UP);
   CHECK(deceptusFloor(engine.entities[0].x) == 136);
   engine.entities[0].x = DECEPTUS_FIXED(180);
   deceptusTick(&engine, 0);
   CHECK(engine.health == 2 && deceptusFloor(engine.entities[0].x) == 136);
   CHECK(!deceptusMechanismsInit(&mechanisms, definitions, DECEPTUS_MECHANISM_CAPACITY + 1, targets, 1));
   CHECK(mechanisms.count == 0);
   {
      static const DeceptusMechanismDef trap[] = {{90, {80, 160, 24, 24}, 0, 0, 100, DECEPTUS_MECH_SPIKES, 1, 0}};
      CHECK(deceptusMechanismsInit(&mechanisms, trap, 1, 0, 0));
      player.x = 80;
      player.y = 160;
      deceptusMechanismsTick(&mechanisms, &player, 0);
      CHECK(mechanisms.event_count == 1 && mechanisms.events[0].kind == DECEPTUS_MECH_HIT);
      CHECK(mechanisms.events[0].value == 100);
      CHECK(!deceptusMechanismSolid(&mechanisms, &player));
      deceptusMechanismEnable(&mechanisms, 90, 0);
      deceptusMechanismsTick(&mechanisms, &player, 0);
      CHECK(mechanisms.event_count == 0);
   }
   {
      static const DeceptusMechanismDef inverted[] = {
         {1585, {24, 144, 32, 48}, 0, 1, 0, DECEPTUS_MECH_LEVER, 0, DECEPTUS_MECH_INVERT_TARGETS},
         {1586, {80, 144, 8, 48}, 0, 0, 0, DECEPTUS_MECH_BLOCK, 1, 0}};
      static const DeceptusU16 inverted_targets[] = {1586};
      CHECK(deceptusMechanismsInit(&mechanisms, inverted, 2, inverted_targets, 1));
      CHECK(!mechanisms.states[0].enabled && mechanisms.states[1].enabled);
      player.x = 32;
      player.y = 160;
      deceptusMechanismsTick(&mechanisms, &player, 1);
      CHECK(mechanisms.states[0].enabled && !mechanisms.states[1].enabled);
      deceptusMechanismsTick(&mechanisms, &player, 0);
      deceptusMechanismsTick(&mechanisms, &player, 1);
      CHECK(!mechanisms.states[0].enabled && mechanisms.states[1].enabled);
   }
   {
      static const DeceptusMechanismDef gates[] = {
         {425, {0, 24, 24, 72}, 0, 0, 7, DECEPTUS_MECH_DOOR, 0, 0},
         {1439, {80, 24, 48, 96}, 0, 0, 30, DECEPTUS_MECH_DESTRUCTIBLE, 1, DECEPTUS_MECH_RIGHT_ALIGNED},
         {493, {160, 24, 48, 36}, 0, 0, 0, DECEPTUS_MECH_CANNON_BODY, 1, 0}};
      CHECK(deceptusMechanismsInit(&mechanisms, gates, 3, 0, 0));
      player.x = 160;
      player.y = 24;
      CHECK(deceptusMechanismSolid(&mechanisms, &player));
      CHECK(!deceptusMechanismProjectileSolid(&mechanisms, &player));
      CHECK(!mechanisms.states[2].visible);
      player.x = 0;
      player.y = 24;
      CHECK(deceptusMechanismSolid(&mechanisms, &player));
      deceptusMechanismEnable(&mechanisms, 425, 1);
      for (index = 0; index < 68; ++index)
      {
         deceptusMechanismsTick(&mechanisms, &player, 0);
      }
      CHECK(mechanisms.states[0].frame == 56 && mechanisms.states[0].active);
      deceptusMechanismsTick(&mechanisms, &player, 0);
      CHECK(!deceptusMechanismSolid(&mechanisms, &player) && !mechanisms.states[0].visible);
      deceptusMechanismEnable(&mechanisms, 425, 0);
      for (index = 0; index < 68; ++index)
      {
         deceptusMechanismsTick(&mechanisms, &player, 0);
      }
      CHECK(mechanisms.states[0].frame == 113 && !mechanisms.states[0].active);
      deceptusMechanismsTick(&mechanisms, &player, 0);
      CHECK(deceptusMechanismSolid(&mechanisms, &player) && mechanisms.states[0].frame == 0);
      player.x = 80;
      CHECK(deceptusMechanismSolid(&mechanisms, &player));
      CHECK(deceptusMechanismProjectileSolid(&mechanisms, &player));
      CHECK(!deceptusMechanismHit(&mechanisms, 425, 100));
      CHECK(deceptusMechanismHit(&mechanisms, 1439, 10));
      CHECK(deceptusMechanismHit(&mechanisms, 1439, 10));
      CHECK(mechanisms.states[1].cooldown == 10 && deceptusMechanismSolid(&mechanisms, &player));
      CHECK(deceptusMechanismHit(&mechanisms, 1439, 10));
      CHECK(!deceptusMechanismSolid(&mechanisms, &player));
      CHECK(!deceptusMechanismHit(&mechanisms, 1439, 10));
      for (index = 0; index < 74; ++index)
      {
         deceptusMechanismsTick(&mechanisms, &player, 0);
      }
      CHECK(mechanisms.states[1].frame == 49 && mechanisms.states[1].triggered);
   }
   puts("mechanism, original gates, dynamic collision, checkpoint and bounded logic checks passed");
   return 0;
}
