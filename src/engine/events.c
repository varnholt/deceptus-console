#include "deceptus/engine.h"

void deceptusEventPush(DeceptusEngine* engine, DeceptusU8 kind)
{
   if (engine->event_count == DECEPTUS_EVENT_CAPACITY)
   {
      engine->event_overflow = 1;
      return;
   }
   engine->events[engine->event_count].kind = kind;
   engine->events[engine->event_count].entity = 0;
   ++engine->event_count;
}
