/* the game's sound cues (soundcue.h): which engine events make which of the original's sounds. */
#include "deceptus/soundcue.h"

void soundQueuePush(SoundQueue *queue, DeceptusU8 cue)
{
   if ((cue == SOUND_NONE) || (queue->count >= SOUND_QUEUE_CAPACITY))
   {
      return;
   }
   queue->cues[queue->count++] = cue;
}

static DeceptusU8 footstep(SoundQueue *queue, DeceptusU8 surface)
{
   queue->right_foot = (DeceptusU8)!queue->right_foot;
   if (surface == SOUND_SURFACE_GRASS)
   {
      return queue->right_foot ? SOUND_STEP_GRASS_RIGHT : SOUND_STEP_GRASS_LEFT;
   }
   return queue->right_foot ? SOUND_STEP_STONE_RIGHT : SOUND_STEP_STONE_LEFT;
}

void soundQueueEvent(SoundQueue *queue, DeceptusU8 event, DeceptusU8 surface)
{
   switch (event)
   {
   case DECEPTUS_EVENT_JUMP:
      soundQueuePush(queue, SOUND_JUMP);
      break;
   case DECEPTUS_EVENT_LAND:
      soundQueuePush(queue, SOUND_LAND);
      break;
   case DECEPTUS_EVENT_STEP:
      soundQueuePush(queue, footstep(queue, surface));
      break;
   case DECEPTUS_EVENT_HURT:
      soundQueuePush(queue, SOUND_HURT);
      break;
   case DECEPTUS_EVENT_CHECKPOINT:
      soundQueuePush(queue, SOUND_CHECKPOINT);
      break;
   case DECEPTUS_EVENT_ITEM:
      soundQueuePush(queue, SOUND_ITEM);
      break;
   case DECEPTUS_EVENT_LEVER_INSERT:
      soundQueuePush(queue, SOUND_LEVER_INSERT);
      break;
   case DECEPTUS_EVENT_DIALOGUE:
      soundQueuePush(queue, SOUND_DIALOGUE);
      break;
   default:
      break;
   }
}

void soundQueueEngineEvents(SoundQueue *queue, const DeceptusEngine *engine, DeceptusU8 surface)
{
   DeceptusU8 index;
   for (index = 0; index < engine->event_count; ++index)
   {
      soundQueueEvent(queue, engine->events[index].kind, surface);
   }
}

void soundQueueClear(SoundQueue *queue)
{
   queue->count = 0;
}
