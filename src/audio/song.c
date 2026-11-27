/* a FastTracker 2 style player for the songs tools/song_assets.py converts (include/deceptus/song.h).
 * it follows FT2's tick rules with linear frequencies: effects act on tick 0 or on the later ticks of
 * a row as FT2 does, and remember their last non-zero parameter per channel. panning is left out:
 * the consoles play the song in mono. */
#include "deceptus/song.h"
#include "songfrequencies.h"

/* XM's linear period of C-0 and of the note that plays at 8363 Hz (C-4), in 1/64 semitones. */
#define PERIOD_C0 7680
#define PERIOD_MIDDLE 4608
#define SEMITONE 64
/* periods outside this range would leave the frequency table's reach. */
#define PERIOD_MIN 1
#define PERIOD_MAX 7680
#define FADEOUT_START 32768U
/* the display frames and the song's ticks (bpm * 2 / 5 per second) meet on this common scale. */
#define TICKS_PER_BEAT_NUMERATOR 2
#define TICKS_PER_BEAT_DENOMINATOR 5

/* a quarter sine wave of FT2's vibrato and tremolo, 0 .. 255. */
static const DeceptusU8 sine[32] = {0,   24,  49,  74,  97,  120, 141, 161, 180, 197, 212, 224, 235, 244, 250, 253,
                                    255, 253, 250, 244, 235, 224, 212, 197, 180, 161, 141, 120, 97,  74,  49,  24};

/* the volume change of FT2's multi retrigger (Rxy) by x: a step, or for 6, 7, 14 and 15 a scale. */
static const DeceptusI8 retrigger_steps[16] = {0, -1, -2, -4, -8, -16, 0, 0, 0, 1, 2, 4, 8, 16, 0, 0};

DeceptusU32 songFrequency(DeceptusI16 period)
{
   DeceptusI16 steps;
   DeceptusI16 octave;
   steps = (DeceptusI16)(PERIOD_MIDDLE - period);
   octave = 0;
   while (steps < 0)
   {
      steps = (DeceptusI16)(steps + SONG_OCTAVE_STEPS);
      --octave;
   }
   while (steps >= SONG_OCTAVE_STEPS)
   {
      steps = (DeceptusI16)(steps - SONG_OCTAVE_STEPS);
      ++octave;
   }
   if (octave >= 0)
   {
      return song_frequencies[steps] << octave;
   }
   return song_frequencies[steps] >> -octave;
}

/* tick % divisor: ticks stay below a row's 31, so subtraction is cheaper than a library remainder. */
static DeceptusU8 tickRemainder(DeceptusU8 tick, DeceptusU8 divisor)
{
   while (tick >= divisor)
   {
      tick = (DeceptusU8)(tick - divisor);
   }
   return tick;
}

static DeceptusU8 clampVolume(DeceptusI16 volume)
{
   if (volume < 0)
   {
      return 0;
   }
   if (volume > SONG_VOLUME_MAX)
   {
      return SONG_VOLUME_MAX;
   }
   return (DeceptusU8)volume;
}

static DeceptusI16 clampPeriod(DeceptusI16 period)
{
   if (period < PERIOD_MIN)
   {
      return PERIOD_MIN;
   }
   if (period > PERIOD_MAX)
   {
      return PERIOD_MAX;
   }
   return period;
}

/* the cursor at the first cell of a row of the current order's pattern. */
static void seekRow(SongPlayer *player, DeceptusU16 row)
{
   const DeceptusU8 *cursor;
   DeceptusU16 cell, cells;
   DeceptusU8 flags;
   cursor = player->song->patterns[player->song->orders[player->order]];
   cells = (DeceptusU16)(row * player->song->channels);
   for (cell = 0; cell < cells; ++cell)
   {
      flags = *cursor;
      if (flags & 0x80)
      {
         cursor += 1 + ((flags & 1) != 0) + ((flags & 2) != 0) + ((flags & 4) != 0) + ((flags & 8) != 0) + ((flags & 16) != 0);
      }
      else
      {
         cursor += 5;
      }
   }
   player->cursor = cursor;
   player->row = row;
}

static DeceptusU16 patternRows(const SongPlayer *player)
{
   return player->song->pattern_rows[player->song->orders[player->order]];
}

void songStart(SongPlayer *player, const Song *song)
{
   DeceptusU8 index;
   SongChannel *channel;
   player->song = song;
   player->order = 0;
   player->tick = 0;
   player->speed = song->speed;
   player->bpm = song->bpm;
   player->global_volume = SONG_VOLUME_MAX;
   player->pattern_delay = 0;
   player->jump = 0;
   player->tick_due = 0;
   for (index = 0; index < SONG_CHANNELS_MAX; ++index)
   {
      channel = &player->channels[index];
      channel->instrument = 0;
      channel->sample = 0;
      channel->volume = 0;
      channel->key_on = 0;
      channel->period = PERIOD_C0;
      channel->target_period = PERIOD_C0;
      channel->vibrato_offset = 0;
      channel->tremolo_volume = 0;
      channel->porta_up = channel->porta_down = channel->porta_speed = channel->volume_slide = 0;
      channel->fine_porta_up = channel->fine_porta_down = channel->extra_fine_up = channel->extra_fine_down = 0;
      channel->fine_volume_up = channel->fine_volume_down = channel->sample_offset = channel->retrigger = 0;
      channel->global_slide = 0;
      channel->vibrato_speed = channel->vibrato_depth = channel->vibrato_position = 0;
      channel->tremolo_speed = channel->tremolo_depth = channel->tremolo_position = 0;
      channel->retrigger_ticks = 0;
      channel->effect = channel->parameter = 0;
      channel->cell_note = channel->cell_instrument = channel->cell_volume = 0;
      player->outputs[index].sample = 0;
      player->outputs[index].offset = 0;
      player->outputs[index].frequency = 0;
      player->outputs[index].volume = 0;
      player->outputs[index].started = 0;
   }
   seekRow(player, 0);
}

static DeceptusI16 notePeriod(const SongSample *sample, DeceptusU8 note)
{
   return (DeceptusI16)(PERIOD_C0 - ((DeceptusI16)note - 1 + sample->relative_note) * SEMITONE - sample->finetune / 2);
}

/* the parameter, or the channel's memory of it when it is zero. */
static DeceptusU8 remembered(DeceptusU8 *memory, DeceptusU8 parameter)
{
   if (parameter)
   {
      *memory = parameter;
   }
   return *memory;
}

static void keyOn(SongChannel *channel)
{
   channel->key_on = 1;
   channel->envelope_tick = 0;
   channel->envelope_point = 0;
   channel->fadeout = FADEOUT_START;
   channel->vibrato_position = 0;
   channel->tremolo_position = 0;
}

static void keyOff(SongChannel *channel)
{
   channel->key_on = 0;
   if ((channel->instrument == 0) || (channel->instrument->volume.count == 0))
   {
      channel->volume = 0;
   }
}

static void trigger(SongPlayer *player, DeceptusU8 index, DeceptusU32 offset)
{
   SongOutput *output;
   output = &player->outputs[index];
   output->sample = player->channels[index].sample;
   output->offset = offset;
   output->started = 1;
   if ((output->sample != 0) && (offset >= output->sample->length))
   {
      output->sample = 0;
   }
}

static void volumeSlide(SongChannel *channel, DeceptusU8 parameter)
{
   if (parameter & 0xf0)
   {
      channel->volume = clampVolume((DeceptusI16)(channel->volume + (parameter >> 4)));
   }
   else
   {
      channel->volume = clampVolume((DeceptusI16)(channel->volume - (parameter & 15)));
   }
}

static void tonePortamento(SongChannel *channel)
{
   DeceptusI16 step;
   step = (DeceptusI16)(channel->porta_speed * 4);
   if (channel->period < channel->target_period)
   {
      channel->period = (DeceptusI16)(channel->period + step);
      if (channel->period > channel->target_period)
      {
         channel->period = channel->target_period;
      }
   }
   else if (channel->period > channel->target_period)
   {
      channel->period = (DeceptusI16)(channel->period - step);
      if (channel->period < channel->target_period)
      {
         channel->period = channel->target_period;
      }
   }
}

static DeceptusI16 wave(DeceptusU8 position, DeceptusU8 depth, DeceptusU8 shift)
{
   DeceptusI16 value;
   value = (DeceptusI16)((sine[position & 31] * depth) >> shift);
   return (position & 32) ? (DeceptusI16)-value : value;
}

static void vibrato(SongChannel *channel)
{
   channel->vibrato_offset = wave(channel->vibrato_position, channel->vibrato_depth, 5);
   channel->vibrato_position = (DeceptusU8)((channel->vibrato_position + channel->vibrato_speed) & 63);
}

static void tremolo(SongChannel *channel)
{
   channel->tremolo_volume = (DeceptusU8)(wave(channel->tremolo_position, channel->tremolo_depth, 6) + 64);
   channel->tremolo_position = (DeceptusU8)((channel->tremolo_position + channel->tremolo_speed) & 63);
}

static void retriggerVolume(SongChannel *channel)
{
   DeceptusU8 change;
   change = (DeceptusU8)(channel->retrigger >> 4);
   if (change == 6)
   {
      channel->volume = (DeceptusU8)(channel->volume * 2 / 3);
   }
   else if (change == 7)
   {
      channel->volume = (DeceptusU8)(channel->volume / 2);
   }
   else if (change == 14)
   {
      channel->volume = clampVolume((DeceptusI16)(channel->volume * 3 / 2));
   }
   else if (change == 15)
   {
      channel->volume = clampVolume((DeceptusI16)(channel->volume * 2));
   }
   else
   {
      channel->volume = clampVolume((DeceptusI16)(channel->volume + retrigger_steps[change]));
   }
}

/* the cell's note, instrument and volume take effect: on tick 0, or on the tick a note delay names. */
static void startCell(SongPlayer *player, DeceptusU8 index)
{
   SongChannel *channel;
   const SongInstrument *instrument;
   const SongSample *sample;
   DeceptusU8 note, porta, volume;
   DeceptusU32 offset;
   channel = &player->channels[index];
   note = channel->cell_note;
   volume = channel->cell_volume;
   porta = (channel->effect == 3) || (channel->effect == 5) || ((volume >> 4) == 0xf);
   if (channel->cell_instrument && (channel->cell_instrument <= player->song->instrument_count))
   {
      channel->instrument = &player->song->instruments[channel->cell_instrument - 1];
   }
   if (note == SONG_NOTE_OFF)
   {
      keyOff(channel);
   }
   else if (note && channel->instrument && (note <= SONG_KEYS))
   {
      instrument = channel->instrument;
      sample = (instrument->keys[note - 1] == 255) ? 0 : &player->song->samples[instrument->keys[note - 1]];
      if (sample)
      {
         if (porta && channel->sample)
         {
            channel->target_period = notePeriod(channel->sample, note);
         }
         else
         {
            channel->sample = sample;
            channel->note = note;
            channel->period = notePeriod(sample, note);
            channel->target_period = channel->period;
            offset = 0;
            if (channel->effect == 9)
            {
               offset = (DeceptusU32)remembered(&channel->sample_offset, channel->parameter) << 8;
            }
            trigger(player, index, offset);
         }
      }
   }
   if (channel->cell_instrument && channel->sample && (note != SONG_NOTE_OFF))
   {
      channel->volume = channel->sample->volume;
      keyOn(channel);
   }
   if ((volume >= 0x10) && (volume <= 0x50))
   {
      channel->volume = (DeceptusU8)(volume - 0x10);
   }
   else if ((volume >> 4) == 0x8)
   {
      channel->volume = clampVolume((DeceptusI16)(channel->volume - (volume & 15)));
   }
   else if ((volume >> 4) == 0x9)
   {
      channel->volume = clampVolume((DeceptusI16)(channel->volume + (volume & 15)));
   }
   else if ((volume >> 4) == 0xa)
   {
      channel->vibrato_speed = (DeceptusU8)(volume & 15);
   }
   else if ((volume >> 4) == 0xb)
   {
      if (volume & 15)
      {
         channel->vibrato_depth = (DeceptusU8)(volume & 15);
      }
   }
   else if ((volume >> 4) == 0xf)
   {
      if (volume & 15)
      {
         channel->porta_speed = (DeceptusU8)((volume & 15) << 4);
      }
   }
}

/* the effects that act once, on the row's first tick. */
static void rowEffect(SongPlayer *player, DeceptusU8 index)
{
   SongChannel *channel;
   DeceptusU8 parameter, high, low;
   channel = &player->channels[index];
   parameter = channel->parameter;
   high = (DeceptusU8)(parameter >> 4);
   low = (DeceptusU8)(parameter & 15);
   switch (channel->effect)
   {
   case 0x0:
      channel->arpeggio = parameter;
      break;
   case 0x1:
      remembered(&channel->porta_up, parameter);
      break;
   case 0x2:
      remembered(&channel->porta_down, parameter);
      break;
   case 0x3:
      remembered(&channel->porta_speed, parameter);
      break;
   case 0x4:
      if (high)
      {
         channel->vibrato_speed = high;
      }
      if (low)
      {
         channel->vibrato_depth = low;
      }
      break;
   case 0x5:
   case 0x6:
   case 0xa:
      remembered(&channel->volume_slide, parameter);
      break;
   case 0x7:
      if (high)
      {
         channel->tremolo_speed = high;
      }
      if (low)
      {
         channel->tremolo_depth = low;
      }
      break;
   case 0xb:
      player->jump_order = parameter;
      player->break_row = 0;
      player->jump = 1;
      break;
   case 0xc:
      channel->volume = clampVolume(parameter);
      break;
   case 0xd:
      if (!player->jump)
      {
         player->jump_order = (DeceptusU8)(player->order + 1);
      }
      player->break_row = (DeceptusU8)(high * 10 + low);
      player->jump = 1;
      break;
   case 0xe:
      switch (high)
      {
      case 0x1:
         channel->period = clampPeriod((DeceptusI16)(channel->period - remembered(&channel->fine_porta_up, low) * 4));
         break;
      case 0x2:
         channel->period = clampPeriod((DeceptusI16)(channel->period + remembered(&channel->fine_porta_down, low) * 4));
         break;
      case 0xa:
         channel->volume = clampVolume((DeceptusI16)(channel->volume + remembered(&channel->fine_volume_up, low)));
         break;
      case 0xb:
         channel->volume = clampVolume((DeceptusI16)(channel->volume - remembered(&channel->fine_volume_down, low)));
         break;
      case 0xe:
         if (!player->pattern_delay)
         {
            player->pattern_delay = low;
         }
         break;
      default:
         break;
      }
      break;
   case 0xf:
      if (parameter == 0)
      {
         break;
      }
      if (parameter < 32)
      {
         player->speed = parameter;
      }
      else
      {
         player->bpm = parameter;
      }
      break;
   case 0x10:
      player->global_volume = clampVolume(parameter);
      break;
   case 0x11:
      remembered(&channel->global_slide, parameter);
      break;
   case 0x14:
      if (parameter == 0)
      {
         keyOff(channel);
      }
      break;
   case 0x1b:
      if (high)
      {
         channel->retrigger = (DeceptusU8)((channel->retrigger & 15) | (high << 4));
      }
      if (low)
      {
         channel->retrigger = (DeceptusU8)((channel->retrigger & 0xf0) | low);
      }
      break;
   case 0x21:
      if (high == 1)
      {
         channel->period = clampPeriod((DeceptusI16)(channel->period - remembered(&channel->extra_fine_up, low)));
      }
      else if (high == 2)
      {
         channel->period = clampPeriod((DeceptusI16)(channel->period + remembered(&channel->extra_fine_down, low)));
      }
      break;
   default:
      break;
   }
}

/* the effects that act on the row's later ticks. */
static void tickEffect(SongPlayer *player, DeceptusU8 index)
{
   SongChannel *channel;
   DeceptusU8 parameter, high, low, volume;
   channel = &player->channels[index];
   parameter = channel->parameter;
   high = (DeceptusU8)(parameter >> 4);
   low = (DeceptusU8)(parameter & 15);
   volume = channel->cell_volume;
   if ((volume >> 4) == 0x6)
   {
      channel->volume = clampVolume((DeceptusI16)(channel->volume - (volume & 15)));
   }
   else if ((volume >> 4) == 0x7)
   {
      channel->volume = clampVolume((DeceptusI16)(channel->volume + (volume & 15)));
   }
   else if ((volume >> 4) == 0xb)
   {
      vibrato(channel);
   }
   else if ((volume >> 4) == 0xf)
   {
      tonePortamento(channel);
   }
   switch (channel->effect)
   {
   case 0x1:
      channel->period = clampPeriod((DeceptusI16)(channel->period - channel->porta_up * 4));
      break;
   case 0x2:
      channel->period = clampPeriod((DeceptusI16)(channel->period + channel->porta_down * 4));
      break;
   case 0x3:
      tonePortamento(channel);
      break;
   case 0x4:
      vibrato(channel);
      break;
   case 0x5:
      tonePortamento(channel);
      volumeSlide(channel, channel->volume_slide);
      break;
   case 0x6:
      vibrato(channel);
      volumeSlide(channel, channel->volume_slide);
      break;
   case 0x7:
      tremolo(channel);
      break;
   case 0xa:
      volumeSlide(channel, channel->volume_slide);
      break;
   case 0xe:
      if ((high == 0x9) && low && (tickRemainder(player->tick, low) == 0))
      {
         trigger(player, index, 0);
         keyOn(channel);
      }
      else if ((high == 0xc) && (player->tick == low))
      {
         channel->volume = 0;
      }
      else if ((high == 0xd) && (player->tick == low))
      {
         startCell(player, index);
      }
      break;
   case 0x11:
      if (channel->global_slide & 0xf0)
      {
         player->global_volume = clampVolume((DeceptusI16)(player->global_volume + (channel->global_slide >> 4)));
      }
      else
      {
         player->global_volume = clampVolume((DeceptusI16)(player->global_volume - (channel->global_slide & 15)));
      }
      break;
   case 0x14:
      if (player->tick == parameter)
      {
         keyOff(channel);
      }
      break;
   case 0x1b:
      if ((channel->retrigger & 15) && (++channel->retrigger_ticks >= (channel->retrigger & 15)))
      {
         channel->retrigger_ticks = 0;
         retriggerVolume(channel);
         trigger(player, index, 0);
      }
      break;
   default:
      break;
   }
}

/* the envelope value now, 0 .. SONG_VOLUME_MAX, and one tick further along it. */
static DeceptusU8 envelopeStep(SongChannel *channel)
{
   const SongEnvelope *envelope;
   DeceptusU8 point, value;
   DeceptusU16 start, end;
   envelope = &channel->instrument->volume;
   point = channel->envelope_point;
   while ((point + 1 < envelope->count) && (channel->envelope_tick >= envelope->ticks[point + 1]))
   {
      ++point;
   }
   channel->envelope_point = point;
   if (point + 1 >= envelope->count)
   {
      value = envelope->values[envelope->count - 1];
   }
   else
   {
      start = envelope->ticks[point];
      end = envelope->ticks[point + 1];
      value = (DeceptusU8)(envelope->values[point] +
                           ((DeceptusI16)(envelope->values[point + 1] - envelope->values[point]) * (DeceptusI16)(channel->envelope_tick - start)) /
                              (DeceptusI16)(end - start));
   }
   if (channel->key_on && (envelope->flags & SONG_ENVELOPE_SUSTAIN) && (channel->envelope_tick == envelope->ticks[envelope->sustain]))
   {
      return value;
   }
   ++channel->envelope_tick;
   if ((envelope->flags & SONG_ENVELOPE_LOOP) && (channel->envelope_tick > envelope->ticks[envelope->loop_end]))
   {
      channel->envelope_tick = envelope->ticks[envelope->loop_start];
      channel->envelope_point = envelope->loop_start;
   }
   return value;
}

static void updateOutput(SongPlayer *player, DeceptusU8 index)
{
   SongChannel *channel;
   SongOutput *output;
   DeceptusI16 period;
   DeceptusU8 semitones;
   DeceptusU16 volume;
   channel = &player->channels[index];
   output = &player->outputs[index];
   period = (DeceptusI16)(channel->period + channel->vibrato_offset);
   if ((channel->effect == 0) && channel->arpeggio)
   {
      semitones = 0;
      if (tickRemainder(player->tick, 3) == 1)
      {
         semitones = (DeceptusU8)(channel->arpeggio >> 4);
      }
      else if (tickRemainder(player->tick, 3) == 2)
      {
         semitones = (DeceptusU8)(channel->arpeggio & 15);
      }
      period = (DeceptusI16)(period - semitones * SEMITONE);
   }
   output->frequency = songFrequency(clampPeriod(period));
   volume = channel->volume;
   if (channel->effect == 0x7)
   {
      volume = clampVolume((DeceptusI16)(volume + channel->tremolo_volume - 64));
   }
   if (channel->instrument && channel->instrument->volume.count)
   {
      volume = (DeceptusU16)((volume * envelopeStep(channel)) >> 6);
      if (!channel->key_on)
      {
         volume = (DeceptusU16)((volume * (channel->fadeout >> 7)) >> 8);
         channel->fadeout = (channel->fadeout > channel->instrument->fadeout) ? (DeceptusU16)(channel->fadeout - channel->instrument->fadeout) : 0;
      }
   }
   output->volume = (DeceptusU8)((volume * player->global_volume) >> 6);
}

/* the next row's cells into the channels. */
static void readRow(SongPlayer *player)
{
   SongChannel *channel;
   DeceptusU8 index, flags, fields[5], field;
   for (index = 0; index < player->song->channels; ++index)
   {
      channel = &player->channels[index];
      flags = *player->cursor;
      if (flags & 0x80)
      {
         ++player->cursor;
         for (field = 0; field < 5; ++field)
         {
            fields[field] = 0;
            if (flags & (1 << field))
            {
               fields[field] = *player->cursor++;
            }
         }
      }
      else
      {
         for (field = 0; field < 5; ++field)
         {
            fields[field] = *player->cursor++;
         }
      }
      channel->cell_note = fields[0];
      channel->cell_instrument = fields[1];
      channel->cell_volume = fields[2];
      channel->effect = fields[3];
      channel->parameter = fields[4];
      channel->vibrato_offset = 0;
      channel->retrigger_ticks = 0;
      if (channel->effect != 0)
      {
         channel->arpeggio = 0;
      }
   }
}

static void nextRow(SongPlayer *player)
{
   DeceptusU16 row;
   if (player->jump)
   {
      player->jump = 0;
      player->order = player->jump_order;
      row = player->break_row;
   }
   else
   {
      row = (DeceptusU16)(player->row + 1);
      if (row >= patternRows(player))
      {
         ++player->order;
         row = 0;
      }
      else
      {
         player->row = row;
         return;
      }
   }
   if (player->order >= player->song->order_count)
   {
      player->order = player->song->restart < player->song->order_count ? player->song->restart : 0;
   }
   if (row >= patternRows(player))
   {
      row = 0;
   }
   seekRow(player, row);
}

void songTick(SongPlayer *player)
{
   DeceptusU8 index;
   SongChannel *channel;
   if (player->tick == 0)
   {
      readRow(player);
      for (index = 0; index < player->song->channels; ++index)
      {
         channel = &player->channels[index];
         if (!((channel->effect == 0xe) && ((channel->parameter >> 4) == 0xd) && (channel->parameter & 15)))
         {
            startCell(player, index);
         }
         rowEffect(player, index);
      }
   }
   else
   {
      for (index = 0; index < player->song->channels; ++index)
      {
         tickEffect(player, index);
      }
   }
   for (index = 0; index < player->song->channels; ++index)
   {
      updateOutput(player, index);
   }
   if (++player->tick >= player->speed * (1 + player->pattern_delay))
   {
      player->tick = 0;
      player->pattern_delay = 0;
      nextRow(player);
   }
}

/* five times the song's ticks per second, on the frame rate's scale. */
static DeceptusU32 ticksPerSecondScaled(DeceptusU8 bpm)
{
   return (DeceptusU32)bpm * TICKS_PER_BEAT_NUMERATOR * SONG_FRAME_RATE_SCALE;
}

DeceptusU8 songAdvance(SongPlayer *player, DeceptusU16 frame_rate)
{
   DeceptusU32 tick_cost;
   DeceptusU8 ticks;
   tick_cost = (DeceptusU32)frame_rate * TICKS_PER_BEAT_DENOMINATOR;
   player->tick_due += ticksPerSecondScaled(player->bpm);
   ticks = 0;
   while (player->tick_due >= tick_cost)
   {
      player->tick_due -= tick_cost;
      songTick(player);
      ++ticks;
   }
   return ticks;
}
