#ifndef SONG_H
#define SONG_H
#include "deceptus/types.h"

/* a tracked song (FastTracker 2 XM semantics, linear frequencies) converted by tools/song_assets.py,
 * and the player that steps it. the player only decides what each channel plays; a platform turns
 * its channel outputs into sound (a mixer, Paula channels, PCM channels). */

#define SONG_CHANNELS_MAX 8
#define SONG_KEYS 96
#define SONG_ENVELOPE_POINTS 12
/* the key-off note of a pattern cell. */
#define SONG_NOTE_OFF 97
/* a frequency in 1/256 Hz: the slowest note is a few Hz, the fastest some 100 kHz. */
#define SONG_FREQUENCY_SHIFT 8
#define SONG_VOLUME_MAX 64

typedef struct SongSample
{
   const DeceptusI8* data;
   /* lengths in samples; a ping-pong loop is unrolled by the converter, loop_length 0 plays once. */
   DeceptusU32 length, loop_start, loop_length;
   DeceptusU8 volume;
   DeceptusI8 relative_note, finetune;
} SongSample;

typedef struct SongEnvelope
{
   /* point ticks and values (0 .. SONG_VOLUME_MAX); count 0 means no envelope. */
   const DeceptusU16* ticks;
   const DeceptusU8* values;
   DeceptusU8 count, sustain, loop_start, loop_end;
   /* SONG_ENVELOPE_SUSTAIN and SONG_ENVELOPE_LOOP. */
   DeceptusU8 flags;
} SongEnvelope;

#define SONG_ENVELOPE_SUSTAIN 1
#define SONG_ENVELOPE_LOOP 2

typedef struct SongInstrument
{
   /* the song sample each key plays, 255 for none. */
   const DeceptusU8* keys;
   SongEnvelope volume;
   DeceptusU16 fadeout;
} SongInstrument;

typedef struct Song
{
   const DeceptusU8* orders;
   /* XM-packed pattern cells, row after row, channel after channel. */
   const DeceptusU8* const* patterns;
   const DeceptusU16* pattern_rows;
   const SongInstrument* instruments;
   const SongSample* samples;
   DeceptusU8 channels, order_count, restart, speed, bpm, instrument_count;
} Song;

/* what a channel plays after a tick. */
typedef struct SongOutput
{
   const SongSample* sample;
   /* the sample position to start from when started is set, in samples. */
   DeceptusU32 offset;
   DeceptusU32 frequency;
   DeceptusU8 volume;
   /* the sample restarts at offset this tick; cleared by the platform once it has started it. */
   DeceptusU8 started;
} SongOutput;

typedef struct SongChannel
{
   const SongInstrument* instrument;
   const SongSample* sample;
   DeceptusU8 note, cell_note, cell_instrument, cell_volume, effect, parameter;
   DeceptusI16 period, target_period, vibrato_offset;
   DeceptusU8 volume, key_on, tremolo_volume;
   DeceptusU16 envelope_tick, fadeout;
   DeceptusU8 envelope_point;
   /* effect memories, as FastTracker 2 keeps them per channel. */
   DeceptusU8 porta_up, porta_down, porta_speed, volume_slide, fine_porta_up, fine_porta_down, extra_fine_up, extra_fine_down,
      fine_volume_up, fine_volume_down, sample_offset, retrigger, global_slide;
   DeceptusU8 vibrato_speed, vibrato_depth, vibrato_position, tremolo_speed, tremolo_depth, tremolo_position;
   DeceptusU8 retrigger_ticks, arpeggio;
} SongChannel;

typedef struct SongPlayer
{
   const Song* song;
   const DeceptusU8* cursor;
   SongChannel channels[SONG_CHANNELS_MAX];
   SongOutput outputs[SONG_CHANNELS_MAX];
   /* the song's tick time owed to the caller's frames: ticks per second times 5 * SONG_FRAME_RATE_SCALE. */
   DeceptusU32 tick_due;
   DeceptusU16 row;
   DeceptusU8 order, tick, speed, bpm, global_volume, pattern_delay, jump_order, break_row, jump;
} SongPlayer;

/* starts the song from its first order. */
void songStart(SongPlayer* player, const Song* song);
/* display rates in 1/SONG_FRAME_RATE_SCALE Hz: the GBA's 59.73 Hz is 5973. */
#define SONG_FRAME_RATE_SCALE 100
/* advances the song by one frame of a display running at frame_rate; returns the ticks it played. */
DeceptusU8 songAdvance(SongPlayer* player, DeceptusU16 frame_rate);
/* one tick of the song: its outputs then describe the channels. */
void songTick(SongPlayer* player);
/* a frequency in 1/256 Hz for a linear XM period. */
DeceptusU32 songFrequency(DeceptusI16 period);

#endif
