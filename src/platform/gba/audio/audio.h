#ifndef GBA_AUDIO_H
#define GBA_AUDIO_H
#include "deceptus/song.h"
#include "deceptus/soundcue.h"

/* the song mixer on Direct Sound A: a mid-frame scanline interrupt steps the song and mixes the next
 * frame's samples, so the music keeps time however long the game takes for a frame. */

/* Direct Sound, its timer, the DMA and the scanline interrupt; the song plays from the start. */
void audioStart(const Song* song);
/* starts the queued sound cues on the sound effect voices, then empties the queue. */
void audioPlayCues(SoundQueue* queue);
/* called by the interrupt stub (start.s) for every enabled interrupt. */
void audioInterrupt(void);

#endif
