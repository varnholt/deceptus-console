#include <assert.h>
#include <string.h>
#include "deceptus/enemies.h"
#include "deceptus/playersword.h"

static DeceptusEngine engine;
static DeceptusPlayerSword sword;

static void setup(void)
{
   memset(&engine, 0, sizeof(engine));
   deceptusPlayerSwordInit(&sword);
   engine.entities[0].x = DECEPTUS_FIXED(100);
   engine.entities[0].y = DECEPTUS_FIXED(100);
   engine.entities[0].grounded = 1;
   engine.entities[1].x = DECEPTUS_FIXED(150);
   engine.entities[1].y = DECEPTUS_FIXED(80);
   engine.entities[1].kind = DECEPTUS_BLOB_2;
   engine.entities[1].active = 1;
}

static void tick(DeceptusU16 count)
{
   while (count)
   {
      deceptusPlayerSwordTick(&sword, &engine, 0);
      --count;
   }
}

int main(void)
{
   DeceptusU16 attack;
   setup();
   deceptusPlayerSwordTick(&sword, &engine, DECEPTUS_ATTACK);
   assert(sword.ticks == 1 && sword.duration == 34);
   tick(11);
   assert(sword.damage[1] == 0);
   tick(1);
   assert(sword.damage[1] == 10 && sword.hit);
   tick(21);
   assert(sword.damage[1] == 10 && engine.entities[1].state != 3);
   /* the original blob energy is 30; each distinct swing applies damage10 once. */
   for (attack = 0; attack < 2; ++attack)
   {
      tick(1);
      deceptusPlayerSwordTick(&sword, &engine, DECEPTUS_ATTACK);
      tick(33);
   }
   assert(sword.damage[1] == 30 && engine.entities[1].state == 3);
   assert(engine.entities[1].timer == 0 && engine.entities[1].velocity_x == 0);
   tick(70);
   assert(sword.ticks == 0 && sword.damage[1] == 30);

   setup();
   engine.entities[0].facing_left = 1;
   deceptusPlayerSwordTick(&sword, &engine, DECEPTUS_ATTACK);
   tick(12);
   assert(sword.damage[1] == 0);
   deceptusPlayerSwordInit(&sword);
   engine.entities[1].x = DECEPTUS_FIXED(50);
   deceptusPlayerSwordTick(&sword, &engine, DECEPTUS_ATTACK);
   engine.entities[0].facing_left = 0;
   tick(12);
   assert(sword.damage[1] == 10 && sword.facing_left);

   setup();
   engine.entities[0].crouched = 1;
   engine.entities[1].y = DECEPTUS_FIXED(45);
   deceptusPlayerSwordTick(&sword, &engine, DECEPTUS_ATTACK);
   tick(12);
   assert(sword.duration == 24 && sword.damage[1] == 0);
   tick(12);
   assert(!sword.ticks);

   setup();
   engine.entities[0].grounded = 0;
   deceptusPlayerSwordTick(&sword, &engine, DECEPTUS_ATTACK);
   assert(sword.airborne && sword.duration == 54);
   tick(54);
   assert(!sword.ticks);
   engine.entities[0].in_water = 1;
   deceptusPlayerSwordTick(&sword, &engine, DECEPTUS_ATTACK);
   assert(!sword.ticks);
   deceptusPlayerSwordInit(&sword);
   assert(!sword.damage[1]);
   setup();
   engine.entities[1].kind = DECEPTUS_FROG_BLUE;
   engine.entities[1].y = DECEPTUS_FIXED(94);
   engine.projectiles[0].kind = DECEPTUS_BLUE_FIREBALL;
   engine.projectiles[0].owner = 1;
   engine.projectiles[0].active = 1;
   for (attack = 0; attack < 3; ++attack)
   {
      deceptusPlayerSwordTick(&sword, &engine, DECEPTUS_ATTACK);
      tick(12);
      assert(sword.damage[1] == (attack + 1) * 10);
      assert(engine.projectiles[0].active == (attack < 2));
      tick(22);
   }
   assert(engine.entities[1].state == 3 && engine.entities[1].pause_ticks == 0 && engine.entities[1].frame == 44);
   setup();
   engine.entities[1].kind = DECEPTUS_FROG_BLUE;
   engine.entities[1].x = DECEPTUS_FIXED(100);
   engine.entities[1].y = DECEPTUS_FIXED(94);
   deceptusPlayerSwordTick(&sword, &engine, DECEPTUS_ATTACK);
   tick(12);
   assert(!sword.damage[1]); /* source 16px upper-left hitbox is outside this swing. */
   setup();
   engine.entities[1].kind = DECEPTUS_ORB;
   deceptusPlayerSwordTick(&sword, &engine, DECEPTUS_ATTACK);
   tick(12);
   assert(!sword.damage[1] && engine.entities[1].active);
   {
      static const DeceptusMechanismDef wood = {1439, {120, 60, 48, 96}, 0, 0, 30, DECEPTUS_MECH_DESTRUCTIBLE, 1, 0};
      setup();
      assert(deceptusMechanismsInit(&engine.mechanisms, &wood, 1, 0, 0));
      for (attack = 0; attack < 3; ++attack)
      {
         deceptusPlayerSwordTick(&sword, &engine, DECEPTUS_ATTACK);
         tick(11);
         assert(engine.mechanisms.states[0].cooldown == 30 - attack * 10);
         tick(1);
         assert(engine.mechanisms.states[0].cooldown == 20 - attack * 10);
         tick(22);
      }
      assert(engine.mechanisms.states[0].triggered && !engine.mechanisms.states[0].active);
   }
   return 0;
}
