#include "deceptus/playersword.h"
#include "deceptus/enemies.h"
#include <string.h>

void deceptusPlayerSwordInit(DeceptusPlayerSword *sword)
{
   memset(sword, 0, sizeof(*sword));
}

void deceptusPlayerSwordTick(DeceptusPlayerSword *sword, DeceptusEngine *engine, DeceptusU16 pressed)
{
   DeceptusEntity *player, *enemy;
   DeceptusI16 left, top, width, height, x, y, hit_width;
   DeceptusU16 index, projectile;
   player = &engine->entities[0];
   if (sword->ticks)
   {
      ++sword->ticks;
      if (sword->ticks > sword->duration)
      {
         sword->ticks = 0;
      }
   }
   if (!sword->ticks && (pressed & DECEPTUS_ATTACK) && !player->in_water)
   {
      sword->ticks = 1;
      sword->hit = 0;
      sword->facing_left = player->facing_left;
      sword->crouched = player->crouched;
      sword->airborne = !player->grounded;
      sword->duration = sword->airborne ? 54 : (sword->crouched ? 24 : 34);
   }
   /* playersword.cpp clears its hit permission on the first active update:
    * 200ms windup, then one damage-10 query in the 120ms hit window. */
   if (!sword->ticks || sword->ticks < 13 || sword->hit)
   {
      return;
   }
   sword->hit = 1;
   width = sword->crouched ? 40 : 60;
   height = sword->crouched ? 20 : 40;
   left = deceptusFloor(player->x) + (sword->facing_left ? -5 - width : 5);
   top = deceptusFloor(player->y) - (sword->crouched ? 20 : 39);
   for (index = 1; index < DECEPTUS_ENTITY_CAPACITY; ++index)
   {
      enemy = &engine->entities[index];
      /* both source hit callbacks have energy 30; frog_blue.lua uses a
       * smaller offset hitbox. rat.lua and orb.lua have no hit callback. */
      if (!enemy->active || (enemy->kind != DECEPTUS_BLOB_2 && enemy->kind != DECEPTUS_FROG_BLUE) || enemy->state == 3)
      {
         continue;
      }
      x = deceptusFloor(enemy->x) - 18;
      y = deceptusFloor(enemy->y) - (enemy->kind == DECEPTUS_FROG_BLUE ? 32 : 18);
      hit_width = enemy->kind == DECEPTUS_FROG_BLUE ? 16 : 36;
      if (x < left + width && x + hit_width > left && y < top + height && y + hit_width > top)
      {
         sword->damage[index] += 10;
         if (sword->damage[index] >= 30)
         {
            enemy->state = 3;
            enemy->timer = 0;
            enemy->pause_ticks = 0;
            enemy->velocity_x = 0;
            if (enemy->kind == DECEPTUS_FROG_BLUE)
            {
               enemy->frame = 44;
               for (projectile = 0; projectile < DECEPTUS_PROJECTILE_CAPACITY; ++projectile)
               {
                  if (engine->projectiles[projectile].kind == DECEPTUS_BLUE_FIREBALL && engine->projectiles[projectile].owner == index)
                  {
                     engine->projectiles[projectile].active = 0;
                  }
               }
            }
         }
      }
   }
   for (index = 0; index < engine->mechanisms.count; ++index)
   {
      const DeceptusMechanismDef *definition;
      definition = &engine->mechanisms.definitions[index];
      if (definition->kind == DECEPTUS_MECH_DESTRUCTIBLE && definition->bounds.x < left + width &&
          definition->bounds.x + definition->bounds.width > left && definition->bounds.y < top + height &&
          definition->bounds.y + definition->bounds.height > top)
      {
         deceptusMechanismHit(&engine->mechanisms, definition->source_id, 10);
      }
   }
}
