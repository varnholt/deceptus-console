#include "deceptus/game.h"
#include "deceptus/campaign.h"
#include <string.h>

void deceptusCampaignInit(DeceptusCampaign *campaign, const DeceptusCampaignDef *definition, const DeceptusVideoCaps *video)
{
   DeceptusVideoCaps saved_video;
   saved_video = *video;
   memset(campaign, 0, sizeof(*campaign));
   campaign->definition = definition;
   campaign->video = saved_video;
   if ((definition == 0) || (definition->room_count == 0) || (definition->rooms == 0) || (definition->goals == 0))
   {
      campaign->engine.content_error = 7;
      return;
   }
   deceptusInit(&campaign->engine, definition->rooms[0], &saved_video);
   deceptusTick(&campaign->engine, 0);
}

DeceptusU8 deceptusCampaignAtGoal(const DeceptusCampaign *campaign)
{
   const DeceptusRect *goal;
   DeceptusI16 x, y, slot;
   if (campaign->mode != DECEPTUS_CAMPAIGN_PLAYING)
   {
      return 0;
   }
   goal = &campaign->definition->goals[campaign->room_index].bounds;
   x = deceptusFloor(campaign->engine.entities[0].x);
   y = deceptusFloor(campaign->engine.entities[0].y);
   if (x + 8 <= goal->x || x - 8 >= goal->x + goal->width || y <= goal->y || y - 30 >= goal->y + goal->height)
   {
      return 0;
   }
   if (campaign->definition->goals[campaign->room_index].disabled_mechanism)
   {
      slot = deceptusMechanismFind(&campaign->engine.mechanisms, campaign->definition->goals[campaign->room_index].disabled_mechanism);
      if (slot < 0 || campaign->engine.mechanisms.states[slot].enabled)
      {
         return 0;
      }
   }
   return 1;
}

DeceptusU8 deceptusCampaignTick(DeceptusCampaign *campaign, DeceptusU16 buttons)
{
   DeceptusU16 pressed, deaths;
   pressed = buttons & ~campaign->previous_buttons;
   campaign->previous_buttons = buttons;
   if (campaign->engine.content_error)
   {
      return 0;
   }
   if (campaign->mode == DECEPTUS_CAMPAIGN_TITLE || campaign->mode == DECEPTUS_CAMPAIGN_READY)
   {
      if (pressed & (DECEPTUS_PAUSE | DECEPTUS_JUMP))
      {
         campaign->mode = DECEPTUS_CAMPAIGN_PLAYING;
         campaign->engine.previous_buttons = buttons;
      }
      return 0;
   }
   if (campaign->mode == DECEPTUS_CAMPAIGN_FINISHED)
   {
      if (pressed & (DECEPTUS_PAUSE | DECEPTUS_RESTART))
      {
         deceptusCampaignInit(campaign, campaign->definition, &campaign->video);
         campaign->previous_buttons = buttons;
         campaign->mode = DECEPTUS_CAMPAIGN_PLAYING;
         campaign->engine.previous_buttons = buttons;
         return 1;
      }
      return 0;
   }
   deaths = campaign->engine.deaths;
   if (pressed & DECEPTUS_RESTART)
   {
      ++campaign->restarts;
   }
   deceptusTick(&campaign->engine, buttons);
   if (campaign->engine.deaths > deaths)
   {
      campaign->deaths += campaign->engine.deaths - deaths;
   }
   if (!campaign->engine.paused)
   {
      ++campaign->elapsed_ticks;
      if (++campaign->subsecond >= 60)
      {
         campaign->subsecond = 0;
         ++campaign->seconds;
      }
   }
   if (!campaign->engine.paused && (pressed & DECEPTUS_UP) && deceptusCampaignAtGoal(campaign))
   {
      if (campaign->room_index + 1 >= campaign->definition->room_count)
      {
         campaign->mode = DECEPTUS_CAMPAIGN_FINISHED;
      }
      else
      {
         ++campaign->room_index;
         campaign->mode = DECEPTUS_CAMPAIGN_READY;
         deceptusInit(&campaign->engine, campaign->definition->rooms[campaign->room_index], &campaign->video);
         campaign->engine.previous_buttons = buttons;
         return 1;
      }
   }
   return 0;
}
