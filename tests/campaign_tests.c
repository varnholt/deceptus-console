#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "deceptus/campaign.h"
#define CHECK(value)                                         \
   do                                                        \
   {                                                         \
      if (!(value))                                          \
      {                                                      \
         fprintf(stderr, "line %d: %s\n", __LINE__, #value); \
         exit(1);                                            \
      }                                                      \
   } while (0)
int main(void)
{
   static DeceptusU8 cells[32 * 28];
   static DeceptusRoom first, second;
   static const DeceptusRoom* rooms[2];
   static const DeceptusCampaignGoal goals[2] = {{{32, 144, 48, 64}, 77}, {{32, 144, 48, 64}, 0}};
   static const DeceptusMechanismDef flag = {77, {100, 100, 24, 24}, 0, 0, 0, DECEPTUS_MECH_FLAG, 1, 0};
   DeceptusCampaignDef definition;
   DeceptusCampaign game;
   DeceptusVideoCaps video = {240, 160, 24};
   DeceptusU16 index;
   for (index = 0; index < 32; ++index)
   {
      cells[24 * 32 + index] = DECEPTUS_SOLID;
   }
   memset(&first, 0, sizeof(first));
   first.collision = cells;
   first.columns = 32;
   first.rows = 28;
   first.spawn_x = 40;
   first.spawn_y = 192;
   first.patrol_x = -1;
   first.door_x = -1;
   first.max_health = 100;
   first.mechanisms = &flag;
   first.mechanism_count = 1;
   second = first;
   second.mechanisms = 0;
   second.mechanism_count = 0;
   rooms[0] = &first;
   rooms[1] = &second;
   definition.rooms = rooms;
   definition.goals = goals;
   definition.room_count = 2;
   deceptusCampaignInit(&game, &definition, &video);
   CHECK(game.mode == DECEPTUS_CAMPAIGN_TITLE);
   deceptusCampaignTick(&game, DECEPTUS_UP);
   CHECK(game.room_index == 0 && game.elapsed_ticks == 0);
   deceptusCampaignTick(&game, DECEPTUS_PAUSE);
   CHECK(game.mode == DECEPTUS_CAMPAIGN_PLAYING);
   CHECK(!deceptusCampaignAtGoal(&game));
   deceptusCampaignTick(&game, DECEPTUS_UP);
   CHECK(game.room_index == 0);
   deceptusMechanismEnable(&game.engine.mechanisms, 77, 0);
   CHECK(deceptusCampaignAtGoal(&game));
   deceptusCampaignTick(&game, 0);
   CHECK(deceptusCampaignTick(&game, DECEPTUS_UP) == 1 && game.room_index == 1);
   deceptusCampaignTick(&game, DECEPTUS_UP);
   CHECK(game.mode == DECEPTUS_CAMPAIGN_READY);
   deceptusCampaignTick(&game, DECEPTUS_PAUSE);
   CHECK(game.mode == DECEPTUS_CAMPAIGN_PLAYING);
   deceptusCampaignTick(&game, 0);
   deceptusCampaignTick(&game, DECEPTUS_UP);
   CHECK(game.mode == DECEPTUS_CAMPAIGN_FINISHED);
   index = game.seconds;
   deceptusCampaignTick(&game, 0);
   deceptusCampaignTick(&game, DECEPTUS_RIGHT);
   CHECK(game.seconds == index);
   CHECK(deceptusCampaignTick(&game, DECEPTUS_PAUSE) == 1 && game.mode == DECEPTUS_CAMPAIGN_PLAYING && game.room_index == 0);
   CHECK(game.engine.video.width == 240 && game.video.height == 160 && game.elapsed_ticks == 0);
   deceptusCampaignTick(&game, 0);
   deceptusCampaignTick(&game, DECEPTUS_RESTART);
   CHECK(game.restarts == 1 && game.room_index == 0);
   deceptusCampaignTick(&game, 0);
   deceptusCampaignTick(&game, DECEPTUS_PAUSE);
   CHECK(game.engine.paused);
   index = (DeceptusU16)game.elapsed_ticks;
   deceptusCampaignTick(&game, DECEPTUS_RIGHT);
   CHECK(game.elapsed_ticks == index);
   deceptusCampaignInit(&game, 0, &video);
   CHECK(game.engine.content_error == 7);
   puts("campaign title, goal prerequisites, room transitions, finish, replay and pause checks passed");
   return 0;
}
