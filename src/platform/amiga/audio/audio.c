/* Paula plays the song (audio.h). a channel restarts over three vertical blanks, as Paula needs: its
 * DMA stops while the start registers are written, starts on the next blank (Paula latches the
 * start), and the blank after that writes the loop, or a silent word, for Paula to repeat. */
#include "audio.h"
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include "amiga_sounds.h"

#define CHIP __attribute__((section(".MEMF_CHIP")))
#define CHANNELS 4
/* Paula's clock on a PAL machine, and its vertical blank rate in 1/SONG_FRAME_RATE_SCALE Hz. */
#define PAULA_CLOCK 3546895UL
#define FRAME_RATE 4992
/* the shortest period Paula's DMA keeps up with. */
#define PERIOD_MIN 124
/* the cues are resampled to PAULA_CLOCK / CUE_PERIOD (tools/build_amiga_opening.py). */
#define CUE_PERIOD 443
#define CUE_CHANNEL 3
#define CUE_RATE (PAULA_CLOCK / CUE_PERIOD)

#define STATE_IDLE 0
#define STATE_START 1
#define STATE_LOOP 2

static volatile struct Custom* const hardware = (struct Custom*)0xdff000;

typedef struct Channel
{
   const DeceptusI8* loop;
   DeceptusU16 loop_words;
   DeceptusU8 state;
} Channel;

static DeceptusI8 silence[2] CHIP;
static SongPlayer player;
static Channel channels[CHANNELS];
static volatile DeceptusU8 playing;
/* the cue that next starts on CUE_CHANNEL, and the frames the cue sounding there has left. */
static const SoundEffect* volatile cue_pending;
static DeceptusU16 cue_frames;

static void stopChannel(DeceptusU8 index)
{
   hardware->dmacon = (DeceptusU16)(DMAF_AUD0 << index);
   hardware->aud[index].ac_vol = 0;
   channels[index].state = STATE_IDLE;
}

/* writes the start registers with the channel's DMA off; the next blank starts it. */
static void loadChannel(DeceptusU8 index, const DeceptusI8* data, DeceptusU32 length, const DeceptusI8* loop, DeceptusU32 loop_length)
{
   hardware->dmacon = (DeceptusU16)(DMAF_AUD0 << index);
   hardware->aud[index].ac_ptr = (UWORD*)data;
   hardware->aud[index].ac_len = (DeceptusU16)((length >> 1) ? (length >> 1) : 1);
   channels[index].loop = loop_length >= 2 ? loop : silence;
   channels[index].loop_words = (DeceptusU16)(loop_length >= 2 ? (loop_length >> 1) : 1);
   channels[index].state = STATE_START;
}

static void startMusic(DeceptusU8 index, const SongOutput* output)
{
   const SongSample* sample;
   DeceptusU32 offset;
   sample = output->sample;
   if (sample == 0)
   {
      stopChannel(index);
      return;
   }
   /* Paula reads words: the start and lengths round down to even samples. */
   offset = output->offset & ~1UL;
   loadChannel(index, sample->data + offset, sample->length - offset, sample->data + (sample->loop_start & ~1UL), sample->loop_length);
}

static void advanceChannels(void)
{
   DeceptusU8 index;
   DeceptusU16 enable;
   enable = 0;
   for (index = 0; index < CHANNELS; ++index)
   {
      if (channels[index].state == STATE_LOOP)
      {
         hardware->aud[index].ac_ptr = (UWORD*)channels[index].loop;
         hardware->aud[index].ac_len = channels[index].loop_words;
         channels[index].state = STATE_IDLE;
      }
      else if (channels[index].state == STATE_START)
      {
         enable |= (DeceptusU16)(DMAF_AUD0 << index);
         channels[index].state = STATE_LOOP;
      }
   }
   if (enable)
   {
      hardware->dmacon = (DeceptusU16)(DMAF_SETCLR | DMAF_MASTER | enable);
   }
}

static DeceptusU16 period(DeceptusU32 frequency)
{
   DeceptusU32 value;
   if (frequency == 0)
   {
      return 0xffff;
   }
   value = (PAULA_CLOCK << SONG_FREQUENCY_SHIFT) / frequency;
   if (value < PERIOD_MIN)
   {
      return PERIOD_MIN;
   }
   return value > 0xffffUL ? 0xffff : (DeceptusU16)value;
}

static void applySong(void)
{
   SongOutput* output;
   DeceptusU8 index;
   if (player.song == 0)
   {
      return;
   }
   for (index = 0; index < player.song->channels && index < CHANNELS; ++index)
   {
      if ((index == CUE_CHANNEL) && cue_frames)
      {
         continue;
      }
      output = &player.outputs[index];
      if (output->started)
      {
         output->started = 0;
         startMusic(index, output);
      }
      hardware->aud[index].ac_per = period(output->frequency);
      hardware->aud[index].ac_vol = output->volume;
   }
}

static void applyCue(void)
{
   const SoundEffect* effect;
   effect = cue_pending;
   if (effect)
   {
      cue_pending = 0;
      loadChannel(CUE_CHANNEL, effect->data, effect->length, silence, 0);
      hardware->aud[CUE_CHANNEL].ac_per = CUE_PERIOD;
      hardware->aud[CUE_CHANNEL].ac_vol = SONG_VOLUME_MAX;
      /* the samples at CUE_RATE over 50 frames a second, rounded up, plus the restart's two blanks. */
      cue_frames = (DeceptusU16)(effect->length / (CUE_RATE / 50) + 3);
   }
   else if (cue_frames && (--cue_frames == 0))
   {
      /* the song takes its channel back with the next note it starts there. */
      stopChannel(CUE_CHANNEL);
   }
}

void audioVerticalBlank(void)
{
   if (!playing)
   {
      return;
   }
   advanceChannels();
   songAdvance(&player, FRAME_RATE);
   applyCue();
   applySong();
}

void audioPlayCues(SoundQueue* queue)
{
   DeceptusU8 index;
   for (index = 0; index < queue->count; ++index)
   {
      if (amiga_sounds[queue->cues[index]].length)
      {
         cue_pending = &amiga_sounds[queue->cues[index]];
      }
   }
   soundQueueClear(queue);
}

void audioStart(const Song* song)
{
   DeceptusU8 index;
   playing = 0;
   for (index = 0; index < CHANNELS; ++index)
   {
      stopChannel(index);
   }
   silence[0] = silence[1] = 0;
   songStart(&player, song);
   cue_pending = 0;
   cue_frames = 0;
   playing = 1;
}

void audioPlaySong(const Song* song)
{
   DeceptusU8 index;
   /* the vertical blank steps the player: it must not see the change half done. */
   playing = 0;
   for (index = 0; index < CHANNELS; ++index)
   {
      if ((index != CUE_CHANNEL) || !cue_frames)
      {
         stopChannel(index);
      }
   }
   songStart(&player, song);
   playing = 1;
}

void audioStop(void)
{
   DeceptusU8 index;
   playing = 0;
   for (index = 0; index < CHANNELS; ++index)
   {
      stopChannel(index);
   }
}
