/* original counterpart: data/scripts/enemies/frog_blue.lua. */

static const DeceptusI16 frog_blue_sines[256] = {
   0,      402,    804,    1205,   1606,   2006,   2404,   2801,   3196,   3590,   3981,   4370,   4756,   5139,   5520,   5897,
   6270,   6639,   7005,   7366,   7723,   8076,   8423,   8765,   9102,   9434,   9760,   10080,  10394,  10702,  11003,  11297,
   11585,  11866,  12140,  12406,  12665,  12916,  13160,  13395,  13623,  13842,  14053,  14256,  14449,  14635,  14811,  14978,
   15137,  15286,  15426,  15557,  15679,  15791,  15893,  15986,  16069,  16143,  16207,  16261,  16305,  16340,  16364,  16379,
   16384,  16379,  16364,  16340,  16305,  16261,  16207,  16143,  16069,  15986,  15893,  15791,  15679,  15557,  15426,  15286,
   15137,  14978,  14811,  14635,  14449,  14256,  14053,  13842,  13623,  13395,  13160,  12916,  12665,  12406,  12140,  11866,
   11585,  11297,  11003,  10702,  10394,  10080,  9760,   9434,   9102,   8765,   8423,   8076,   7723,   7366,   7005,   6639,
   6270,   5897,   5520,   5139,   4756,   4370,   3981,   3590,   3196,   2801,   2404,   2006,   1606,   1205,   804,    402,
   0,      -402,   -804,   -1205,  -1606,  -2006,  -2404,  -2801,  -3196,  -3590,  -3981,  -4370,  -4756,  -5139,  -5520,  -5897,
   -6270,  -6639,  -7005,  -7366,  -7723,  -8076,  -8423,  -8765,  -9102,  -9434,  -9760,  -10080, -10394, -10702, -11003, -11297,
   -11585, -11866, -12140, -12406, -12665, -12916, -13160, -13395, -13623, -13842, -14053, -14256, -14449, -14635, -14811, -14978,
   -15137, -15286, -15426, -15557, -15679, -15791, -15893, -15986, -16069, -16143, -16207, -16261, -16305, -16340, -16364, -16379,
   -16384, -16379, -16364, -16340, -16305, -16261, -16207, -16143, -16069, -15986, -15893, -15791, -15679, -15557, -15426, -15286,
   -15137, -14978, -14811, -14635, -14449, -14256, -14053, -13842, -13623, -13395, -13160, -12916, -12665, -12406, -12140, -11866,
   -11585, -11297, -11003, -10702, -10394, -10080, -9760,  -9434,  -9102,  -8765,  -8423,  -8076,  -7723,  -7366,  -7005,  -6639,
   -6270,  -5897,  -5520,  -5139,  -4756,  -4370,  -3981,  -3590,  -3196,  -2801,  -2404,  -2006,  -1606,  -1205,  -804,   -402,
};

static DeceptusI16 frogBlueSine(DeceptusU16 phase)
{
   DeceptusU16 index;
   DeceptusI16 first, difference;
   index = phase >> 8;
   first = frog_blue_sines[index];
   difference = frog_blue_sines[(index + 1) & 255] - first;
   return first + (DeceptusI16)((DeceptusI32)difference * (phase & 255) / 256);
}

static void frogBlueClearOrb(DeceptusEngine *engine, DeceptusU8 owner)
{
   DeceptusU16 index;
   for (index = 0; index < DECEPTUS_PROJECTILE_CAPACITY; ++index)
   {
      if (engine->projectiles[index].kind == DECEPTUS_BLUE_FIREBALL && engine->projectiles[index].owner == owner)
      {
         engine->projectiles[index].active = 0;
      }
   }
}

static void frogBlueFire(DeceptusEngine *engine, DeceptusU8 owner, const DeceptusEnemyDef *definition)
{
   DeceptusU16 index;
   DeceptusProjectile *orb;
   for (index = 0; index < DECEPTUS_PROJECTILE_CAPACITY; ++index)
   {
      orb = &engine->projectiles[index];
      if (!orb->active)
      {
         orb->active = 1;
         orb->kind = DECEPTUS_BLUE_FIREBALL;
         orb->owner = owner;
         orb->phase = 0;
         orb->age = 0;
         orb->alignment = definition->alignment;
         orb->gravity = 0;
         orb->velocity_x = definition->alignment ? definition->speed : -definition->speed;
         orb->velocity_y = 0;
         orb->damage = definition->damage;
         ++engine->projectiles_fired;
         return;
      }
   }
   engine->content_error = 6;
}

static void frogBlueTick(DeceptusEngine *engine, DeceptusEntity *frog, const DeceptusEnemyDef *definition, DeceptusU8 owner)
{
   DeceptusU8 column;
   if (++frog->drop_ticks >= 80)
   {
      frog->drop_ticks = 0;
   }
   if (frog->state == 3)
   {
      frogBlueClearOrb(engine, owner);
      if (++frog->pause_ticks >= 114)
      {
         frog->active = 0;
         return;
      }
      frog->frame = 44 + (DeceptusU8)(frog->pause_ticks / 6);
   }
   else
   {
      if (frog->timer < 65535)
      {
         ++frog->timer;
      }
      if (frog->state == 1)
      {
         frog->animation_ticks += definition->animation_fps;
         if (frog->animation_ticks >= 60)
         {
            frog->animation_ticks -= 60;
            frog->arrived = (frog->arrived + 1) & 7;
         }
         if (frog->coyote_ticks > frog->arrived && frog->coyote_ticks >= 7)
         {
            frog->random_seed = (DeceptusU16)(frog->random_seed * 17U + 31U);
            frog->inverse = ((frog->random_seed >> 8) % 20) == 0;
         }
         frog->coyote_ticks = frog->arrived;
         frog->frame = definition->alignment * 22 + (frog->inverse ? 8 : 0) + frog->arrived;
         if (frog->timer >= definition->period)
         {
            frog->state = 2;
            frog->timer = 0;
            frog->pause_ticks = 0;
            frog->arrived = 0;
            frog->animation_ticks = 0;
            frog->fired = 0;
            frog->frame = definition->alignment * 22 + 16;
            frogBlueClearOrb(engine, owner);
         }
      }
      else
      {
         ++frog->pause_ticks;
         if (frog->pause_ticks >= 107)
         {
            frog->state = 1;
            frog->arrived = 0;
            frog->animation_ticks = 0;
            frog->frame = definition->alignment * 22 + (frog->inverse ? 8 : 0);
         }
         else
         {
            if (frog->pause_ticks < 60)
            {
               column = 2;
            }
            else if (frog->pause_ticks < 62)
            {
               column = 3;
            }
            else if (frog->pause_ticks < 63)
            {
               column = 4;
            }
            else if (frog->pause_ticks < 88)
            {
               column = 5;
            }
            else if (frog->pause_ticks < 94)
            {
               column = 4;
            }
            else if (frog->pause_ticks < 100)
            {
               column = 3;
            }
            else
            {
               column = 0;
            }
            frog->frame = definition->alignment * 22 + 16 + column;
            if (frog->pause_ticks >= 60 && !frog->fired)
            {
               frogBlueFire(engine, owner, definition);
               frog->fired = 1;
            }
         }
      }
   }
   frog->velocity_y += 4;
   if (frog->velocity_y > 80)
   {
      frog->velocity_y = 80;
   }
   deceptusMoveVerticalDynamic(engine->room, frog, 19, 24, &engine->mechanisms);
}

static DeceptusU16 frogBlueContact(DeceptusEngine *engine, DeceptusEntity *frog, const DeceptusEnemyDef *definition)
{
   DeceptusEntity *player;
   DeceptusI16 dx, dy;
   if (frog->state == 3)
   {
      return 0;
   }
   if (!frog->active)
   {
      return 0;
   }
   player = &engine->entities[0];
   dx = deceptusFloor(frog->x) - deceptusFloor(player->x);
   dy = deceptusFloor(frog->y) - deceptusFloor(player->y);
   if (dx <= -28 || dx >= 28 || dy <= -DECEPTUS_PLAYER_HEIGHT(player) || dy >= 24)
   {
      return 0;
   }
   if (player->velocity_y > 0 && dy > 12)
   {
      frog->state = 3;
      frog->pause_ticks = 0;
      frog->frame = 44;
      player->velocity_y = -50;
      return 0;
   }
   return definition->damage;
}

static DeceptusU16 frogBlueOrbTick(DeceptusEngine *engine, DeceptusProjectile *orb)
{
   DeceptusEntity *frog, *player;
   const DeceptusEnemyDef *definition;
   DeceptusI32 x, y;
   DeceptusI16 dx, dy, half, offset_y;
   if (!orb->owner || orb->owner >= DECEPTUS_ENTITY_CAPACITY)
   {
      orb->active = 0;
      return 0;
   }
   frog = &engine->entities[orb->owner];
   if (!frog->active || frog->kind != DECEPTUS_FROG_BLUE || frog->state == 3)
   {
      orb->active = 0;
      return 0;
   }
   definition = &engine->room->enemy_definitions[frog->definition];
   ++orb->age;
   orb->phase += definition->delays[1];
   x = (DeceptusI32)frog->x + DECEPTUS_FIXED(definition->alignment ? 4 : -6) + (DeceptusI32)orb->velocity_x * orb->age;
   /* room-local coordinates must not wrap when an intangible source orb
    * travels beyond the loaded room. its owner cannot fire again early. */
   if (x < -DECEPTUS_FIXED(120) || x > (DeceptusI32)engine->room->columns * 128 + DECEPTUS_FIXED(120) || x < -32768 || x > 32767)
   {
      orb->active = 0;
      return 0;
   }
   orb->x = (DeceptusFixed)x;
   offset_y = (DeceptusI16)((DeceptusI32)frogBlueSine(orb->phase) * definition->delays[2] / 16384);
   y = (DeceptusI32)frog->y - DECEPTUS_FIXED(14) + offset_y;
   if (y < -32768 || y > 32767)
   {
      orb->active = 0;
      return 0;
   }
   orb->y = (DeceptusFixed)y;
   player = &engine->entities[0];
   dx = deceptusFloor(orb->x) - deceptusFloor(player->x);
   dy = deceptusFloor(orb->y) - deceptusFloor(player->y);
   half = definition->cycles[0] / 2;
   if (dx > -9 - half && dx < 9 + half && dy > -DECEPTUS_PLAYER_HEIGHT(player) - half && dy < half)
   {
      orb->active = 0;
      return definition->damage;
   }
   if (orb->age >= definition->delays[0])
   {
      orb->active = 0;
   }
   return 0;
}
