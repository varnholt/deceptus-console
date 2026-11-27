/* the 68000's half of the Mega Drive's audio (audio.h): it steps the song and writes what each channel
 * plays into the Z80 driver's channel table. the table's layout comes from audio_driver.s80 through
 * the generated smd_audio_driver.h. */
#include "audio.h"
#include <genesis.h>
#include "smd_audio_driver.h"
#include "smd_sounds.h"

/* timer A counts from its register value up to this, then runs out. */
#define TIMER_A_RANGE 1024
/* display rates in 1/SONG_FRAME_RATE_SCALE Hz. */
#define FRAME_RATE_NTSC 5992
#define FRAME_RATE_PAL 4970
/* the scanline whose interrupt steps the song: mid-picture, once a frame. */
#define AUDIO_LINE 150
#define CUE_CHANNEL 3
/* a cue's samples are at the DAC rate: one sample per output sample. */
#define STEP_ONE 256
#define STEP_LIMIT ((DeceptusU32)Z80_STEP_INTEGER_LIMIT * STEP_ONE - 1)
#define VOLUME_MAX (Z80_VOLUME_LEVELS - 1)
/* four channels at full volume sum to the DAC's full range. */
#define VOLUME_HEADROOM 4

typedef char ChannelsMatch[(Z80_CHANNELS == 4) && (SONG_CHANNELS_MAX >= Z80_CHANNELS) ? 1 : -1];

/* a channel's request as the driver's table lays it out, built up during a frame and copied in one
 * go: the Z80 stops while the 68000 holds its bus, and the DAC with it. */
typedef struct ChannelRequest
{
   DeceptusU8 fields[Z80_CHANNEL_REQUEST_SIZE];
   /* the CHANGED_ fields not yet copied. */
   DeceptusU8 changed;
} ChannelRequest;
#define CHANGED_START 1
#define CHANGED_STEP 2
#define CHANGED_VOLUME 4

static SongPlayer player;
static volatile DeceptusU8 playing;
static DeceptusU16 rate;
static DeceptusU16 frame_rate;
static ChannelRequest requests[Z80_CHANNELS];
static const SoundEffect* volatile cue_pending;
static DeceptusU16 cue_frames;

static void setTriple(DeceptusU8* field, DeceptusU32 value)
{
   field[0] = (DeceptusU8)value;
   field[1] = (DeceptusU8)(value >> 8);
   field[2] = (DeceptusU8)(value >> 16);
}

/* the driver copies the request once the sequence changes. */
static void requestStart(DeceptusU8 index, const DeceptusI8* data, DeceptusU32 length, DeceptusU32 loop_length)
{
   ChannelRequest* request;
   request = &requests[index];
   setTriple(&request->fields[Z80_CHANNEL_START], (DeceptusU32)data);
   setTriple(&request->fields[Z80_CHANNEL_END], (DeceptusU32)data + length);
   setTriple(&request->fields[Z80_CHANNEL_LOOP], loop_length);
   ++request->fields[Z80_CHANNEL_SEQUENCE];
   request->changed |= CHANGED_START;
}

static void requestStep(DeceptusU8 index, DeceptusU16 step)
{
   ChannelRequest* request;
   request = &requests[index];
   if ((request->fields[Z80_CHANNEL_STEP] != (DeceptusU8)step) || (request->fields[Z80_CHANNEL_STEP + 1] != (DeceptusU8)(step >> 8)))
   {
      request->fields[Z80_CHANNEL_STEP] = (DeceptusU8)step;
      request->fields[Z80_CHANNEL_STEP + 1] = (DeceptusU8)(step >> 8);
      request->changed |= CHANGED_STEP;
   }
}

static void requestVolume(DeceptusU8 index, DeceptusU8 volume)
{
   ChannelRequest* request;
   request = &requests[index];
   if (request->fields[Z80_CHANNEL_VOLUME] != (DeceptusU8)(Z80_VOLUME_PAGE + volume))
   {
      request->fields[Z80_CHANNEL_VOLUME] = (DeceptusU8)(Z80_VOLUME_PAGE + volume);
      request->changed |= CHANGED_VOLUME;
   }
}

/* copies the changed fields into the driver's table; the caller holds the Z80 bus. */
static void copyRequests(void)
{
   ChannelRequest* request;
   volatile DeceptusU8* table;
   DeceptusU8 index, field;
   for (index = 0; index < Z80_CHANNELS; ++index)
   {
      request = &requests[index];
      table = (volatile DeceptusU8*)(Z80_RAM + Z80_CHANNEL_TABLE + index * Z80_CHANNEL_SIZE);
      if (request->changed & CHANGED_VOLUME)
      {
         table[Z80_CHANNEL_VOLUME] = request->fields[Z80_CHANNEL_VOLUME];
      }
      if (request->changed & CHANGED_STEP)
      {
         table[Z80_CHANNEL_STEP] = request->fields[Z80_CHANNEL_STEP];
         table[Z80_CHANNEL_STEP + 1] = request->fields[Z80_CHANNEL_STEP + 1];
      }
      if (request->changed & CHANGED_START)
      {
         for (field = Z80_CHANNEL_START; field < Z80_CHANNEL_REQUEST_SIZE; ++field)
         {
            table[field] = request->fields[field];
         }
         table[Z80_CHANNEL_SEQUENCE] = request->fields[Z80_CHANNEL_SEQUENCE];
      }
      request->changed = 0;
   }
}

static DeceptusU16 songStep(DeceptusU32 frequency)
{
   DeceptusU32 step;
   step = frequency / rate;
   return (DeceptusU16)(step > STEP_LIMIT ? STEP_LIMIT : step);
}

static void applySong(void)
{
   SongOutput* output;
   const SongSample* sample;
   DeceptusU8 index;
   if (player.song == 0)
   {
      return;
   }
   for (index = 0; (index < player.song->channels) && (index < Z80_CHANNELS); ++index)
   {
      if ((index == CUE_CHANNEL) && cue_frames)
      {
         continue;
      }
      output = &player.outputs[index];
      sample = output->sample;
      if (output->started)
      {
         output->started = 0;
         if (sample == 0)
         {
            requestStart(index, 0, 0, 0);
         }
         else if (output->offset < sample->length)
         {
            requestStart(index, sample->data + output->offset, sample->length - output->offset, sample->loop_length);
         }
      }
      requestStep(index, songStep(output->frequency));
      requestVolume(index, (DeceptusU8)((output->volume * VOLUME_MAX + SONG_VOLUME_MAX / 2) / SONG_VOLUME_MAX));
   }
}

static void applyCue(void)
{
   const SoundEffect* effect;
   effect = cue_pending;
   if (effect)
   {
      cue_pending = 0;
      requestStart(CUE_CHANNEL, effect->data, effect->length, 0);
      requestStep(CUE_CHANNEL, STEP_ONE);
      requestVolume(CUE_CHANNEL, VOLUME_MAX);
      /* the cue's frames, rounded up, plus the frame the driver may take to start it. */
      cue_frames = (DeceptusU16)(effect->length * frame_rate / ((DeceptusU32)rate * SONG_FRAME_RATE_SCALE) + 2);
   }
   else if (cue_frames && (--cue_frames == 0))
   {
      /* the song takes its channel back with the next note it starts there. */
      requestStart(CUE_CHANNEL, 0, 0, 0);
   }
}

static HINTERRUPT_CALLBACK audioLine(void)
{
   if (!playing)
   {
      return;
   }
   songAdvance(&player, frame_rate);
   applyCue();
   applySong();
   Z80_requestBus(TRUE);
   copyRequests();
   Z80_releaseBus();
}

/* each volume's page holds every signed sample scaled by volume / VOLUME_MAX / VOLUME_HEADROOM,
 * rounded towards zero so that four full channels stay within -128 .. 127. */
static void fillVolumePages(void)
{
   volatile DeceptusU8* page;
   DeceptusU16 volume, value;
   DeceptusI16 sample, scaled;
   for (volume = 0; volume < Z80_VOLUME_LEVELS; ++volume)
   {
      page = (volatile DeceptusU8*)(Z80_RAM + ((Z80_VOLUME_PAGE + volume) << 8));
      for (value = 0; value < 256; ++value)
      {
         sample = (DeceptusI16)(value < 128 ? value : value - 256);
         scaled = (DeceptusI16)((sample < 0 ? -sample : sample) * volume / (VOLUME_MAX * VOLUME_HEADROOM));
         page[value] = (DeceptusU8)(sample < 0 ? -scaled : scaled);
      }
   }
}

void audioStart(const Song* song)
{
   volatile DeceptusU8* control;
   DeceptusU16 timer;
   playing = 0;
   rate = (DeceptusU16)((IS_PAL_SYSTEM ? Z80_YM_RATE_PAL : Z80_YM_RATE_NTSC) / Z80_DAC_TIMER_DIVISOR);
   frame_rate = IS_PAL_SYSTEM ? FRAME_RATE_PAL : FRAME_RATE_NTSC;
   Z80_loadCustomDriver(smd_audio_driver, sizeof(smd_audio_driver));
   Z80_requestBus(TRUE);
   fillVolumePages();
   control = (volatile DeceptusU8*)(Z80_RAM + Z80_CONTROL);
   timer = TIMER_A_RANGE - Z80_DAC_TIMER_DIVISOR;
   control[Z80_CONTROL_TIMER_HIGH] = (DeceptusU8)(timer >> 2);
   control[Z80_CONTROL_TIMER_LOW] = (DeceptusU8)(timer & 3);
   control[Z80_CONTROL_GO] = 1;
   Z80_releaseBus();
   memset(requests, 0, sizeof(requests));
   songStart(&player, song);
   cue_pending = 0;
   cue_frames = 0;
   playing = 1;
   SYS_setHIntCallback(audioLine);
   VDP_setHIntCounter(AUDIO_LINE);
   VDP_setHInterrupt(TRUE);
}

/* the song's interrupt takes and releases the Z80 bus too: masked, it cannot release the bus while
 * the main loop holds it, nor leave the main loop waiting for a grant it has withdrawn. */
static void writeTransfers(DeceptusU8 value)
{
   SYS_disableInts();
   Z80_requestBus(TRUE);
   *(volatile DeceptusU8*)(Z80_RAM + Z80_CONTROL + Z80_CONTROL_TRANSFERS) = value;
   Z80_releaseBus();
   SYS_enableInts();
}

void audioTransfersBegin(void)
{
   writeTransfers(1);
}

void audioTransfersEnd(void)
{
   writeTransfers(0);
}

void audioPlaySong(const Song* song)
{
   DeceptusU8 index;
   /* the song's interrupt steps the player and writes the requests: masked, it cannot see the
    * change half done. */
   SYS_disableInts();
   songStart(&player, song);
   for (index = 0; index < Z80_CHANNELS; ++index)
   {
      if ((index != CUE_CHANNEL) || !cue_frames)
      {
         requestStart(index, 0, 0, 0);
      }
   }
   SYS_enableInts();
}

void audioPlayCues(SoundQueue* queue)
{
   DeceptusU8 index;
   for (index = 0; index < queue->count; ++index)
   {
      if (smd_sounds[queue->cues[index]].length)
      {
         cue_pending = &smd_sounds[queue->cues[index]];
      }
   }
   soundQueueClear(queue);
}
