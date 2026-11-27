#ifndef SOUND_CUE_H
#define SOUND_CUE_H
#include "deceptus/engine.h"

/* the sounds the game asks a platform to play. each cue stands for one of the original's sound files;
 * tools/sound_assets.py maps them. the queue collects them over a frame's ticks for the platform. */
#define SOUND_NONE 0
#define SOUND_JUMP 1
#define SOUND_LAND 2
#define SOUND_STEP_STONE_LEFT 3
#define SOUND_STEP_STONE_RIGHT 4
#define SOUND_STEP_GRASS_LEFT 5
#define SOUND_STEP_GRASS_RIGHT 6
#define SOUND_HURT 7
#define SOUND_CHECKPOINT 8
#define SOUND_ITEM 9
#define SOUND_LEVER_INSERT 10
#define SOUND_DIALOGUE 11
#define SOUND_CUE_COUNT 12

/* the floors the original's footsteps tell apart (data/config/footsteps.json). */
#define SOUND_SURFACE_STONE 0
#define SOUND_SURFACE_GRASS 1

/* a cue's signed 8-bit samples at its platform's rate (tools/sound_assets.py); length 0 is silent. */
typedef struct SoundEffect
{
   const DeceptusI8 *data;
   DeceptusU32 length;
} SoundEffect;

#define SOUND_QUEUE_CAPACITY 8
typedef struct SoundQueue
{
   DeceptusU8 cues[SOUND_QUEUE_CAPACITY];
   DeceptusU8 count;
   /* footsteps alternate feet, as the original's do. */
   DeceptusU8 right_foot;
} SoundQueue;

/* queues a cue; a full queue drops it. */
void soundQueuePush(SoundQueue *queue, DeceptusU8 cue);
/* queues the sound of an engine event, if it has one, for footsteps on the given surface. */
void soundQueueEvent(SoundQueue *queue, DeceptusU8 event, DeceptusU8 surface);
/* queues the sounds of a tick's engine events. */
void soundQueueEngineEvents(SoundQueue *queue, const DeceptusEngine *engine, DeceptusU8 surface);
/* empties the queue once the platform has started its sounds. */
void soundQueueClear(SoundQueue *queue);

#endif
