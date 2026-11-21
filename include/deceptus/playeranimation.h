#ifndef DECEPTUS_PLAYERANIMATION_H
#define DECEPTUS_PLAYERANIMATION_H

#include "deceptus/adventure.h"
#include "deceptus/game.h"

#define DECEPTUS_APPEARANCE_NONE 0
#define DECEPTUS_APPEARANCE_READY 1
#define DECEPTUS_APPEARANCE_BAD_CONTENT 2
#define DECEPTUS_PLAYER_ANIMATION_INITIALIZER {0, 0, 255}

typedef struct DeceptusPlayerAnimation
{
   DeceptusU16 previous_tick, pose_ticks;
   DeceptusU8 previous_pose;
} DeceptusPlayerAnimation;

typedef struct DeceptusSwordAnimationDef
{
   const DeceptusU8 (*clips)[4];
   const DeceptusU8 *durations;
   DeceptusU16 frame_count;
   DeceptusU8 clip_count;
} DeceptusSwordAnimationDef;

DeceptusU8 deceptusPlayerSwordAppearance(const DeceptusAdventure *game, DeceptusPlayerAnimation *animation,
                                         const DeceptusSwordAnimationDef *definition, DeceptusDraw *draw);

#endif
