#include "deceptus/appearance.h"
#include "deceptus/enemies.h"

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

static DeceptusU8 mechanismSprites(const DeceptusEngine *engine, DeceptusAppearanceOutput *output)
{
   const DeceptusMechanismDef *definition;
   const DeceptusMechanismState *state;
   DeceptusU16 index, image, segment, segments;
   DeceptusI16 x, y;
   DeceptusU8 family;
   for (index = 0; index < engine->mechanisms.count; ++index)
   {
      definition = &engine->mechanisms.definitions[index];
      state = &engine->mechanisms.states[index];
      if (!state->visible)
      {
         continue;
      }
      segments = 1;
      image = state->frame;
      x = definition->bounds.x - engine->camera_x;
      y = definition->bounds.y - engine->camera_y;
      if (definition->kind == DECEPTUS_MECH_LEVER)
      {
         family = DECEPTUS_SPRITE_LEVER;
         image = state->handle_available ? state->frame : 32;
      }
      else if (definition->kind == DECEPTUS_MECH_BLOCK && engine->mechanisms.timings &&
               (engine->mechanisms.timings[index].flags & DECEPTUS_TIMED_ANIMATION))
      {
         family = DECEPTUS_SPRITE_BLOCK;
      }
      else if (definition->kind == DECEPTUS_MECH_SPIKES)
      {
         family = DECEPTUS_SPRITE_SPIKES;
      }
      else if (definition->kind == DECEPTUS_MECH_COLLAPSING)
      {
         if (state->frame >= 38 || state->phase == 2)
         {
            continue;
         }
         family = DECEPTUS_SPRITE_COLLAPSING;
         segments = definition->bounds.width / 24;
         y -= 24;
         if (state->phase == 1)
         {
            y += state->timer / 10;
         }
         else if (state->timer)
         {
            y += (DeceptusI16)((state->timer / 2) & 3) - 1;
         }
      }
      else if (definition->kind == DECEPTUS_MECH_DOOR)
      {
         family = DECEPTUS_SPRITE_IRON_DOOR;
         x -= 24;
         y -= 24;
      }
      else if (definition->kind == DECEPTUS_MECH_MOVEABLE_BOX)
      {
         family = DECEPTUS_SPRITE_MOVEABLE_BOX;
         image = 0;
         x = deceptusMechanismBounds(&engine->mechanisms, index)->x - engine->camera_x;
         /* moveablebox.cpp draws the box's lip one tile above its body. */
         y = deceptusMechanismBounds(&engine->mechanisms, index)->y - engine->camera_y - 24;
      }
      else if (definition->kind == DECEPTUS_MECH_DESTRUCTIBLE)
      {
         family = DECEPTUS_SPRITE_WOODEN_PLANKS;
         if (definition->flags & DECEPTUS_MECH_RIGHT_ALIGNED)
         {
            image += 50;
            x -= 102;
         }
      }
      else
      {
         continue;
      }
      for (segment = 0; segment < segments; ++segment)
      {
         if (!addSprite(output, family, image + (family == DECEPTUS_SPRITE_COLLAPSING ? (segment & 3) * 38 : 0), x + segment * 24, y, 0))
         {
            return 0;
         }
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
   if (!mechanismSprites(&game->campaign.engine, output))
   {
      return 0;
   }
   if (definition->rope_sprites)
   {
      return definition->rope_sprites(&game->campaign.engine, output->emit, output->context);
   }
   return ropeSprites(&game->campaign.engine, output);
}
