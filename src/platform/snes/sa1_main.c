/* the game loop on the SA-1: it owns the adventure state in BW-RAM and runs what the S-CPU's
 * mailbox requests. the S-CPU keeps pacing, input, video and the presentation of each frame. */
#include "background.h"
#include "deceptus/adventure.h"
#include "deceptus/game.h"
#include "sa1_mailbox.h"
#include "snes_assets.h"
#include "sprites.h"
#include "storm.h"

extern const DeceptusAdventureDef snes_tutorial_definition;

DeceptusAdventure snes_sa1_adventure;
DeceptusFrame snes_sa1_frame;

static const DeceptusVideoCaps video = {256, 224, DECEPTUS_DRAW_CAPACITY};

static void advance(volatile SnesSa1Mailbox* mailbox)
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

static void prepare(DeceptusU8 buffer)
{
   snesBackgroundSetFlags(deceptusAdventurePresentationFlags(&snes_sa1_adventure));
   snesBackgroundPrepare(snes_sa1_frame.camera_x, snes_sa1_frame.camera_y);
   snesBackgroundStage(buffer);
   snesSpritesPrepare(&snes_sa1_adventure, &snes_sa1_frame);
   snesSpritesStage(buffer);
   snesStormStage(&snes_sa1_adventure, buffer);
}

/* the deepest overwritten stack byte found so far; zero until the first request. */
static DeceptusU16 stack_low_water;

/* a deeper stack shows in the bytes just below the previous low-water mark, so only a window there
 * is read: a scan of the whole stack before every answer cost a third of the SA-1's time. a frame
 * that leaves more than a window of untouched bytes below it is reported shallower. */
static void recordStackLowWater(volatile SnesSa1Mailbox* mailbox)
{
   const volatile DeceptusU8* floor;
   DeceptusU16 address, previous;
   floor = (const volatile DeceptusU8*)SNES_SA1_STACK_FLOOR;
   if (stack_low_water == 0)
   {
      stack_low_water = SNES_SA1_STACK_TOP;
   }
   do
   {
      previous = stack_low_water;
      address = previous;
      while ((address > SNES_SA1_STACK_FLOOR) && (address + SNES_SA1_STACK_WINDOW > previous))
      {
         --address;
         if (floor[address - SNES_SA1_STACK_FLOOR] != SNES_SA1_STACK_PATTERN)
         {
            stack_low_water = address;
         }
      }
   } while (stack_low_water != previous);
   mailbox->stack_low_water = stack_low_water;
}

void snesSa1Main(void)
{
   volatile SnesSa1Mailbox* mailbox;
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
      else if (mailbox->command == SNES_SA1_PRESENT)
      {
         advance(mailbox);
         if (mailbox->room_changed == 0)
         {
            prepare((DeceptusU8)mailbox->buffer);
         }
      }
      else if (mailbox->command == SNES_SA1_LOAD_ROOM)
      {
         snesBackgroundLoad(snes_sa1_adventure.campaign.room_index);
         snesSpritesReset();
         snesStormReset();
      }
      else if (mailbox->command == SNES_SA1_PREPARE)
      {
         prepare((DeceptusU8)mailbox->buffer);
      }
      mailbox->done = request;
      /* after the answer, while the S-CPU presents; it reads the value with the next answer. */
      recordStackLowWater(mailbox);
   }
}
