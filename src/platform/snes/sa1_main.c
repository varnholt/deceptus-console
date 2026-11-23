/* the game loop on the SA-1: it owns the adventure state in BW-RAM and runs what the S-CPU's
 * mailbox requests. the S-CPU keeps pacing, input, video and the presentation of each frame. */
#include "deceptus/adventure.h"
#include "deceptus/game.h"
#include "snes_assets.h"
#include "sa1_mailbox.h"
#include "background.h"

extern const DeceptusAdventureDef snes_tutorial_definition;

DeceptusAdventure snes_sa1_adventure;
DeceptusFrame snes_sa1_frame;

static const DeceptusVideoCaps video = {256, 224, DECEPTUS_DRAW_CAPACITY};

static void advance(volatile SnesSa1Mailbox *mailbox)
{
   DeceptusU16 ticks;
   mailbox->room_changed = 0;
   for (ticks = mailbox->ticks; ticks; --ticks)
   {
      if (deceptusAdventureTick(&snes_sa1_adventure, mailbox->buttons))
      {
         mailbox->room_changed = 1;
         break;
      }
   }
   deceptusRender(&snes_sa1_adventure.campaign.engine, &snes_sa1_frame);
}

static void recordStackLowWater(volatile SnesSa1Mailbox *mailbox)
{
   const volatile DeceptusU8 *stack;
   DeceptusU16 address;
   stack = (const volatile DeceptusU8 *)SNES_SA1_STACK_FLOOR;
   address = SNES_SA1_STACK_FLOOR;
   while ((address < SNES_SA1_STACK_TOP) && (*stack == SNES_SA1_STACK_PATTERN))
   {
      ++stack;
      ++address;
   }
   mailbox->stack_low_water = address;
}

void snesSa1Main(void)
{
   volatile SnesSa1Mailbox *mailbox;
   DeceptusU8 request;
   mailbox = SNES_SA1_MAILBOX;
   while (1)
   {
      request = mailbox->request;
      if (request == mailbox->done)
      {
         continue;
      }
      if (mailbox->command == SNES_SA1_INIT)
      {
         deceptusAdventureInit(&snes_sa1_adventure, &snes_tutorial_definition, &video);
      }
      else if (mailbox->command == SNES_SA1_ADVANCE)
      {
         advance(mailbox);
      }
      else if (mailbox->command == SNES_SA1_LOAD_ROOM)
      {
         snesBackgroundLoad(snes_sa1_adventure.campaign.room_index);
      }
      else if (mailbox->command == SNES_SA1_PREPARE)
      {
         snesBackgroundSetFlags(deceptusAdventurePresentationFlags(&snes_sa1_adventure));
         snesBackgroundPrepare(snes_sa1_frame.camera_x, snes_sa1_frame.camera_y);
      }
      recordStackLowWater(mailbox);
      mailbox->done = request;
   }
}
