#ifndef ROPE_LINK_H
#define ROPE_LINK_H
#include "deceptus/types.h"

/* the grab rope is drawn as straight pieces between points along it, each turned to its direction:
 * image k of a piece runs at -90 + 11.25 k degrees from straight down towards the right, so image
 * ROPE_LINK_ANGLES / 2 hangs straight down. the plain link images come first, then the knot piece
 * at the top of the rope; all are ROPE_LINK_CELL square around the piece's centre. */
#define ROPE_LINK_ANGLES 17
#define ROPE_LINK_IMAGE_KNOT ROPE_LINK_ANGLES
#define ROPE_LINK_IMAGE_COUNT (2 * ROPE_LINK_ANGLES)
#define ROPE_LINK_CELL 24
/* rope length per plain link and for the knot piece, in pixels along the rope. */
#define ROPE_LINK_LENGTH 8
#define ROPE_KNOT_LENGTH 16

/* the angle image for a piece running dx, dy; a piece looks the same in both directions. */
DeceptusU8 ropeLinkAngle(DeceptusI16 dx, DeceptusI16 dy);
/* the cell's top left for a piece from x0, y0 to x1, y1 that covers covered px of a piece drawn
 * full px long: centred on its points, and a shorter last piece ending at x1, y1. */
void ropeLinkCell(
   DeceptusI16 x0,
   DeceptusI16 y0,
   DeceptusI16 x1,
   DeceptusI16 y1,
   DeceptusU16 covered,
   DeceptusU16 full,
   DeceptusI16* x,
   DeceptusI16* y
);

#endif
