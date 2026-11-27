#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "deceptus/enemies.h"
#include "deceptus/game.h"
#include "deceptus/physics.h"
#ifdef DECEPTUS_HAVE_LUA_REFERENCE
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
static unsigned reference_tick, reference_count;
static unsigned reference_shots[64];
static int noop(lua_State* state)
{
   (void)state;
   return 0;
}
static unsigned reference_blob_frame;
static unsigned reference_cannon_frame, reference_cannon_last_shot;
static double reference_orb_x, reference_orb_y, reference_frog_offset_x, reference_frog_offset_y;
static unsigned reference_orb_frame, reference_frog_frame, reference_frog_orb_frame, reference_frog_visible;
static DeceptusU16 reference_frog_seed;
static int recordOrbTransform(lua_State* state)
{
   reference_orb_x = lua_tonumber(state, 1);
   reference_orb_y = lua_tonumber(state, 2);
   return 0;
}
static int recordOrbSprite(lua_State* state)
{
   reference_orb_frame = (unsigned)lua_tointeger(state, 2) / 120;
   return 0;
}
static int recordFrogSprite(lua_State* state)
{
   unsigned x, y, row, column;
   x = (unsigned)lua_tointeger(state, 2);
   y = (unsigned)lua_tointeger(state, 3);
   if (lua_tointeger(state, 1) == 1)
   {
      reference_frog_orb_frame = x / 120;
   }
   else if (lua_tointeger(state, 4) == 72)
   {
      reference_frog_frame = 44 + x / 72;
   }
   else
   {
      row = y / 48;
      column = (x - 768) / 48;
      reference_frog_frame = (row / 3) * 22 + ((row % 3) == 0 ? column : (row % 3) == 1 ? 8 + column : 16 + column);
   }
   return 0;
}
static int recordFrogVisible(lua_State* state)
{
   if (lua_tointeger(state, 1) == 1)
   {
      reference_frog_visible = lua_toboolean(state, 2);
   }
   return 0;
}
static int recordFrogOffset(lua_State* state)
{
   if (lua_tointeger(state, 1) == 1)
   {
      reference_frog_offset_x = lua_tonumber(state, 2);
      reference_frog_offset_y = lua_tonumber(state, 3);
   }
   return 0;
}
static int frogReferenceRandom(lua_State* state)
{
   reference_frog_seed = (DeceptusU16)(reference_frog_seed * 17U + 31U);
   lua_pushinteger(state, (reference_frog_seed >> 8) % 20 + 1);
   return 1;
}
static int recordCannonSprite(lua_State* state)
{
   unsigned row, column;
   column = (unsigned)lua_tointeger(state, 2) / 144;
   row = (unsigned)lua_tointeger(state, 3) / 72;
   reference_cannon_frame = (row / 5) * 43;
   row %= 5;
   reference_cannon_frame += row ? 3 + (row - 1) * 12 + column : column;
   return 0;
}
static int recordCannonShot(lua_State* state)
{
   (void)state;
   /* LuaNode::useWeapon delegates to Gun::useInIntervals (>1000ms). */
   if (reference_tick > reference_cannon_last_shot + 60 && reference_count < 64)
   {
      reference_shots[reference_count++] = reference_tick;
      reference_cannon_last_shot = reference_tick;
   }
   return 0;
}
static int recordBlobSprite(lua_State* state)
{
   static const unsigned first[6] = {0, 15, 31, 0, 0, 47};
   unsigned row, column;
   column = (unsigned)lua_tointeger(state, 2) / 72;
   row = (unsigned)lua_tointeger(state, 3) / 72;
   if (row < 6)
   {
      reference_blob_frame = first[row] + column;
   }
   return 0;
}
static int recordShot(lua_State* state)
{
   (void)state;
   if (reference_count < 64)
   {
      reference_shots[reference_count++] = reference_tick;
   }
   return 0;
}
#endif
#define CHECK(value)                                         \
   do                                                        \
   {                                                         \
      if (!(value))                                          \
      {                                                      \
         fprintf(stderr, "line %d: %s\n", __LINE__, #value); \
         exit(1);                                            \
      }                                                      \
   } while (0)
static DeceptusU8 cells[32 * 28];
static DeceptusEngine engine;
static DeceptusRoom room;
static const DeceptusVideoCaps video = {240, 160, DECEPTUS_DRAW_CAPACITY};
static const DeceptusEnemyDef trap = {1059, 96, 0, 0, 0, 0, 96, 0, 60, 180, {60, 75, 90}, DECEPTUS_ARROW_TRAP, 2, {0, 0, 0, 0, 0}, 20, 0};
static const DeceptusEnemyDef rat = {111, 200, 178, 0, 2, 0, 10, 1, 0, 0, {0, 0, 0}, DECEPTUS_RAT, 1, {7, 7, 5, 7, 3}, 20, 60};
static const DeceptusEnemyDef blob = {1048, 200, 180, 0, 2, 0, 5, 1, 4, 0, {0, 0, 0}, DECEPTUS_BLOB_2, 1, {15, 16, 16, 14, 3}, 20, 0};
static const DeceptusEnemyDef cannon = {493, 96, 80, 0, 0, 0, 102, 0, 60, 180, {0, 0, 0}, DECEPTUS_CANNON_2, 1, {0, 0, 0, 0, 0}, 16, 0};
static const DeceptusPathPoint path[] = {{200, 178}, {240, 178}};
static const DeceptusPathPoint orb_path[] = {{200, 180}, {200, 100}, {120, 100}, {120, 180}};
static const DeceptusEnemyDef orb = {1396, 188, 168, 0, 4, 0, 20, 123, 2, 0, {0, 0, 0}, DECEPTUS_ORB, 0, {0, 0, 0, 0, 0}, 20, 0};
static const DeceptusEnemyDef frog =
   {1721, 800, 178, 0, 0, 0, 48, 0, 1, 300, {240, 1252, 320}, DECEPTUS_FROG_BLUE, 0, {10, 1, 0, 0, 0}, 10, 0};
static void reset(const DeceptusEnemyDef* enemy)
{
   DeceptusU16 column;
   memset(&room, 0, sizeof(room));
   memset(cells, 0, sizeof(cells));
   for (column = 0; column < 32; ++column)
   {
      cells[24 * 32 + column] = DECEPTUS_SOLID;
   }
   room.collision = cells;
   room.columns = 32;
   room.rows = 28;
   room.spawn_x = 40;
   room.spawn_y = 192;
   room.patrol_x = -1;
   room.door_x = -1;
   room.max_health = 100;
   room.enemy_definitions = enemy;
   room.enemy_count = 1;
   room.enemy_paths = enemy->kind == DECEPTUS_ORB ? orb_path : path;
   room.enemy_path_count = enemy->kind == DECEPTUS_ORB ? 4 : 2;
   deceptusInit(&engine, &room, &video);
   CHECK(!engine.content_error);
}

static void projectileStart(DeceptusI16 x, DeceptusI16 y, DeceptusFixed velocity_x, DeceptusFixed velocity_y)
{
   reset(&trap);
   engine.entities[1].active = 0;
   engine.projectiles[0].active = 1;
   engine.projectiles[0].x = DECEPTUS_FIXED(x);
   engine.projectiles[0].y = DECEPTUS_FIXED(y);
   engine.projectiles[0].velocity_x = velocity_x;
   engine.projectiles[0].velocity_y = velocity_y;
   engine.projectiles[0].damage = 60;
}

static void projectileFloorTests(void)
{
   static const DeceptusMechanismDef floor = {900, {80, 64, 32, 1}, 0, 0, 0, DECEPTUS_MECH_COLLAPSING, 1, 0};
   DeceptusU16 tick;
   projectileStart(100, 60, 0, 96);
   cells[8 * 32 + 12] = DECEPTUS_ONE_WAY;
   CHECK(!deceptusEnemiesTick(&engine));
   CHECK(!engine.projectiles[0].active && engine.projectiles[0].y < DECEPTUS_FIXED(64));

   /* the source's player-only pass-through callback does not exempt arrows:
    * the closed collision contour catches underside and side impacts too. */
   projectileStart(100, 74, 0, -96);
   cells[8 * 32 + 12] = DECEPTUS_ONE_WAY;
   deceptusEnemiesTick(&engine);
   CHECK(!engine.projectiles[0].active);
   projectileStart(94, 68, 96, 0);
   cells[8 * 32 + 12] = DECEPTUS_ONE_WAY;
   deceptusEnemiesTick(&engine);
   CHECK(!engine.projectiles[0].active);
   projectileStart(108, 68, -96, 0);
   cells[8 * 32 + 12] = DECEPTUS_ONE_WAY;
   deceptusEnemiesTick(&engine);
   CHECK(!engine.projectiles[0].active);

   projectileStart(94, 62, 96, 96);
   cells[8 * 32 + 12] = DECEPTUS_ONE_WAY;
   engine.projectiles[0].x += 7;
   engine.projectiles[0].y += 9;
   engine.projectiles[0].gravity = 203;
   deceptusEnemiesTick(&engine);
   CHECK(!engine.projectiles[0].active && engine.projectiles[0].velocity_y == 97);

   /* an empty tile alongside the one-way floor remains empty. */
   projectileStart(108, 62, 96, 96);
   cells[8 * 32 + 12] = DECEPTUS_ONE_WAY;
   deceptusEnemiesTick(&engine);
   CHECK(engine.projectiles[0].active);

   projectileStart(100, 60, 0, 96);
   CHECK(deceptusMechanismsInit(&engine.mechanisms, &floor, 1, 0, 0));
   deceptusEnemiesTick(&engine);
   CHECK(!engine.projectiles[0].active && engine.projectiles[0].y < DECEPTUS_FIXED(64));
   projectileStart(100, 60, 0, 96);
   CHECK(deceptusMechanismsInit(&engine.mechanisms, &floor, 1, 0, 0));
   engine.mechanisms.states[0].active = 0;
   deceptusEnemiesTick(&engine);
   CHECK(engine.projectiles[0].active);
   engine.mechanisms.states[0].active = 1;
   engine.mechanisms.states[0].enabled = 0;
   engine.projectiles[0].y = DECEPTUS_FIXED(60);
   deceptusEnemiesTick(&engine);
   CHECK(engine.projectiles[0].active);

   projectileStart(100, 60, 0, 96);
   cells[8 * 32 + 12] = DECEPTUS_SOLID;
   deceptusEnemiesTick(&engine);
   CHECK(!engine.projectiles[0].active);
   projectileStart(100, 62, 0, 96);
   cells[8 * 32 + 12] = DECEPTUS_ONE_WAY;
   engine.entities[0].x = DECEPTUS_FIXED(100);
   engine.entities[0].y = DECEPTUS_FIXED(96);
   CHECK(!deceptusEnemiesTick(&engine));
   CHECK(!engine.projectiles[0].active);

   projectileStart(100, 60, 48, -16);
   for (tick = 0; tick < 10; ++tick)
   {
      deceptusEnemiesTick(&engine);
   }
   CHECK(engine.projectiles[0].active && engine.projectiles[0].x == DECEPTUS_FIXED(130));
   puts("arrow collision covers closed one-way contours and active dynamic floors");
}

static void cannonTests(void)
{
   static const DeceptusMechanismDef body = {493, {96, 92, 48, 36}, 0, 0, 0, DECEPTUS_MECH_CANNON_BODY, 1, 0};
   DeceptusEnemyDef left;
   DeceptusRect muzzle;
   DeceptusU16 tick, previous, count;
   unsigned shots[16];
#ifdef DECEPTUS_HAVE_LUA_REFERENCE
   lua_State* reference;
   unsigned index;
   const char* stubs[] = {
      "addShapeRectBevel",
      "addHitbox",
      "addAudioRange",
      "addSample",
      "addWeapon",
      "registerHitAnimation",
      "registerHitSamples",
      "updateProjectileAnimation",
      "setSpriteOffset",
      "playSample"
   };
   reference = luaL_newstate();
   CHECK(reference);
   luaL_openlibs(reference);
   lua_getglobal(reference, "package");
   lua_pushstring(reference, DECEPTUS_SOURCE_ROOT "/?.lua");
   lua_setfield(reference, -2, "path");
   lua_pop(reference, 1);
   for (index = 0; index < sizeof(stubs) / sizeof(stubs[0]); ++index)
   {
      lua_register(reference, stubs[index], noop);
   }
   reference_count = 0;
   reference_cannon_last_shot = 0;
   reference_cannon_frame = 0;
   lua_register(reference, "useWeapon", recordCannonShot);
   lua_register(reference, "updateSpriteRect", recordCannonSprite);
   CHECK(luaL_dofile(reference, DECEPTUS_SOURCE_ROOT "/data/scripts/enemies/cannon_2.lua") == LUA_OK);
   lua_getglobal(reference, "initialize");
   CHECK(lua_pcall(reference, 0, 0, 0) == LUA_OK);
   CHECK(luaL_dostring(reference, "writeProperty('alignment','right'); movedTo(96,80)") == LUA_OK);
#endif
   reset(&cannon);
   engine.projectiles[0].kind = DECEPTUS_BLUE_FIREBALL;
   engine.projectiles[0].owner = 1;
   engine.projectiles[0].phase = 4321;
   count = 0;
   for (tick = 1; tick <= 720; ++tick)
   {
      previous = engine.projectiles_fired;
#ifdef DECEPTUS_HAVE_LUA_REFERENCE
      reference_tick = tick;
      lua_getglobal(reference, "update");
      lua_pushnumber(reference, 1.0 / 60.0 + 1e-10);
      CHECK(lua_pcall(reference, 1, 0, 0) == LUA_OK);
#endif
      deceptusEnemiesTick(&engine);
      if (engine.projectiles_fired != previous)
      {
         CHECK(count < 16);
         shots[count++] = tick;
         CHECK(engine.projectiles[0].kind == DECEPTUS_CANNONBALL);
         CHECK(!engine.projectiles[0].owner && !engine.projectiles[0].phase);
         CHECK(engine.projectiles[0].velocity_x == 102 && engine.projectiles[0].velocity_y == 0);
         CHECK(engine.projectiles[0].y == DECEPTUS_FIXED(104));
      }
      CHECK(engine.entities[1].frame < 86 && !engine.content_error);
#ifdef DECEPTUS_HAVE_LUA_REFERENCE
      if (engine.entities[1].frame != reference_cannon_frame)
      {
         fprintf(stderr, "cannon frame mismatch tick %u: C %u Lua %u\n", tick, engine.entities[1].frame, reference_cannon_frame);
         exit(1);
      }
#endif
   }
   CHECK(count == 4 && shots[0] == 64 && shots[1] == 243);
#ifdef DECEPTUS_HAVE_LUA_REFERENCE
   CHECK(reference_count == count);
   for (index = 0; index < count; ++index)
   {
      CHECK(shots[index] == reference_shots[index]);
   }
   lua_close(reference);
   puts("cannon sprite sequence and gun interval agree with original Lua execution");
#endif
   reset(&cannon);
   engine.projectiles[0].kind = DECEPTUS_CANNONBALL;
   engine.projectiles[0].active = 1;
   engine.projectiles[0].x = DECEPTUS_FIXED(100);
   engine.projectiles[0].y = DECEPTUS_FIXED(100);
   engine.projectiles[0].velocity_x = -102;
   engine.projectiles[0].damage = 60;
   engine.entities[0].x = DECEPTUS_FIXED(80);
   engine.entities[0].y = DECEPTUS_FIXED(120);
   CHECK(deceptusEnemiesTick(&engine) == 60);
   CHECK(!engine.projectiles[0].active);
   left = cannon;
   left.alignment = 0;
   reset(&left);
   for (tick = 0; tick < 64; ++tick)
   {
      deceptusEnemiesTick(&engine);
   }
   CHECK(engine.projectiles_fired == 1 && engine.projectiles[0].kind == DECEPTUS_CANNONBALL);
   CHECK(engine.projectiles[0].velocity_x == -102 && engine.projectiles[0].velocity_y == 0);

   /* source gun filtering ignores enemy bodies even when a right-facing
    * muzzle begins inside its own cannon's physical collision contour. */
   reset(&cannon);
   CHECK(deceptusMechanismsInit(&engine.mechanisms, &body, 1, 0, 0));
   muzzle.x = 128;
   muzzle.y = 104;
   muzzle.width = 1;
   muzzle.height = 1;
   CHECK(deceptusMechanismSolid(&engine.mechanisms, &muzzle));
   CHECK(!deceptusMechanismProjectileSolid(&engine.mechanisms, &muzzle));
   for (tick = 0; tick < 64; ++tick)
   {
      deceptusEnemiesTick(&engine);
   }
   CHECK(engine.projectiles[0].active && engine.projectiles[0].x > DECEPTUS_FIXED(128));
}

static void sealedEnemyTests(void)
{
   static DeceptusU8 frog_cells[128 * 28];
   DeceptusU16 tick, column, seen, fired;
   DeceptusFrame frame;
#ifdef DECEPTUS_HAVE_LUA_REFERENCE
   lua_State* reference;
   unsigned index;
   double difference;
   const char* orb_stubs[] = {"addShapeCircle"};
   const char* frog_stubs[] = {
      "addShapeRect",
      "addHitbox",
      "addSprite",
      "setSpriteScale",
      "setSpriteZ",
      "addSample",
      "setReferenceVolume",
      "setAudioUpdateBehavior",
      "updateKeysPressed",
      "intersectsWithPlayer",
      "playSample",
      "setDamage",
      "die"
   };
   reference = luaL_newstate();
   CHECK(reference);
   luaL_openlibs(reference);
   lua_getglobal(reference, "package");
   lua_pushstring(reference, DECEPTUS_SOURCE_ROOT "/?.lua");
   lua_setfield(reference, -2, "path");
   lua_pop(reference, 1);
   for (index = 0; index < sizeof(orb_stubs) / sizeof(orb_stubs[0]); ++index)
   {
      lua_register(reference, orb_stubs[index], noop);
   }
   lua_register(reference, "setTransform", recordOrbTransform);
   lua_register(reference, "updateSpriteRect", recordOrbSprite);
   CHECK(luaL_dofile(reference, DECEPTUS_SOURCE_ROOT "/data/scripts/enemies/orb.lua") == LUA_OK);
   CHECK(
      luaL_dostring(
         reference,
         "initialize(); movedTo(188,168); setPath('path',{188,168,188,112,132,112,132,168}); "
         "writeProperty('speed',2); writeProperty('animation_speed',20)"
      ) == LUA_OK
   );
#endif
   reset(&orb);
   seen = 0;
   for (tick = 0; tick < 600; ++tick)
   {
      CHECK(!deceptusEnemiesTick(&engine));
      seen |= (DeceptusU16)(1U << engine.entities[1].frame);
#ifdef DECEPTUS_HAVE_LUA_REFERENCE
      lua_getglobal(reference, "update");
      lua_pushnumber(reference, 1.0 / 60.0 + 1e-10);
      CHECK(lua_pcall(reference, 1, 0, 0) == LUA_OK);
      difference = engine.entities[1].x / 16.0 - reference_orb_x;
      CHECK(difference > -0.13 && difference < 0.13);
      difference = engine.entities[1].y / 16.0 - reference_orb_y;
      CHECK(difference > -0.13 && difference < 0.13);
      CHECK(engine.entities[1].frame == reference_orb_frame);
#endif
   }
   CHECK(seen == 255);
   deceptusPlacePlayer(&engine, deceptusFloor(engine.entities[1].x), deceptusFloor(engine.entities[1].y) + 16);
   CHECK(deceptusEnemiesTick(&engine) == 2);
#ifdef DECEPTUS_HAVE_LUA_REFERENCE
   lua_close(reference);
   puts("orb contour motion and animation agree with original Lua within 0.13px");
   reference = luaL_newstate();
   CHECK(reference);
   luaL_openlibs(reference);
   lua_getglobal(reference, "package");
   lua_pushstring(reference, DECEPTUS_SOURCE_ROOT "/?.lua");
   lua_setfield(reference, -2, "path");
   lua_pop(reference, 1);
   for (index = 0; index < sizeof(frog_stubs) / sizeof(frog_stubs[0]); ++index)
   {
      lua_register(reference, frog_stubs[index], noop);
   }
   reference_frog_seed = frog.source_id;
   reference_frog_visible = 0;
   lua_getglobal(reference, "math");
   lua_pushcfunction(reference, frogReferenceRandom);
   lua_setfield(reference, -2, "random");
   lua_pop(reference, 1);
   lua_register(reference, "setSpriteOffset", recordFrogOffset);
   lua_register(reference, "setSpriteVisible", recordFrogVisible);
   lua_register(reference, "updateSpriteRect", recordFrogSprite);
   CHECK(luaL_dofile(reference, DECEPTUS_SOURCE_ROOT "/data/scripts/enemies/frog_blue.lua") == LUA_OK);
   CHECK(
      luaL_dostring(
         reference,
         "initialize(); movedTo(800,178); writeProperty('alignment','left'); "
         "writeProperty('attack_interval',5); writeProperty('orb_lifetime',4000); "
         "writeProperty('orb_speed',180); writeProperty('orb_path','sine'); "
         "writeProperty('orb_collision_size',10); writeProperty('wave_speed',0.8); "
         "writeProperty('wave_strength',20)"
      ) == LUA_OK
   );
#endif
   reset(&frog);
   memset(frog_cells, 0, sizeof(frog_cells));
   for (column = 0; column < 128; ++column)
   {
      frog_cells[24 * 128 + column] = DECEPTUS_SOLID;
   }
   for (tick = 0; tick < 24; ++tick)
   {
      frog_cells[tick * 128 + 62] = DECEPTUS_SOLID;
   }
   room.collision = frog_cells;
   room.columns = 128;
   deceptusInit(&engine, &room, &video);
   fired = 0;
   for (tick = 1; tick <= 900; ++tick)
   {
      CHECK(!deceptusEnemiesTick(&engine));
      CHECK(!engine.content_error);
      if (engine.projectiles_fired != fired)
      {
         CHECK(tick == 360 || tick == 660);
         fired = engine.projectiles_fired;
      }
      deceptusEnemiesRender(&engine, &frame);
#ifdef DECEPTUS_HAVE_LUA_REFERENCE
      lua_getglobal(reference, "update");
      lua_pushnumber(reference, 1.0 / 60.0 + 1e-10);
      CHECK(lua_pcall(reference, 1, 0, 0) == LUA_OK);
      if (engine.entities[1].frame != reference_frog_frame)
      {
         fprintf(stderr, "blue frog frame mismatch tick %u: C %u Lua %u\n", tick, engine.entities[1].frame, reference_frog_frame);
         exit(1);
      }
      CHECK((engine.projectiles[0].active != 0) == (reference_frog_visible != 0));
      if (reference_frog_visible)
      {
         difference = engine.projectiles[0].x / 16.0 - (800 + reference_frog_offset_x - 24);
         CHECK(difference > -0.13 && difference < 0.13);
         difference = engine.projectiles[0].y / 16.0 - (178 + reference_frog_offset_y);
         CHECK(difference > -0.3 && difference < 0.3);
         CHECK(((engine.entities[1].drop_ticks * 3 / 10) & 7) == reference_frog_orb_frame);
      }
#endif
   }
   CHECK(fired == 2);
#ifdef DECEPTUS_HAVE_LUA_REFERENCE
   lua_close(reference);
   puts("blue frog attack sequence, lifetime and sine orbs agree with original Lua");
#endif
   engine.entities[1].state = 1;
   engine.projectiles[0].active = 1;
   engine.projectiles[0].kind = DECEPTUS_BLUE_FIREBALL;
   engine.projectiles[0].owner = 1;
   engine.projectiles[0].age = 7;
   engine.projectiles[0].phase = 7 * 1252;
   engine.projectiles[0].velocity_x = -48;
   engine.entities[0].x = DECEPTUS_FIXED(770);
   engine.entities[0].y = DECEPTUS_FIXED(194);
   CHECK(deceptusEnemiesTick(&engine) == 1);
   CHECK(!engine.projectiles[0].active);
   CHECK(!deceptusEnemiesTick(&engine));
   engine.entities[1].state = 3;
   engine.entities[1].pause_ticks = 0;
   engine.entities[0].x = engine.entities[1].x;
   engine.entities[0].y = engine.entities[1].y;
   engine.entities[0].velocity_y = 0;
   for (tick = 0; tick < 114; ++tick)
   {
      CHECK(!deceptusEnemiesTick(&engine));
   }
   CHECK(!engine.entities[1].active);
}

int main(void)
{
   DeceptusU16 tick, previous, count, states, index;
   DeceptusI16 minimum, maximum;
   unsigned shots[64];
   DeceptusFrame frame;
#ifdef DECEPTUS_HAVE_LUA_REFERENCE
   lua_State* reference;
   const char* stubs[] = {
      "addSprite",
      "addShapeRect",
      "addHitbox",
      "addAudioRange",
      "addSample",
      "addWeapon",
      "setSpriteOffset",
      "updateSpriteRect",
      "updateProperties",
      "playSample"
   };
   reference = luaL_newstate();
   CHECK(reference);
   luaL_openlibs(reference);
   lua_getglobal(reference, "package");
   lua_pushstring(reference, DECEPTUS_SOURCE_ROOT "/?.lua");
   lua_setfield(reference, -2, "path");
   lua_pop(reference, 1);
   for (index = 0; index < sizeof(stubs) / sizeof(stubs[0]); ++index)
   {
      lua_register(reference, stubs[index], noop);
   }
   lua_register(reference, "useWeapon", recordShot);
   if (luaL_dofile(reference, DECEPTUS_SOURCE_ROOT "/data/scripts/enemies/arrowtrap_2.lua") != LUA_OK)
   {
      fprintf(stderr, "%s\n", lua_tostring(reference, -1));
      return 1;
   }
   lua_getglobal(reference, "initialize");
   CHECK(lua_pcall(reference, 0, 0, 0) == LUA_OK);
#endif
   reset(&trap);
   count = 0;
   for (tick = 1; tick <= 720; ++tick)
   {
      previous = engine.projectiles_fired;
      deceptusTick(&engine, 0);
      while (previous++ < engine.projectiles_fired)
      {
         CHECK(count < 64);
         shots[count++] = tick;
      }
#ifdef DECEPTUS_HAVE_LUA_REFERENCE
      reference_tick = tick;
      lua_getglobal(reference, "update");
      lua_pushnumber(reference, 1.0 / 60.0);
      CHECK(lua_pcall(reference, 1, 0, 0) == LUA_OK);
#endif
      CHECK(!engine.content_error);
   }
   CHECK(count == 12 && shots[0] == 72 && shots[1] == 87 && shots[2] == 102);
#ifdef DECEPTUS_HAVE_LUA_REFERENCE
   CHECK(reference_count == count);
   for (index = 0; index < count; ++index)
   {
      CHECK(shots[index] + 1 >= reference_shots[index] && shots[index] <= reference_shots[index] + 1);
   }
   lua_close(reference);
   puts("arrow firing schedule agrees with execution of original Lua");
#else
   (void)index;
#endif
   reset(&trap);
   engine.entities[0].x = DECEPTUS_FIXED(100);
   engine.checkpoint_x = 100;
   for (tick = 0; tick < 150; ++tick)
   {
      deceptusTick(&engine, 0);
   }
   CHECK(engine.health == 40 && engine.deaths == 0);
   for (tick = 0; tick < 160; ++tick)
   {
      deceptusTick(&engine, 0);
   }
   CHECK(engine.deaths == 1 && engine.health == 100 && deceptusFloor(engine.entities[0].x) == 100);
   reset(&rat);
   minimum = 240;
   maximum = 0;
   states = 0;
   for (tick = 0; tick < 1800; ++tick)
   {
      deceptusTick(&engine, 0);
      index = (DeceptusU16)deceptusFloor(engine.entities[1].x);
      if (index < minimum)
      {
         minimum = index;
      }
      if (index > maximum)
      {
         maximum = index;
      }
      states |= (DeceptusU16)(1U << engine.entities[1].state);
      CHECK(engine.entities[1].frame < 70 && engine.health == 100 && !engine.content_error);
   }
   CHECK(minimum <= 201 && maximum >= 239 && (states & 1) && (states & (1 << 4)));
   deceptusRender(&engine, &frame);
   CHECK(frame.count >= 1);
   room.enemy_count = 9;
   deceptusInit(&engine, &room, &video);
   CHECK(engine.content_error == 6);
   reset(&blob);
#ifdef DECEPTUS_HAVE_LUA_REFERENCE
   reference = luaL_newstate();
   CHECK(reference);
   luaL_openlibs(reference);
   lua_getglobal(reference, "package");
   lua_pushstring(reference, DECEPTUS_SOURCE_ROOT "/?.lua");
   lua_setfield(reference, -2, "path");
   lua_pop(reference, 1);
   for (index = 0; index < sizeof(stubs) / sizeof(stubs[0]); ++index)
   {
      lua_register(reference, stubs[index], noop);
   }
   lua_register(reference, "addShapeCircle", noop);
   lua_register(reference, "setReferenceVolume", noop);
   lua_register(reference, "setAudioUpdateBehavior", noop);
   lua_register(reference, "updateKeysPressed", noop);
   lua_register(reference, "setDamage", noop);
   lua_register(reference, "die", noop);
   lua_register(reference, "updateSpriteRect", recordBlobSprite);
   CHECK(luaL_dofile(reference, DECEPTUS_SOURCE_ROOT "/data/scripts/enemies/blob_2.lua") == LUA_OK);
   lua_getglobal(reference, "initialize");
   CHECK(lua_pcall(reference, 0, 0, 0) == LUA_OK);
   CHECK(luaL_dostring(reference, "setPath('path',{200,180,240,180})") == LUA_OK);
#endif
   minimum = 32767;
   maximum = 0;
   states = 0;
   for (tick = 0; tick < 1800; ++tick)
   {
#ifdef DECEPTUS_HAVE_LUA_REFERENCE
      lua_getglobal(reference, "movedTo");
      lua_pushnumber(reference, deceptusFloor(engine.entities[1].x));
      lua_pushnumber(reference, deceptusFloor(engine.entities[1].y) - 12);
      CHECK(lua_pcall(reference, 2, 0, 0) == LUA_OK);
      lua_getglobal(reference, "update");
      lua_pushnumber(reference, 1.0 / 60.0 + 1e-10);
      CHECK(lua_pcall(reference, 1, 0, 0) == LUA_OK);
#endif
      deceptusTick(&engine, 0);
#ifdef DECEPTUS_HAVE_LUA_REFERENCE
      if (engine.entities[1].frame != reference_blob_frame)
      {
         fprintf(stderr, "blob frame mismatch tick %u: C %u Lua %u\n", tick, engine.entities[1].frame, reference_blob_frame);
         exit(1);
      }
#endif
      index = (DeceptusU16)deceptusFloor(engine.entities[1].x);
      if (index < minimum)
      {
         minimum = (DeceptusI16)index;
      }
      if (index > maximum)
      {
         maximum = (DeceptusI16)index;
      }
      states |= (DeceptusU16)(1U << engine.entities[1].state);
      CHECK(engine.entities[1].frame < 61 && !engine.content_error);
   }
   CHECK(minimum <= 201 && maximum >= 239 && (states & 1) && (states & 2));
#ifdef DECEPTUS_HAVE_LUA_REFERENCE
   lua_close(reference);
   puts("blob ground-patrol sprite transitions agree with original Lua under identical position inputs");
#endif
   deceptusPlacePlayer(&engine, deceptusFloor(engine.entities[1].x), deceptusFloor(engine.entities[1].y) - 15);
   engine.entities[0].velocity_y = 16;
   deceptusTick(&engine, 0);
   CHECK(engine.entities[1].state == 3 && engine.entities[0].velocity_y < 0);
   for (tick = 0; tick < 43; ++tick)
   {
      deceptusTick(&engine, 0);
   }
   CHECK(!engine.entities[1].active);
   reset(&rat);
   room.skills = DECEPTUS_SKILL_WALL_JUMP;
   room.enemy_count = 0;
   room.spawn_x = 112;
   for (tick = 0; tick < 24; ++tick)
   {
      cells[tick * 32 + 15] = DECEPTUS_SOLID;
   }
   deceptusInit(&engine, &room, &video);
   deceptusTick(&engine, 0);
   for (tick = 0; tick < 20; ++tick)
   {
      deceptusTick(&engine, DECEPTUS_RIGHT | DECEPTUS_JUMP);
   }
   deceptusTick(&engine, DECEPTUS_RIGHT);
   previous = (DeceptusU16)deceptusFloor(engine.entities[0].y);
   deceptusTick(&engine, DECEPTUS_RIGHT | DECEPTUS_JUMP);
   CHECK(engine.entities[0].velocity_x < 0 && engine.entities[0].velocity_y < 0 && deceptusFloor(engine.entities[0].y) < previous);
   projectileFloorTests();
   cannonTests();
   sealedEnemyTests();
   puts("native rat patrol, trap projectiles, damage, respawn and wall jump checks passed");
   return 0;
}
