#include "deceptus/game.h"
#include "deceptus/logic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                                                                   \
   do                                                                                                                                      \
   {                                                                                                                                       \
      if (!(condition))                                                                                                                    \
      {                                                                                                                                    \
         fprintf(stderr, "line %d: %s\n", __LINE__, #condition);                                                                           \
         exit(1);                                                                                                                          \
      }                                                                                                                                    \
   } while (0)

static DeceptusMechanismSet mechanisms;
static const DeceptusMechanismDef switches[] = {{1, {0, 0, 72, 48}, 0, 1, 0, DECEPTUS_MECH_LEVER, 1, 0},
                                                {2, {80, 0, 24, 24}, 0, 0, 100, DECEPTUS_MECH_SPIKES, 1, 0}};
static const DeceptusU16 targets[] = {2};
static const DeceptusMechanismTiming switch_times[] = {{240, 180, 60, 240, 60, DECEPTUS_TIMED_ANIMATION},
                                                       {240, 180, 60, 240, 60, DECEPTUS_TIMED_ANIMATION}};
static const DeceptusMechanismDef platforms[] = {{3, {80, 160, 24, 24}, 0, 0, 0, DECEPTUS_MECH_COLLAPSING, 1, 0}};
static const DeceptusMechanismTiming platform_times[] = {{0, 0, 60, 240, 60, 0}};

static void tickMany(DeceptusU16 count, const DeceptusRect *body)
{
   while (count--)
   {
      deceptusMechanismsTick(&mechanisms, body, 0);
   }
}

int main(void)
{
   DeceptusRect lever = {24, 0, 16, 30};
   DeceptusRect trap = {80, 0, 16, 30};
   DeceptusRect away = {0, 100, 16, 30};
   DeceptusRect feet = {80, 130, 16, 30};
   DeceptusRect overlap = {80, 150, 16, 30};
   DeceptusU16 index;
   DeceptusU8 cells[32 * 28];
   DeceptusRoom room;
   DeceptusEngine engine;
   DeceptusVideoCaps video = {240, 160, 12};
   DeceptusMechanismTiming once = {0, 0, 2, 3, 2, DECEPTUS_COLLAPSE_ONCE_TOUCHED};
   const DeceptusMechanismDef block[] = {{4, {80, 160, 24, 24}, 0, 0, 0, DECEPTUS_MECH_BLOCK, 1, 0}};
   const DeceptusMechanismDef inverted[] = {{4, {80, 160, 24, 24}, 0, 0, 0, DECEPTUS_MECH_BLOCK, 1, DECEPTUS_MECH_INVERTED}};
   const DeceptusMechanismTiming interval[] = {{3, 100, 0, 0, 0, DECEPTUS_TIMED_ANIMATION | DECEPTUS_TIMED_INTERVAL}};
   const DeceptusU8 program[] = {1, 7, 1, 0, 2, 7, 2, 0, 0};

   CHECK(deceptusMechanismsInit(&mechanisms, switches, 2, targets, 1));
   deceptusMechanismsConfigure(&mechanisms, switch_times);
   deceptusMechanismsTick(&mechanisms, &lever, 1);
   CHECK(!mechanisms.states[0].enabled && !mechanisms.states[1].enabled);
   deceptusMechanismsTick(&mechanisms, &trap, 0);
   CHECK(mechanisms.event_count == 1 && mechanisms.events[0].kind == DECEPTUS_MECH_HIT);
   CHECK(mechanisms.states[1].frame == 32);
   tickMany(4, &trap);
   CHECK(mechanisms.states[1].frame == 35 && mechanisms.event_count == 1);
   tickMany(1, &trap);
   CHECK(mechanisms.states[1].frame == 36 && mechanisms.event_count == 0);
   tickMany(5, &away);
   CHECK(mechanisms.states[1].frame == 0 && mechanisms.states[0].frame == 21);
   deceptusMechanismsTick(&mechanisms, &lever, 1);
   CHECK(!mechanisms.states[0].enabled);
   tickMany(60, &away);
   deceptusMechanismsTick(&mechanisms, &lever, 1);
   CHECK(mechanisms.states[0].enabled);
   tickMany(50, &away);
   CHECK(mechanisms.states[1].frame == 32);
   for (index = 0; index < 8; ++index)
   {
      CHECK(deceptusMechanismEnable(&mechanisms, 2, index & 1));
   }
   CHECK(mechanisms.states[1].pending_count == 8);
   CHECK(!deceptusMechanismEnable(&mechanisms, 2, 0));
   CHECK(mechanisms.overflow && mechanisms.states[1].enabled);
   mechanisms.states[0].cooldown = 0;
   deceptusMechanismsTick(&mechanisms, &lever, 1);
   CHECK(mechanisms.overflow && mechanisms.states[0].enabled && mechanisms.states[1].enabled);
   mechanisms.states[1].pending_count = 8;
   mechanisms.states[1].enabled = 0;
   CHECK(deceptusLogicExecute(&mechanisms, program, sizeof(program), 0) == DECEPTUS_LOGIC_BUDGET);
   CHECK(mechanisms.states[0].enabled && !mechanisms.states[1].enabled);

   CHECK(deceptusMechanismsInit(&mechanisms, block, 1, 0, 0));
   deceptusMechanismsConfigure(&mechanisms, interval);
   CHECK(deceptusMechanismSolid(&mechanisms, &overlap));
   tickMany(3, &away);
   CHECK(!mechanisms.states[0].enabled && !deceptusMechanismSolid(&mechanisms, &overlap));
   tickMany(100, &away);
   CHECK(mechanisms.states[0].enabled && deceptusMechanismSolid(&mechanisms, &overlap));

   CHECK(deceptusMechanismsInit(&mechanisms, inverted, 1, 0, 0));
   deceptusMechanismsConfigure(&mechanisms, switch_times);
   CHECK(!deceptusMechanismSolid(&mechanisms, &overlap));
   CHECK(deceptusMechanismEnable(&mechanisms, 4, 0));
   tickMany(1, &away);
   CHECK(deceptusMechanismSolid(&mechanisms, &overlap));
   CHECK(deceptusMechanismEnable(&mechanisms, 4, 1));
   tickMany(25, &away);
   CHECK(!deceptusMechanismSolid(&mechanisms, &overlap));

   CHECK(deceptusMechanismsInit(&mechanisms, platforms, 1, 0, 0));
   deceptusMechanismsConfigure(&mechanisms, platform_times);
   tickMany(60, &feet);
   CHECK(mechanisms.states[0].active && mechanisms.states[0].timer == 60);
   tickMany(1, &away);
   CHECK(mechanisms.states[0].timer == 0);
   tickMany(61, &feet);
   CHECK(mechanisms.states[0].phase == 1 && !deceptusMechanismSolid(&mechanisms, &overlap));
   tickMany(250, &overlap);
   CHECK(mechanisms.states[0].phase == 1);
   tickMany(1, &away);
   CHECK(mechanisms.states[0].phase == 2);
   tickMany(60, &away);
   CHECK(!mechanisms.states[0].active);
   tickMany(1, &away);
   CHECK(mechanisms.states[0].active && mechanisms.states[0].phase == 0);
   CHECK(mechanisms.events[0].kind == DECEPTUS_MECH_RESTORED);
   CHECK(deceptusMechanismsInit(&mechanisms, platforms, 1, 0, 0));
   deceptusMechanismsConfigure(&mechanisms, &once);
   tickMany(1, &feet);
   tickMany(2, &away);
   CHECK(mechanisms.states[0].phase == 1);

   memset(&room, 0, sizeof(room));
   memset(cells, 0, sizeof(cells));
   room.collision = cells;
   room.columns = 32;
   room.rows = 28;
   room.spawn_x = 92;
   room.spawn_y = 160;
   room.patrol_x = -1;
   room.door_x = -1;
   room.mechanisms = platforms;
   room.mechanism_count = 1;
   room.mechanism_timings = platform_times;
   deceptusInit(&engine, &room, &video);
   for (index = 0; index < 60; ++index)
   {
      deceptusTick(&engine, 0);
   }
   CHECK(engine.entities[0].grounded && deceptusFloor(engine.entities[0].y) == 160);
   for (index = 0; index < 12; ++index)
   {
      deceptusTick(&engine, 0);
   }
   CHECK(!engine.entities[0].grounded && deceptusFloor(engine.entities[0].y) > 160);
   deceptusTick(&engine, DECEPTUS_RESTART);
   CHECK(engine.mechanisms.states[0].active && !engine.mechanisms.states[0].phase);
   puts("timed switches, deadly animation frames, bounded queues and collapsing platforms passed");
   return 0;
}
