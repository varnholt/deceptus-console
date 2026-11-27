#ifndef DECEPTUS_CAMPAIGN_H
#define DECEPTUS_CAMPAIGN_H
#include "deceptus/engine.h"
#define DECEPTUS_CAMPAIGN_TITLE 0
#define DECEPTUS_CAMPAIGN_PLAYING 1
#define DECEPTUS_CAMPAIGN_FINISHED 2
#define DECEPTUS_CAMPAIGN_READY 3

typedef struct DeceptusCampaignGoal
{
   DeceptusRect bounds;
   DeceptusU16 disabled_mechanism;
} DeceptusCampaignGoal;
typedef struct DeceptusCampaignDef
{
   const DeceptusRoom* const* rooms;
   const DeceptusCampaignGoal* goals;
   DeceptusU8 room_count;
} DeceptusCampaignDef;
typedef struct DeceptusCampaign
{
   DeceptusEngine engine;
   const DeceptusCampaignDef* definition;
   DeceptusVideoCaps video;
   DeceptusU32 elapsed_ticks;
   DeceptusU16 deaths, restarts, previous_buttons, seconds;
   DeceptusU8 subsecond;
   DeceptusU8 room_index, mode;
} DeceptusCampaign;

void deceptusCampaignInit(DeceptusCampaign* campaign, const DeceptusCampaignDef* definition, const DeceptusVideoCaps* video);
/* returns one when platform resources must change to the current room. */
DeceptusU8 deceptusCampaignTick(DeceptusCampaign* campaign, DeceptusU16 buttons);
DeceptusU8 deceptusCampaignAtGoal(const DeceptusCampaign* campaign);
#endif
