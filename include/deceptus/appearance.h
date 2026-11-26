#ifndef DECEPTUS_APPEARANCE_H
#define DECEPTUS_APPEARANCE_H
#include "deceptus/itemheadtorch.h"
#include "deceptus/playeranimation.h"

#define DECEPTUS_SPRITE_PLAYER 0
#define DECEPTUS_SPRITE_LEVER 1
#define DECEPTUS_SPRITE_SPIKES 2
#define DECEPTUS_SPRITE_BLOCK 3
#define DECEPTUS_SPRITE_COLLAPSING 4
#define DECEPTUS_SPRITE_RAT 5
#define DECEPTUS_SPRITE_ARROW_TRAP 6
#define DECEPTUS_SPRITE_ARROW_MUZZLE 7
#define DECEPTUS_SPRITE_ARROW 8
#define DECEPTUS_SPRITE_PLAYER_SWIM 9
#define DECEPTUS_SPRITE_BLOB 10
#define DECEPTUS_SPRITE_SPLASH 11
#define DECEPTUS_SPRITE_PLAYER_SWORD 12
#define DECEPTUS_SPRITE_CANNON 13
#define DECEPTUS_SPRITE_CANNONBALL 14
#define DECEPTUS_SPRITE_IRON_DOOR 15
#define DECEPTUS_SPRITE_WOODEN_PLANKS 16
#define DECEPTUS_SPRITE_HEADTORCH 17
#define DECEPTUS_SPRITE_HEADTORCH_BEAM 18
#define DECEPTUS_SPRITE_ORB 19
#define DECEPTUS_SPRITE_FROG_BLUE 20
#define DECEPTUS_SPRITE_BLUE_FIREBALL 21
#define DECEPTUS_SPRITE_GRAB_ROPE 22
#define DECEPTUS_SPRITE_PLAYER_ROPE 23
#define DECEPTUS_SPRITE_MOVEABLE_BOX 24
#define DECEPTUS_SPRITE_FAMILY_COUNT 25

/* logical original-source frames; target palettes, tiles and DMA stay in backends. */
typedef struct DeceptusSpriteAppearance
{
   DeceptusI16 x, y;
   DeceptusU16 frame;
   DeceptusU8 family, flip_x;
} DeceptusSpriteAppearance;

typedef DeceptusU8 (*DeceptusSpriteEmitter)(void *context, const DeceptusSpriteAppearance *sprite);

typedef struct DeceptusAppearanceDef
{
   DeceptusSwordAnimationDef sword;
   const DeceptusHeadtorchPose (*normal)[26];
   const DeceptusHeadtorchPose *swim, *rope, *sword_poses;
   const DeceptusI16 (*lamp_offsets)[2];
   /* a platform's own drawing of the grab ropes, which must emit what the shared one does; zero for
    * the shared one. */
   DeceptusU8 (*rope_sprites)(const struct DeceptusEngine *engine, DeceptusSpriteEmitter emit, void *context);
} DeceptusAppearanceDef;

DeceptusU8 deceptusAppearanceVisit(const DeceptusAdventure *game, const DeceptusFrame *frame, const DeceptusAppearanceDef *definition,
                                   DeceptusPlayerAnimation *animation, DeceptusSpriteEmitter emit, void *context);
#endif
