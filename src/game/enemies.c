#include "deceptus/enemies.h"
#include "deceptus/physics.h"

static DeceptusFixed approach(DeceptusFixed current, DeceptusFixed target, DeceptusFixed step)
{
   if (current < target)
   {
      current += step;
      return current > target ? target : current;
   }
   if (current > target)
   {
      current -= step;
      return current < target ? target : current;
   }
   return current;
}

#include "enemies/blob_2.c"
#include "enemies/cannon_2.c"
#include "enemies/frog_blue.c"
#include "enemies/orb.c"

void deceptusEnemiesInit(DeceptusEngine* engine)
{
   DeceptusU16 index, point;
   DeceptusI16 row;
   const DeceptusEnemyDef* definition;
   DeceptusEntity* entity;
   if (engine->room->enemy_count > 8 || (engine->room->enemy_count && !engine->room->enemy_definitions))
   {
      engine->content_error = 6;
      return;
   }
   for (index = 0; index < engine->room->enemy_count; ++index)
   {
      definition = &engine->room->enemy_definitions[index];
      if (definition->path_first > engine->room->enemy_path_count ||
          definition->path_count > engine->room->enemy_path_count - definition->path_first ||
          (definition->path_count && !engine->room->enemy_paths) || definition->alignment > 3 ||
          (definition->kind != DECEPTUS_RAT && definition->kind != DECEPTUS_ARROW_TRAP && definition->kind != DECEPTUS_BLOB_2 &&
           definition->kind != DECEPTUS_CANNON_2 && definition->kind != DECEPTUS_ORB && definition->kind != DECEPTUS_FROG_BLUE) ||
          ((definition->kind == DECEPTUS_ORB || definition->kind == DECEPTUS_FROG_BLUE)
              ? (!definition->animation_fps || definition->animation_fps > 60)
              : definition->animation_fps != (definition->kind == DECEPTUS_CANNON_2 ? 16 : 20)) ||
          definition->speed <= 0 || definition->speed >= DECEPTUS_FIXED(8))
      {
         engine->content_error = 6;
         return;
      }
      if (definition->kind == DECEPTUS_RAT)
      {
         if ((definition->path_count == 0) || (definition->path_count > 8) || (definition->acceleration == 0))
         {
            engine->content_error = 6;
            return;
         }
         for (point = 0; point < 5; ++point)
         {
            if (!definition->cycles[point] || definition->cycles[point] > 7)
            {
               engine->content_error = 6;
               return;
            }
         }
      }
      else if (definition->kind == DECEPTUS_BLOB_2)
      {
         if ((definition->path_count == 0) || (definition->acceleration == 0))
         {
            engine->content_error = 6;
            return;
         }
      }
      else if (definition->kind == DECEPTUS_ORB)
      {
         if (definition->path_count < 2 || definition->path_count > 8 || definition->acceleration < 0 || definition->acceleration > 255)
         {
            engine->content_error = 6;
            return;
         }
         for (point = 0; point < definition->path_count; ++point)
         {
            const DeceptusPathPoint *a, *b;
            a = &engine->room->enemy_paths[definition->path_first + point];
            b = &engine->room->enemy_paths[definition->path_first + (point + 1 == definition->path_count ? 0 : point + 1)];
            if ((a->x != b->x && a->y != b->y) || (a->x == b->x && a->y == b->y))
            {
               engine->content_error = 6;
               return;
            }
            row = a->x != b->x ? a->x - b->x : a->y - b->y;
            if (row > -8 && row < 8)
            {
               engine->content_error = 6;
               return;
            }
         }
      }
      else if (definition->kind == DECEPTUS_FROG_BLUE &&
               ((definition->period == 0) || (definition->period >= 65000) || (definition->alignment > 1) || (definition->delays[0] == 0) ||
                (definition->delays[0] > 1000) || (definition->delays[1] >= 32768) || (definition->delays[2] > 1024) ||
                (definition->cycles[0] == 0) || (definition->cycles[0] > 64)))
      {
         engine->content_error = 6;
         return;
      }
      else if (!definition->period || definition->period >= 65000 ||
               (definition->kind == DECEPTUS_CANNON_2 && (definition->period > 900 || definition->alignment > 1)))
      {
         engine->content_error = 6;
         return;
      }
      entity = deceptusSpawn(engine, definition->kind, definition->x, definition->y);
      if (!entity)
      {
         engine->content_error = 6;
         return;
      }
      entity->definition = (DeceptusU8)index;
      entity->random_seed = definition->source_id;
      entity->timer = definition->time_offset;
      if (definition->kind == DECEPTUS_CANNON_2)
      {
         entity->jump_buffer_ticks = 60;
      }
      if (definition->kind == DECEPTUS_ORB)
      {
         entity->x = DECEPTUS_FIXED(engine->room->enemy_paths[definition->path_first].x);
         entity->y = DECEPTUS_FIXED(engine->room->enemy_paths[definition->path_first].y);
         entity->target = 1;
      }
      if (definition->kind == DECEPTUS_FROG_BLUE)
      {
         entity->state = 1;
         entity->y += DECEPTUS_FIXED(14);
         entity->frame = definition->alignment * 22;
      }
      if (definition->kind == DECEPTUS_BLOB_2)
      {
         entity->target = definition->path_count > 1 ? 1 : 0;
         entity->y += DECEPTUS_FIXED(12);
      }
      if (definition->kind == DECEPTUS_RAT)
      {
         entity->state = 1;
         entity->target = definition->path_count > 1 ? 1 : 0;
         entity->y += DECEPTUS_FIXED(14);
         row = deceptusPositionCell(entity->y);
         while (row > 0 && (deceptusCell(engine->room, deceptusPositionCell(entity->x), row) & DECEPTUS_SOLID))
         {
            --row;
         }
         if (row < deceptusPositionCell(entity->y))
         {
            entity->y = DECEPTUS_FIXED((row + 1) * 8);
         }
      }
   }
}

static void ratTick(DeceptusEngine* engine, DeceptusEntity* rat, const DeceptusEnemyDef* definition)
{
   DeceptusI16 distance;
   DeceptusU8 column, choice;
   const DeceptusPathPoint* point;
   if (rat->pause_ticks)
   {
      --rat->pause_ticks;
   }
   else
   {
      if (++rat->timer >= definition->cycles[rat->state] * 3)
      {
         rat->timer = 0;
         rat->random_seed = (DeceptusU16)(rat->random_seed * 17U + 31U);
         choice = (DeceptusU8)((rat->random_seed >> 8) & 3);
         if (rat->state == 0)
         {
            if (rat->arrived)
            {
               rat->state = 4;
               rat->inverse = 0;
            }
         }
         else if (rat->state < 4)
         {
            rat->state = choice == 3 ? 4 : choice + 1;
            rat->inverse = choice == 3;
            rat->pause_ticks = definition->idle_ticks;
         }
         else if (rat->inverse)
         {
            rat->state = 0;
            rat->arrived = 0;
         }
         else
         {
            rat->state = choice == 3 ? 1 : choice + 1;
            rat->pause_ticks = definition->idle_ticks;
         }
      }
   }
   if (rat->state != 0 || rat->arrived)
   {
      rat->velocity_x = 0;
   }
   if (rat->state == 0 && !rat->arrived)
   {
      point = &engine->room->enemy_paths[definition->path_first + rat->target];
      distance = point->x - deceptusFloor(rat->x);
      if (distance >= -1 && distance <= 1)
      {
         rat->arrived = 1;
         rat->velocity_x = 0;
         if (++rat->target >= definition->path_count)
         {
            rat->target = 0;
         }
      }
      else
      {
         rat->facing_left = distance < 0;
         rat->velocity_x = approach(rat->velocity_x, rat->facing_left ? -definition->speed : definition->speed, definition->acceleration);
      }
   }
   rat->velocity_y += 4;
   if (rat->velocity_y > 80)
   {
      rat->velocity_y = 80;
   }
   deceptusMoveHorizontalDynamic(engine->room, rat, 7, 14, &engine->mechanisms);
   deceptusMoveVerticalDynamic(engine->room, rat, 7, 14, &engine->mechanisms);
   column = (DeceptusU8)(rat->timer / 3);
   if (column >= definition->cycles[rat->state])
   {
      column = 0;
   }
   if (rat->state == 4 && rat->inverse)
   {
      column = definition->cycles[4] - column - 1;
   }
   rat->frame = rat->state * 14 + (rat->facing_left ? 0 : 7) + column;
}

static void fireArrow(DeceptusEngine* engine, const DeceptusEntity* trap, const DeceptusEnemyDef* definition, DeceptusU8 muzzle)
{
   DeceptusU16 index;
   DeceptusProjectile* arrow;
   for (index = 0; index < DECEPTUS_PROJECTILE_CAPACITY; ++index)
   {
      arrow = &engine->projectiles[index];
      if (arrow->active)
      {
         continue;
      }
      arrow->active = 1;
      arrow->kind = DECEPTUS_ARROW;
      arrow->owner = 0;
      arrow->phase = 0;
      arrow->alignment = definition->alignment;
      arrow->age = 0;
      arrow->gravity = 0;
      arrow->damage = definition->damage;
      arrow->velocity_x = 0;
      arrow->velocity_y = 0;
      arrow->x = trap->x;
      arrow->y = trap->y;
      if (definition->alignment < 2)
      {
         arrow->x += DECEPTUS_FIXED(definition->alignment ? 36 : -16);
         arrow->y += DECEPTUS_FIXED(3 + muzzle * 8);
         arrow->velocity_x = definition->alignment ? definition->speed : -definition->speed;
      }
      else
      {
         arrow->x += DECEPTUS_FIXED(4 + muzzle * 8);
         arrow->y += DECEPTUS_FIXED(definition->alignment == 2 ? 40 : -16);
         arrow->velocity_y = definition->alignment == 2 ? definition->speed : -definition->speed;
      }
      ++engine->projectiles_fired;
      return;
   }
}

static DeceptusU8 projectileMove(DeceptusEngine* engine, DeceptusProjectile* arrow)
{
   DeceptusFixed start_x, start_y, next_x, next_y;
   DeceptusI16 step, row, column;
   DeceptusU8 radius;
   DeceptusRect point;
   start_x = arrow->x;
   start_y = arrow->y;
   radius = arrow->kind == DECEPTUS_CANNONBALL ? 10 : 0;
   point.width = radius ? radius * 2 : 1;
   point.height = point.width;
   /* each step is less than a pixel under the shared <8px/tick limit. this
    * also catches thin dynamic floors without adding a projectile body pool. */
   for (step = 1; step <= 8; ++step)
   {
      next_x = start_x + arrow->velocity_x * step / 8;
      next_y = start_y + arrow->velocity_y * step / 8;
      point.x = deceptusFloor(next_x) - radius;
      point.y = deceptusFloor(next_y) - radius;
      if (point.x < 0 || point.y < 0 || point.x + point.width > engine->room->columns * 8 ||
          point.y + point.height > engine->room->rows * 8 || deceptusMechanismProjectileSolid(&engine->mechanisms, &point))
      {
         arrow->active = 0;
         return 0;
      }
      for (row = point.y / 8; row <= (point.y + point.height - 1) / 8; ++row)
      {
         for (column = point.x / 8; column <= (point.x + point.width - 1) / 8; ++column)
         {
            /* source level.cpp builds closed chain loops for one-way tiles.
             * gamecontactlistener.cpp exempts only players, not projectiles. */
            if (deceptusCell(engine->room, column, row) & (DECEPTUS_SOLID | DECEPTUS_ONE_WAY))
            {
               arrow->active = 0;
               return 0;
            }
         }
      }
      arrow->x = next_x;
      arrow->y = next_y;
   }
   return 1;
}

DeceptusU16 deceptusEnemiesTick(DeceptusEngine* engine)
{
   DeceptusU16 index, muzzle, damage;
   DeceptusI16 x, y, dx, dy;
   DeceptusEntity* enemy;
   DeceptusProjectile* arrow;
   const DeceptusEnemyDef* definition;
   damage = 0;
   for (index = 1; index < DECEPTUS_ENTITY_CAPACITY; ++index)
   {
      enemy = &engine->entities[index];
      if (!enemy->active || (enemy->kind != DECEPTUS_RAT && enemy->kind != DECEPTUS_ARROW_TRAP && enemy->kind != DECEPTUS_BLOB_2 &&
                             enemy->kind != DECEPTUS_CANNON_2 && enemy->kind != DECEPTUS_ORB && enemy->kind != DECEPTUS_FROG_BLUE))
      {
         continue;
      }
      definition = &engine->room->enemy_definitions[enemy->definition];
      if (enemy->kind == DECEPTUS_BLOB_2)
      {
         DeceptusU16 contact;
         blob2Tick(engine, enemy, definition);
         contact = blob2Contact(engine, enemy, definition);
         if (contact > damage)
         {
            damage = contact;
         }
      }
      else if (enemy->kind == DECEPTUS_RAT)
      {
         ratTick(engine, enemy, definition);
         dx = deceptusFloor(enemy->x) - deceptusFloor(engine->entities[0].x);
         dy = deceptusFloor(enemy->y) - deceptusFloor(engine->entities[0].y);
         if (dx > -15 && dx < 15 && dy > -DECEPTUS_PLAYER_HEIGHT(&engine->entities[0]) && dy < 14 && definition->damage > damage)
         {
            damage = definition->damage;
         }
      }
      else if (enemy->kind == DECEPTUS_ORB)
      {
         DeceptusU16 contact;
         orbTick(engine, enemy, definition);
         contact = orbContact(engine, enemy, definition);
         if (contact > damage)
         {
            damage = contact;
         }
      }
      else if (enemy->kind == DECEPTUS_FROG_BLUE)
      {
         DeceptusU16 contact;
         frogBlueTick(engine, enemy, definition, (DeceptusU8)index);
         contact = frogBlueContact(engine, enemy, definition);
         if (contact > damage)
         {
            damage = contact;
         }
      }
      else if (enemy->kind == DECEPTUS_CANNON_2)
      {
         cannon2Tick(engine, enemy, definition);
      }
      else
      {
         if (++enemy->timer > definition->period)
         {
            enemy->timer = 0;
            enemy->fired = 0;
         }
         for (muzzle = 0; muzzle < 3; ++muzzle)
         {
            if (!(enemy->fired & (1 << muzzle)) && enemy->timer >= definition->delays[muzzle] + 12)
            {
               fireArrow(engine, enemy, definition, (DeceptusU8)muzzle);
               enemy->fired |= (DeceptusU8)(1 << muzzle);
            }
         }
         enemy->frame = definition->alignment;
      }
   }
   for (index = 0; index < DECEPTUS_PROJECTILE_CAPACITY; ++index)
   {
      arrow = &engine->projectiles[index];
      if (!arrow->active)
      {
         continue;
      }
      if (arrow->kind == DECEPTUS_BLUE_FIREBALL)
      {
         DeceptusU16 contact;
         contact = frogBlueOrbTick(engine, arrow);
         if (contact > damage)
         {
            damage = contact;
         }
         continue;
      }
      if (++arrow->age > 180)
      {
         arrow->active = 0;
         continue;
      }
      if (arrow->kind == DECEPTUS_CANNONBALL)
      {
         arrow->gravity = 0;
      }
      else if (arrow->gravity > 202)
      {
         arrow->gravity -= 203;
         ++arrow->velocity_y;
      }
      else
      {
         arrow->gravity += 53;
      }
      if (arrow->velocity_y >= DECEPTUS_FIXED(8))
      {
         arrow->velocity_y = DECEPTUS_FIXED(8) - 1;
      }
      if (!projectileMove(engine, arrow))
      {
         continue;
      }
      x = deceptusFloor(arrow->x);
      y = deceptusFloor(arrow->y);
      dx = x - deceptusFloor(engine->entities[0].x);
      dy = y - deceptusFloor(engine->entities[0].y);
      if (dx >= -(arrow->kind == DECEPTUS_CANNONBALL ? 19 : 9) && dx <= (arrow->kind == DECEPTUS_CANNONBALL ? 19 : 9) &&
          dy >= -DECEPTUS_PLAYER_HEIGHT(&engine->entities[0]) - (arrow->kind == DECEPTUS_CANNONBALL ? 10 : 0) &&
          dy <= (arrow->kind == DECEPTUS_CANNONBALL ? 10 : 0))
      {
         if (arrow->damage > damage)
         {
            damage = arrow->damage;
         }
         arrow->active = 0;
      }
   }
   return damage;
}

static void drawEnemy(
   const DeceptusEngine* engine,
   DeceptusFrame* frame,
   DeceptusU8 kind,
   DeceptusU8 image,
   DeceptusI16 x,
   DeceptusI16 y,
   DeceptusI16 width,
   DeceptusI16 height
)
{
   DeceptusDraw* draw;
   x -= engine->camera_x;
   y -= engine->camera_y;
   if (x <= -width || y <= -height || x >= engine->video.width || y >= engine->video.height || frame->count >= DECEPTUS_DRAW_CAPACITY ||
       frame->count >= engine->video.max_draws)
   {
      return;
   }
   draw = &frame->draws[frame->count++];
   draw->kind = kind;
   draw->frame = image;
   draw->x = x;
   draw->y = y;
   draw->palette = 0;
   draw->flip_x = 0;
}

void deceptusEnemiesRender(const DeceptusEngine* engine, DeceptusFrame* frame)
{
   DeceptusU16 index, muzzle, image, elapsed;
   DeceptusI16 x, y;
   const DeceptusEntity* enemy;
   const DeceptusEnemyDef* definition;
   const DeceptusProjectile* arrow;
   for (index = 0; index < DECEPTUS_PROJECTILE_CAPACITY; ++index)
   {
      arrow = &engine->projectiles[index];
      if (arrow->active)
      {
         if (arrow->kind == DECEPTUS_BLUE_FIREBALL)
         {
            if (arrow->owner && arrow->owner < DECEPTUS_ENTITY_CAPACITY)
            {
               drawEnemy(
                  engine,
                  frame,
                  DECEPTUS_BLUE_FIREBALL,
                  (DeceptusU8)(((engine->entities[arrow->owner].drop_ticks * 3) / 10) & 7),
                  deceptusFloor(arrow->x) - 36,
                  deceptusFloor(arrow->y) - 60,
                  120,
                  120
               );
            }
         }
         else if (arrow->kind == DECEPTUS_CANNONBALL)
         {
            drawEnemy(
               engine,
               frame,
               DECEPTUS_CANNONBALL,
               (DeceptusU8)((arrow->age / 3) % 4),
               deceptusFloor(arrow->x) - 36,
               deceptusFloor(arrow->y) - 36,
               72,
               72
            );
         }
         else
         {
            drawEnemy(
               engine,
               frame,
               DECEPTUS_ARROW,
               (DeceptusU8)(arrow->alignment * 4 + (arrow->age / 5) % 4),
               deceptusFloor(arrow->x) - 12,
               deceptusFloor(arrow->y) - 12,
               32,
               32
            );
         }
      }
   }
   for (index = 1; index < DECEPTUS_ENTITY_CAPACITY; ++index)
   {
      enemy = &engine->entities[index];
      if (!enemy->active)
      {
         continue;
      }
      x = deceptusFloor(enemy->x);
      y = deceptusFloor(enemy->y);
      if (enemy->kind == DECEPTUS_BLOB_2)
      {
         drawEnemy(engine, frame, DECEPTUS_BLOB_2, enemy->frame, x - 36, y - 48, 80, 80);
      }
      else if (enemy->kind == DECEPTUS_RAT)
      {
         drawEnemy(engine, frame, DECEPTUS_RAT, enemy->frame, x - 12, y - 26, 32, 32);
      }
      else if (enemy->kind == DECEPTUS_CANNON_2)
      {
         drawEnemy(engine, frame, DECEPTUS_CANNON_2, enemy->frame, x - 48, y - 24, 144, 72);
      }
      else if (enemy->kind == DECEPTUS_ORB)
      {
         drawEnemy(engine, frame, DECEPTUS_ORB, enemy->frame, x - 60, y - 60, 120, 120);
      }
      else if (enemy->kind == DECEPTUS_FROG_BLUE)
      {
         if (enemy->state == 3)
         {
            drawEnemy(engine, frame, DECEPTUS_FROG_BLUE, enemy->frame, x - 36, y - 50, 72, 72);
         }
         else
         {
            drawEnemy(engine, frame, DECEPTUS_FROG_BLUE, enemy->frame, x - 24, y - 38, 48, 48);
         }
      }
      else if (enemy->kind == DECEPTUS_ARROW_TRAP)
      {
         definition = &engine->room->enemy_definitions[enemy->definition];
         drawEnemy(engine, frame, DECEPTUS_ARROW_TRAP, definition->alignment, x - 24, y - 24, 80, 80);
         for (muzzle = 0; muzzle < 3; ++muzzle)
         {
            elapsed = enemy->timer;
            if (elapsed <= definition->delays[muzzle])
            {
               image = elapsed / 3;
               if (image > 5)
               {
                  image = 5;
               }
            }
            else
            {
               elapsed = (DeceptusU16)(elapsed - definition->delays[muzzle]);
               image = 5 + elapsed / 3;
               if (image > 9)
               {
                  image = 9;
               }
            }
            drawEnemy(
               engine,
               frame,
               DECEPTUS_ARROW_MUZZLE,
               (DeceptusU8)(definition->alignment * 10 + image),
               x + (definition->alignment < 2 ? (definition->alignment ? 24 : -24) : muzzle * 8),
               y + (definition->alignment < 2 ? muzzle * 8 : (definition->alignment == 2 ? 24 : -24)),
               32,
               32
            );
         }
      }
   }
}
