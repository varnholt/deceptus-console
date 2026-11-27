/* the GBA's song mixer (audio.h). Direct Sound A plays signed 8-bit samples that DMA 1 feeds from one
 * of two frame buffers; a VCOUNT interrupt once per frame restarts the DMA on the buffer mixed last
 * and mixes the other. at 1596 cycles per sample a 280,896-cycle frame is exactly AUDIO_FRAME_SAMPLES
 * samples, so the buffers stay in step with the interrupt. */
#include "audio.h"
#include <stddef.h>

#define REG16(address) (*(volatile DeceptusU16 *)(address))
#define REG32(address) (*(volatile DeceptusU32 *)(address))

#define CPU_HZ 16777216UL
#define SAMPLE_CYCLES 1596
#define AUDIO_RATE (CPU_HZ / SAMPLE_CYCLES)
#define AUDIO_FRAME_SAMPLES 176
#define FRAME_CYCLES 280896UL
typedef char FrameHoldsWholeSamples[(AUDIO_FRAME_SAMPLES * SAMPLE_CYCLES == FRAME_CYCLES) ? 1 : -1];
/* the display rate the song is stepped at: 16,777,216 / 280,896 Hz. */
#define FRAME_RATE ((DeceptusU16)(CPU_HZ * SONG_FRAME_RATE_SCALE / FRAME_CYCLES))
/* the DMA reads up to 16 bytes ahead of the FIFO: the padding keeps that read inside the buffers. */
#define FIFO_READ_AHEAD 16
/* a line in the visible picture: the mixing stays out of the vblank the video commit needs. */
#define MIX_LINE 100
/* sample positions in 1/4096 sample. */
#define POSITION_SHIFT 12
/* mixed voices sum to about +-32,000 at full volume; the shift brings a typical mix to 8 bits. */
#define MIX_SHIFT 8

#define REG_DISPSTAT 0x04000004
#define REG_SOUNDCNT_H 0x04000082
#define REG_SOUNDCNT_X 0x04000084
#define REG_FIFO_A 0x040000a0
#define REG_DMA1SAD 0x040000bc
#define REG_DMA1DAD 0x040000c0
#define REG_DMA1CNT_H 0x040000c6
#define REG_TM0CNT_L 0x04000100
#define REG_TM0CNT_H 0x04000102
#define REG_IE 0x04000200
#define REG_IME 0x04000208
#define BIOS_IRQ_VECTOR 0x03007ffc

#define DISPSTAT_VCOUNT_IRQ (1 << 5)
#define IRQ_VCOUNT (1 << 2)
/* Direct Sound A at full volume to both speakers, on timer 0, with a FIFO reset. */
#define SOUNDCNT_A_BOTH ((1 << 2) | (1 << 8) | (1 << 9) | (1 << 11))
#define SOUND_MASTER_ENABLE (1 << 7)
#define TIMER_ENABLE (1 << 7)
/* DMA: destination fixed, repeat, 32-bit, started by the sound FIFO, enabled. */
#define DMA_SOUND_FIFO ((2 << 5) | (1 << 9) | (1 << 10) | (3 << 12) | (1 << 15))

/* mixer.s reads this layout; the checks below tie its offsets to the assembly's. */
typedef struct Voice
{
   const DeceptusI8 *data;
   DeceptusU32 position, step, end, loop_length;
   DeceptusU8 volume;
} Voice;
#define VOICE_OFFSET(member) offsetof(Voice, member)
typedef char VoiceLayoutMatchesMixer[(VOICE_OFFSET(data) == 0 && VOICE_OFFSET(position) == 4 && VOICE_OFFSET(step) == 8 &&
                                      VOICE_OFFSET(end) == 12 && VOICE_OFFSET(loop_length) == 16 && VOICE_OFFSET(volume) == 20)
                                        ? 1
                                        : -1];
/* the samples one pass of mixVoice adds into the IWRAM chunk. */
#define CHUNK_SAMPLES 16
typedef char FrameHoldsWholeChunks[(AUDIO_FRAME_SAMPLES % CHUNK_SAMPLES == 0) ? 1 : -1];

/* mixer.s: adds count samples of the voice into chunk, advancing the voice. */
extern void mixVoice(DeceptusI32 *chunk, Voice *voice, DeceptusU32 count);

extern void irqHandler(void);
extern void irqEnable(void);

/* words, as the 32-bit DMA needs: the second buffer starts on a word too. */
typedef char BuffersAligned[(AUDIO_FRAME_SAMPLES % 4 == 0) ? 1 : -1];
static DeceptusU32 buffer_words[(2 * AUDIO_FRAME_SAMPLES + FIFO_READ_AHEAD) / 4];
#define buffers ((DeceptusI8 *)buffer_words)
static DeceptusI32 chunk[CHUNK_SAMPLES] __attribute__((section(".iwram.audioChunk")));
static Voice voices[SONG_CHANNELS_MAX];
static SongPlayer player;
static DeceptusU8 playing_buffer;

static void startVoice(Voice *voice, const SongOutput *output)
{
   const SongSample *sample;
   sample = output->sample;
   if (sample == 0)
   {
      voice->data = 0;
      return;
   }
   voice->data = sample->data;
   voice->position = output->offset << POSITION_SHIFT;
   voice->end = sample->length << POSITION_SHIFT;
   voice->loop_length = sample->loop_length << POSITION_SHIFT;
}

static void applyOutputs(void)
{
   SongOutput *output;
   Voice *voice;
   DeceptusU8 channel;
   for (channel = 0; channel < player.song->channels; ++channel)
   {
      output = &player.outputs[channel];
      voice = &voices[channel];
      if (output->started)
      {
         output->started = 0;
         startVoice(voice, output);
      }
      /* frequency / AUDIO_RATE samples per output sample, in 1/4096. */
      voice->step = (output->frequency << (POSITION_SHIFT - SONG_FREQUENCY_SHIFT)) / AUDIO_RATE;
      voice->volume = output->volume;
   }
}

/* the frame in chunks: each voice adds into the IWRAM chunk, which then becomes 8-bit output. */
static void mixFrame(DeceptusI8 *output)
{
   DeceptusU16 first, index;
   DeceptusI32 value;
   DeceptusU8 channel;
   Voice *voice;
   for (first = 0; first < AUDIO_FRAME_SAMPLES; first += CHUNK_SAMPLES)
   {
      for (index = 0; index < CHUNK_SAMPLES; ++index)
      {
         chunk[index] = 0;
      }
      for (channel = 0; channel < player.song->channels; ++channel)
      {
         voice = &voices[channel];
         if ((voice->data != 0) && (voice->volume != 0))
         {
            mixVoice(chunk, voice, CHUNK_SAMPLES);
         }
      }
      for (index = 0; index < CHUNK_SAMPLES; ++index)
      {
         value = chunk[index] >> MIX_SHIFT;
         if (value > 127)
         {
            value = 127;
         }
         else if (value < -128)
         {
            value = -128;
         }
         output[first + index] = (DeceptusI8)value;
      }
   }
}

static void restartDma(void)
{
   REG16(REG_DMA1CNT_H) = 0;
   REG32(REG_DMA1SAD) = (DeceptusU32)(buffers + playing_buffer * AUDIO_FRAME_SAMPLES);
   REG32(REG_DMA1DAD) = REG_FIFO_A;
   REG16(REG_DMA1CNT_H) = DMA_SOUND_FIFO;
}

void audioInterrupt(void)
{
   playing_buffer ^= 1;
   restartDma();
   songAdvance(&player, FRAME_RATE);
   applyOutputs();
   mixFrame(buffers + (playing_buffer ^ 1) * AUDIO_FRAME_SAMPLES);
}

void audioStart(const Song *song)
{
   DeceptusU16 index;
   songStart(&player, song);
   for (index = 0; index < SONG_CHANNELS_MAX; ++index)
   {
      voices[index].data = 0;
   }
   for (index = 0; index < sizeof(buffer_words) / sizeof(buffer_words[0]); ++index)
   {
      buffer_words[index] = 0;
   }
   playing_buffer = 0;
   REG16(REG_SOUNDCNT_X) = SOUND_MASTER_ENABLE;
   REG16(REG_SOUNDCNT_H) = SOUNDCNT_A_BOTH;
   REG16(REG_TM0CNT_L) = (DeceptusU16)(65536UL - SAMPLE_CYCLES);
   REG16(REG_TM0CNT_H) = TIMER_ENABLE;
   restartDma();
   REG32(BIOS_IRQ_VECTOR) = (DeceptusU32)irqHandler;
   REG16(REG_DISPSTAT) = (DeceptusU16)((MIX_LINE << 8) | DISPSTAT_VCOUNT_IRQ);
   REG16(REG_IE) = IRQ_VCOUNT;
   REG16(REG_IME) = 1;
   irqEnable();
}
