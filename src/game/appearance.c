#include "deceptus/appearance.h"
#include "deceptus/enemies.h"
#include "deceptus/mechanismparts.h"

typedef struct DeceptusAppearanceOutput
{
   DeceptusSpriteEmitter emit;
   void *context;
} DeceptusAppearanceOutput;

static DeceptusU8 addSprite(DeceptusAppearanceOutput *output, DeceptusU8 family, DeceptusU16 frame, DeceptusI16 x, DeceptusI16 y,
                            DeceptusU8 flip)
{
   DeceptusSpriteAppearance sprite;
   sprite.family = family;
   sprite.frame = frame;
   sprite.x = x;
   sprite.y = y;
   sprite.flip_x = flip;
   return output->emit(output->context, &sprite);
}

static DeceptusU8 playerSprites(const DeceptusAdventure *game, const DeceptusDraw *draw, const DeceptusAppearanceDef *definition,
                                DeceptusPlayerAnimation *animation, DeceptusAppearanceOutput *output)
{
   const DeceptusEngine *engine;
   const DeceptusHeadtorchPose *pose;
   DeceptusDraw sword;
   DeceptusI16 x, y, helmet_x, helmet_y;
   DeceptusU8 family, image, flip;
   engine = &game->campaign.engine;
   image = draw->frame;
   x = draw->x;
   y = draw->y;
   flip = draw->flip_x;
   pose = 0;
   if (game->has_sword)
   {
      if (deceptusPlayerSwordAppearance(game, animation, &definition->sword, &sword) != DECEPTUS_APPEARANCE_READY)
      {
         return 0;
      }
      image = sword.frame;
      x = sword.x;
      y = sword.y;
      flip = 0;
      family = DECEPTUS_SPRITE_PLAYER_SWORD;
      if (definition->sword_poses)
      {
         pose = &definition->sword_poses[image];
      }
   }
   else if (draw->kind == DECEPTUS_PLAYER_SWIM && image < 26)
   {
      family = DECEPTUS_SPRITE_PLAYER_SWIM;
      if (definition->swim)
      {
         pose = &definition->swim[image];
      }
   }
   else if (draw->kind == DECEPTUS_PLAYER_ROPE && image < 6)
   {
      family = DECEPTUS_SPRITE_PLAYER_ROPE;
      if (definition->rope)
      {
         pose = &definition->rope[image];
      }
   }
   else if (draw->kind == DECEPTUS_PLAYER && image < 26)
   {
      family = DECEPTUS_SPRITE_PLAYER;
      if (definition->normal)
      {
         pose = &definition->normal[engine->entities[0].facing_left ? 1 : 0][image];
      }
   }
   else
   {
      return 0;
   }
   if (deceptusItemHeadtorchEquipped(game) && pose && pose->valid)
   {
      if ((pose->side > 1) || (definition->lamp_offsets == 0))
      {
         return 0;
      }
      helmet_x = deceptusFloor(engine->entities[0].x) - engine->camera_x + pose->x;
      helmet_y = deceptusFloor(engine->entities[0].y) - engine->camera_y + pose->y;
      if ((addSprite(output, DECEPTUS_SPRITE_HEADTORCH, pose->side, helmet_x, helmet_y, 0) == 0) ||
          (addSprite(output, DECEPTUS_SPRITE_HEADTORCH_BEAM, 0, helmet_x + definition->lamp_offsets[pose->side][0] - (pose->side ? 64 : 0),
                     helmet_y + definition->lamp_offsets[pose->side][1] - 16, pose->side) == 0))
      {
         return 0;
      }
   }
   return addSprite(output, family, image, x, y, flip);
}

static DeceptusU8 mechanismIndexSprites(const DeceptusEngine *engine, DeceptusAppearanceOutput *output, DeceptusU16 index)
{
   MechanismPart piece;
   DeceptusU8 part, parts;
   parts = mechanismPartCount(engine, index);
   for (part = 0; part < parts; ++part)
   {
      mechanismPart(engine, index, part, &piece);
      if (!addSprite(output, piece.family, piece.image, piece.x - engine->camera_x, piece.y - engine->camera_y, 0))
      {
         return 0;
      }
   }
   return 1;
}

/* sprites emitted earlier are drawn in front of later ones: the mechanisms in front of the player
 * come before her, from the list the room start keeps of them. */
static DeceptusU8 frontMechanismSprites(const DeceptusEngine *engine, DeceptusAppearanceOutput *output)
{
   DeceptusU8 front;
   for (front = 0; front < engine->mechanisms.front_count; ++front)
   {
      if (!mechanismIndexSprites(engine, output, engine->mechanisms.front_indices[front]))
      {
         return 0;
      }
   }
   return 1;
}

/* the mechanisms behind the player, after her. the definitions are walked by pointer: indexing them
 * multiplies by their size on every step on the 65816. */
static DeceptusU8 backMechanismSprites(const DeceptusEngine *engine, DeceptusAppearanceOutput *output)
{
   const DeceptusMechanismDef *definition;
   DeceptusU16 index;
   definition = engine->mechanisms.definitions;
   for (index = 0; index < engine->mechanisms.count; ++index, ++definition)
   {
      if (definition->flags & MECHANISM_IN_FRONT)
      {
         continue;
      }
      if (!mechanismIndexSprites(engine, output, index))
      {
         return 0;
      }
   }
   return 1;
}

static DeceptusU8 ropeSprites(const DeceptusEngine *engine, DeceptusAppearanceOutput *output)
{
   DeceptusU16 distance, length, height;
   DeceptusU8 rope, node;
   DeceptusI16 x, y;
   for (rope = 0; rope < engine->grab_ropes.count; ++rope)
   {
      length = deceptusGrabRopeLength(engine, rope);
      node = 1;
      for (distance = 0; distance < length; distance += height)
      {
         height = distance ? 8 : 16;
         if (height > length - distance)
         {
            height = length - distance;
         }
         deceptusGrabRopePointFrom(engine, rope, distance, &node, &x, &y);
         if (!addSprite(output, DECEPTUS_SPRITE_GRAB_ROPE, distance ? height : 0, x - engine->camera_x - 7,
                        y - engine->camera_y - (distance ? 4 : 0), 0))
         {
            return 0;
         }
      }
   }
   return 1;
}

DeceptusU8 deceptusAppearanceVisit(const DeceptusAdventure *game, const DeceptusFrame *frame, const DeceptusAppearanceDef *definition,
                                   DeceptusPlayerAnimation *animation, DeceptusSpriteEmitter emit, void *context)
{
   const DeceptusDraw *draw;
   DeceptusU8 index, family;
   DeceptusAppearanceOutput destination;
   DeceptusAppearanceOutput *output;
   if (!emit)
   {
      return 0;
   }
   destination.emit = emit;
   destination.context = context;
   output = &destination;
   if (game->campaign.mode == DECEPTUS_CAMPAIGN_TITLE)
   {
      return 1;
   }
   if (!frontMechanismSprites(&game->campaign.engine, output))
   {
      return 0;
   }
   for (index = 0; index < frame->count; ++index)
   {
      draw = &frame->draws[index];
      if (draw->kind == DECEPTUS_PLAYER || draw->kind == DECEPTUS_PLAYER_SWIM || draw->kind == DECEPTUS_PLAYER_ROPE)
      {
         if (!playerSprites(game, draw, definition, animation, output))
         {
            return 0;
         }
         continue;
      }
      switch (draw->kind)
      {
      case DECEPTUS_RAT:
         family = DECEPTUS_SPRITE_RAT;
         break;
      case DECEPTUS_BLOB_2:
         family = DECEPTUS_SPRITE_BLOB;
         break;
      case DECEPTUS_ARROW_TRAP:
         family = DECEPTUS_SPRITE_ARROW_TRAP;
         break;
      case DECEPTUS_ARROW_MUZZLE:
         family = DECEPTUS_SPRITE_ARROW_MUZZLE;
         break;
      case DECEPTUS_ARROW:
         family = DECEPTUS_SPRITE_ARROW;
         break;
      case DECEPTUS_CANNON_2:
         family = DECEPTUS_SPRITE_CANNON;
         break;
      case DECEPTUS_CANNONBALL:
         family = DECEPTUS_SPRITE_CANNONBALL;
         break;
      case DECEPTUS_ORB:
         family = DECEPTUS_SPRITE_ORB;
         break;
      case DECEPTUS_FROG_BLUE:
         family = DECEPTUS_SPRITE_FROG_BLUE;
         break;
      case DECEPTUS_BLUE_FIREBALL:
         family = DECEPTUS_SPRITE_BLUE_FIREBALL;
         break;
      case DECEPTUS_WATER_SPLASH:
         family = DECEPTUS_SPRITE_SPLASH;
         break;
      default:
         return 0;
      }
      if (!addSprite(output, family, draw->frame, draw->x, draw->y, draw->flip_x))
      {
         return 0;
      }
   }
   if (!backMechanismSprites(&game->campaign.engine, output))
   {
      return 0;
   }
   if (definition->rope_sprites)
   {
      return definition->rope_sprites(&game->campaign.engine, output->emit, output->context);
   }
   return ropeSprites(&game->campaign.engine, output);
}
