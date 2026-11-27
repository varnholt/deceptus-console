#ifndef SMD_AUDIO_H
#define SMD_AUDIO_H
#include "deceptus/song.h"
#include "deceptus/soundcue.h"

/* the song and the sound cues on the YM2612's DAC. the Z80 mixes the channels (audio_driver.s80); a
 * horizontal interrupt in the middle of the picture steps the song once a frame and passes the
 * channels to it, so the vertical blank stays free for the video transfers. sound cues borrow the
 * last channel while they play. */

/* loads the Z80 driver and plays the song from its start. */
void audioStart(const Song* song);
/* switches to another song from its start; a null song is silence. a playing cue plays on. */
void audioPlaySong(const Song* song);
/* the video transfers of the next vertical blanks follow: from that blank the Z80 keeps off the
 * cartridge, whose reads would stop it while the DMA owns the bus, until audioTransfersEnd. */
void audioTransfersBegin(void);
/* the video transfers are done. */
void audioTransfersEnd(void);
/* starts the queued cues, then empties the queue; called by the game between frames. */
void audioPlayCues(SoundQueue* queue);

#endif
