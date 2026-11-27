#ifndef AMIGA_AUDIO_H
#define AMIGA_AUDIO_H
#include "deceptus/song.h"
#include "deceptus/soundcue.h"

/* the song on Paula's four channels, stepped from the vertical blank interrupt; sound cues borrow
 * the last channel while they play. song and cue samples live in chip RAM. */

/* the song plays from its start; nothing sounds until the vertical blank handler runs. */
void audioStart(const Song *song);
/* starts the queued cues, then empties the queue; called by the game between frames. */
void audioPlayCues(SoundQueue *queue);
/* one display frame of the song and of Paula's channel restarts; called by the vertical blank handler. */
void audioVerticalBlank(void);
/* silences Paula, before the system takes the machine back. */
void audioStop(void);

#endif
