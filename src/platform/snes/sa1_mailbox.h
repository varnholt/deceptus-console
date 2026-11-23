#ifndef SNES_SA1_MAILBOX_H
#define SNES_SA1_MAILBOX_H

#include "deceptus/adventure.h"

/* commands the S-CPU hands to the SA-1, which runs the game. */
#define SNES_SA1_INIT 1
#define SNES_SA1_ADVANCE 2
/* the current room's background data, after the S-CPU set up its video side. */
#define SNES_SA1_LOAD_ROOM 3
/* scenery flags and the background window for the rendered frame. */
#define SNES_SA1_PREPARE 4

/* the mailbox lives in I-RAM, which both CPUs address at 00:3100 (the SA-1 also at 00:0100).
 * the S-CPU fills a request and raises request; the SA-1 answers by setting done to it. */
typedef struct SnesSa1Mailbox
{
   /* bytes, so neither cpu ever reads a half-written sequence number. */
   DeceptusU8 request, done;
   DeceptusU16 command;
   /* advance: the pad state and the number of simulation ticks to run. */
   DeceptusU16 buttons, ticks;
   /* advance: nonzero when a tick entered another room; the remaining ticks were not run. */
   DeceptusU16 room_changed;
   /* the lowest SA-1 stack address the stack has reached so far, for the emulator checks. */
   DeceptusU16 stack_low_water;
} SnesSa1Mailbox;

#define SNES_SA1_MAILBOX ((volatile SnesSa1Mailbox *)0x3100)

/* the SA-1 stack (sa1_boot.asm): I-RAM $140-$7ff, at $3140-$37ff for both cpus, filled with a
 * pattern at start-up, so the lowest overwritten byte shows how deep the stack has grown. */
#define SNES_SA1_STACK_FLOOR 0x3140
#define SNES_SA1_STACK_TOP 0x37FF
#define SNES_SA1_STACK_PATTERN 0xA5

/* the game state the SA-1 owns, in BW-RAM; the S-CPU reads it through these addresses only. */
extern DeceptusAdventure snes_sa1_adventure;
extern DeceptusFrame snes_sa1_frame;

#endif
