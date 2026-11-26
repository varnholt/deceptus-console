/* the sprites of the room's mechanisms, the frames and positions the original draws them at. */
#include "deceptus/mechanismparts.h"
#include "deceptus/appearance.h"
#include "deceptus/crusher.h"
#include "deceptus/floorspikes.h"

/* the source's tile size: the offsets below are whole tiles in the original sprite layouts. */
#define SOURCE_TILE 24
/* collapsingplatform.cpp: the platform's blocks, each one tile wide, are drawn a tile above the body
 * and alternate between four block images of COLLAPSE_FRAMES frames each. */
#define COLLAPSE_FRAMES 38
#define COLLAPSE_VARIANTS 4
#define COLLAPSE_FALLEN_FRAME 38
#define COLLAPSE_PHASE_FALLEN 2
#define COLLAPSE_PHASE_FALLING 1
/* the falling blocks move one pixel per this many ticks. */
#define COLLAPSE_FALL_TICKS_PER_PIXEL 10
/* the lever image for a missing handle, after the 32 frames of its switch animation. */
#define LEVER_IMAGE_NO_HANDLE 32
/* destructibleblockingrect.cpp: planks mirrored for a right-aligned barrier follow the 50 left-aligned
 * frames, and their 150px frame begins 102px left of the 48px body. */
#define PLANKS_RIGHT_IMAGES 50
#define PLANKS_RIGHT_SHIFT 102

static DeceptusU8 isCollapsedOrFallen(const DeceptusMechanismState *state)
{
   return (state->frame >= COLLAPSE_FALLEN_FRAME) || (state->phase == COLLAPSE_PHASE_FALLEN);
}

DeceptusU8 mechanismPartCount(const DeceptusEngine *engine, DeceptusU16 index)
{
   const DeceptusMechanismDef *definition;
   const DeceptusMechanismState *state;
   definition = &engine->mechanisms.definitions[index];
   state = &engine->mechanisms.states[index];
   if (!state->visible)
   {
      return 0;
   }
   switch (definition->kind)
   {
   case DECEPTUS_MECH_LEVER:
   case DECEPTUS_MECH_SPIKES:
   case DECEPTUS_MECH_DOOR:
   case DECEPTUS_MECH_DESTRUCTIBLE:
   case DECEPTUS_MECH_MOVEABLE_BOX:
      return 1;
   case DECEPTUS_MECH_BLOCK:
      /* on/off blocks without an animation are part of the background. */
      return ((engine->mechanisms.timings != 0) && (engine->mechanisms.timings[index].flags & DECEPTUS_TIMED_ANIMATION)) ? 1 : 0;
   case DECEPTUS_MECH_COLLAPSING:
      return isCollapsedOrFallen(state) ? 0 : (DeceptusU8)(definition->bounds.width / SOURCE_TILE);
   case MECHANISM_CRUSHER:
      return crusherPartCount(&engine->mechanisms, index);
   case MECHANISM_FLOOR_SPIKES:
      /* fully retracted spikes show nothing. */
      return (state->frame == FLOOR_SPIKES_RETRACTED) ? 0 : 1;
   default:
      return 0;
   }
}

void mechanismPart(const DeceptusEngine *engine, DeceptusU16 index, DeceptusU8 part, MechanismPart *result)
{
   const DeceptusMechanismDef *definition;
   const DeceptusMechanismState *state;
   const DeceptusRect *bounds;
   DeceptusU8 image;
   definition = &engine->mechanisms.definitions[index];
   state = &engine->mechanisms.states[index];
   bounds = deceptusMechanismBounds(&engine->mechanisms, index);
   result->image = state->frame;
   result->x = bounds->x;
   result->y = bounds->y;
   switch (definition->kind)
   {
   case DECEPTUS_MECH_LEVER:
      result->family = DECEPTUS_SPRITE_LEVER;
      result->image = state->handle_available ? state->frame : LEVER_IMAGE_NO_HANDLE;
      break;
   case DECEPTUS_MECH_BLOCK:
      result->family = DECEPTUS_SPRITE_BLOCK;
      break;
   case DECEPTUS_MECH_SPIKES:
      result->family = DECEPTUS_SPRITE_SPIKES;
      break;
   case DECEPTUS_MECH_COLLAPSING:
      result->family = DECEPTUS_SPRITE_COLLAPSING;
      result->image = (DeceptusU16)(state->frame + (part % COLLAPSE_VARIANTS) * COLLAPSE_FRAMES);
      result->x += part * SOURCE_TILE;
      result->y -= SOURCE_TILE;
      if (state->phase == COLLAPSE_PHASE_FALLING)
      {
         result->y += state->timer / COLLAPSE_FALL_TICKS_PER_PIXEL;
      }
      else if (state->timer)
      {
         /* updateShakeBlocks: each block shakes a step out of phase with its neighbour. */
         result->y += (DeceptusI16)((state->timer / 2 + part) & 3) - 1;
      }
      break;
   case DECEPTUS_MECH_DOOR:
      result->family = DECEPTUS_SPRITE_IRON_DOOR;
      result->x -= SOURCE_TILE;
      result->y -= SOURCE_TILE;
      break;
   case DECEPTUS_MECH_DESTRUCTIBLE:
      result->family = DECEPTUS_SPRITE_WOODEN_PLANKS;
      if (definition->flags & DECEPTUS_MECH_RIGHT_ALIGNED)
      {
         result->image += PLANKS_RIGHT_IMAGES;
         result->x -= PLANKS_RIGHT_SHIFT;
      }
      break;
   case DECEPTUS_MECH_MOVEABLE_BOX:
      /* moveablebox.cpp draws the box's lip one tile above its body. */
      result->family = DECEPTUS_SPRITE_MOVEABLE_BOX;
      result->image = 0;
      result->y -= SOURCE_TILE;
      break;
   case MECHANISM_FLOOR_SPIKES:
      result->family = SPRITE_FLOOR_SPIKES;
      result->image = floorSpikesImage(&engine->mechanisms, index);
      break;
   case MECHANISM_CRUSHER:
      result->family = SPRITE_CRUSHER;
      crusherPart(&engine->mechanisms, index, part, &image, &result->x, &result->y);
      result->image = image;
      break;
   default:
      break;
   }
}
