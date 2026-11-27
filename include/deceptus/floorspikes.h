#ifndef FLOOR_SPIKES_H
#define FLOOR_SPIKES_H
#include "deceptus/mechanisms.h"

/* one tile of the source's spike layers. its definition value is the tile's index in spikes.png
 * (FLOOR_SPIKES_FRAMES columns of animation per orientation row); flags pick the layer's mode, and
 * neither flag means toggle_spikes, which follow the mechanism's enabled state. */
#define FLOOR_SPIKES_INTERVAL 16
#define FLOOR_SPIKES_TRAP 32
#define FLOOR_SPIKES_FRAMES 15
#define FLOOR_SPIKES_ROWS 4
#define FLOOR_SPIKES_IMAGE_COUNT (FLOOR_SPIKES_FRAMES * FLOOR_SPIKES_ROWS)
/* the source atlas row is this many tiles wide: the under-water frames follow the normal ones. */
#define FLOOR_SPIKES_ATLAS_COLUMNS (2 * FLOOR_SPIKES_FRAMES)
/* the frame of a fully retracted spike, which shows nothing. */
#define FLOOR_SPIKES_RETRACTED 14

/* every spike tile of the room starts at its tile's frame. */
void floorSpikesInit(DeceptusMechanismSet* set);
/* one tick of every spike tile; the damage the body at bounds takes from them, zero for none. */
DeceptusU16 floorSpikesTick(DeceptusMechanismSet* set, const DeceptusRect* bounds);
/* the shared sprite family image of the spike tile at mechanism index. */
DeceptusU8 floorSpikesImage(const DeceptusMechanismSet* set, DeceptusU16 index);

#endif
