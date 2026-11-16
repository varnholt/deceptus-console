#include "deceptus/engine.h"
#include <string.h>

DeceptusEntity *deceptusSpawn(DeceptusEngine *engine, DeceptusU8 kind, DeceptusI16 x, DeceptusI16 y)
{
   DeceptusU16 index;
   DeceptusEntity *entity;
   for (index = 0; index < DECEPTUS_ENTITY_CAPACITY; ++index)
   {
      entity = &engine->entities[index];
      if (!entity->active)
      {
         memset(entity, 0, sizeof(*entity));
         entity->active = 1;
         entity->kind = kind;
         entity->x = DECEPTUS_FIXED(x);
         entity->y = DECEPTUS_FIXED(y);
         return entity;
      }
   }
   return 0;
}
