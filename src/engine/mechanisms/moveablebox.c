/* original counterpart: src/game/mechanisms/moveablebox.cpp. there the box is a Box2D body the player
 * pushes; here it moves pixel by pixel through the collision cells, the solid mechanisms, the other
 * boxes and the box colliders, which stop boxes only. */
#include "deceptus/moveablebox.h"
#include "deceptus/physics.h"

/* the player's default gravity and fall speed limit (deceptus.c): the source body has gravity scale 1. */
#define BOX_GRAVITY 4
#define BOX_MAX_FALL 80
/* a box moves at this fraction of the pushing body's speed. the source has no fixed ratio: its Box2D
 * contact depends on the box's density and friction, so this is the console engine's choice. */
#define BOX_PUSH_DIVISOR 2

DeceptusU8 deceptusMoveableBoxesInit(DeceptusEngine* engine)
{
   DeceptusMechanismSet* set;
   DeceptusMoveableBox* box;
   const DeceptusMechanismDef* definition;
   DeceptusU8 index;
   set = &engine->mechanisms;
   set->box_count = 0;
   for (index = 0; index < set->count; ++index)
   {
      definition = &set->definitions[index];
      if (definition->kind != DECEPTUS_MECH_MOVEABLE_BOX)
      {
         continue;
      }
      if (set->box_count == DECEPTUS_MOVEABLE_BOX_CAPACITY)
      {
         return 0;
      }
      box = &set->boxes[set->box_count++];
      box->bounds = definition->bounds;
      box->x = DECEPTUS_FIXED(definition->bounds.x);
      box->y = DECEPTUS_FIXED(definition->bounds.y);
      box->velocity_y = 0;
      box->mechanism_index = index;
   }
   return 1;
}

/* whether the box fits with its top left corner at x, y. a box moving down lands on a one-way cell
 * whose top its bottom edge had reached, as the player does. */
static DeceptusU8
boxFits(const DeceptusEngine* engine, const DeceptusMoveableBox* box, DeceptusI16 x, DeceptusI16 y, DeceptusU8 moving_down)
{
   DeceptusRect bounds;
   DeceptusI16 column, row, left, right, top, bottom;
   DeceptusU8 cell;
   left = deceptusPositionCell(DECEPTUS_FIXED(x));
   right = deceptusPositionCell(DECEPTUS_FIXED(x + box->bounds.width - 1));
   top = deceptusPositionCell(DECEPTUS_FIXED(y));
   bottom = deceptusPositionCell(DECEPTUS_FIXED(y + box->bounds.height - 1));
   for (row = top; row <= bottom; ++row)
   {
      for (column = left; column <= right; ++column)
      {
         cell = deceptusCell(engine->room, column, row);
         if ((cell & DECEPTUS_SOLID) != 0)
         {
            return 0;
         }
         if (moving_down && ((cell & DECEPTUS_ONE_WAY) != 0) && (row == bottom) && (y - 1 + box->bounds.height <= row * DECEPTUS_CELL_SIZE))
         {
            return 0;
         }
      }
   }
   bounds.x = x;
   bounds.y = y;
   bounds.width = box->bounds.width;
   bounds.height = box->bounds.height;
   return deceptusMechanismBoxSolid(&engine->mechanisms, &bounds, box->mechanism_index) == 0;
}

/* moves the box by distance along one axis, a pixel at a time; zero when something stopped it, which
 * leaves it where it last fitted. */
static DeceptusU8 moveBox(const DeceptusEngine* engine, DeceptusMoveableBox* box, DeceptusFixed distance, DeceptusU8 vertical)
{
   DeceptusFixed* position;
   DeceptusI16 pixel, target, step;
   position = vertical ? &box->y : &box->x;
   pixel = deceptusFloor(*position);
   target = deceptusFloor(*position + distance);
   step = (target > pixel) ? 1 : -1;
   while (pixel != target)
   {
      if (!boxFits(
             engine,
             box,
             vertical ? box->bounds.x : pixel + step,
             vertical ? pixel + step : box->bounds.y,
             (DeceptusU8)(vertical && (step > 0))
          ))
      {
         /* only whole pixels moved: the fraction stays, so a body against the box stays outside it. */
         *position = (step > 0) ? DECEPTUS_FIXED(pixel + 1) - 1 : DECEPTUS_FIXED(pixel);
         return 0;
      }
      pixel += step;
      if (vertical)
      {
         box->bounds.y = pixel;
      }
      else
      {
         box->bounds.x = pixel;
      }
   }
   *position += distance;
   return 1;
}

void deceptusMoveableBoxesFall(DeceptusEngine* engine)
{
   DeceptusMoveableBox* box;
   DeceptusU8 index;
   for (index = 0; index < engine->mechanisms.box_count; ++index)
   {
      box = &engine->mechanisms.boxes[index];
      box->velocity_y += BOX_GRAVITY;
      if (box->velocity_y > BOX_MAX_FALL)
      {
         box->velocity_y = BOX_MAX_FALL;
      }
      if (!moveBox(engine, box, box->velocity_y, 1))
      {
         box->velocity_y = 0;
      }
   }
}

void deceptusMoveableBoxesPush(DeceptusEngine* engine, const DeceptusEntity* body, DeceptusU8 half_width, DeceptusU8 height)
{
   DeceptusMoveableBox* box;
   DeceptusFixed gap;
   DeceptusI16 top, bottom;
   DeceptusU8 index;
   if (body->velocity_x == 0)
   {
      return;
   }
   top = deceptusFloor(body->y) - height;
   bottom = deceptusFloor(body->y);
   for (index = 0; index < engine->mechanisms.box_count; ++index)
   {
      box = &engine->mechanisms.boxes[index];
      /* a body standing on the box, or passing below it, does not push it. */
      if ((top >= box->bounds.y + box->bounds.height) || (bottom <= box->bounds.y))
      {
         continue;
      }
      if (body->velocity_x > 0)
      {
         gap = box->x - (body->x + DECEPTUS_FIXED(half_width));
         if ((gap >= 0) && (gap < body->velocity_x))
         {
            moveBox(engine, box, body->velocity_x / BOX_PUSH_DIVISOR, 0);
         }
      }
      else
      {
         gap = (body->x - DECEPTUS_FIXED(half_width)) - (box->x + DECEPTUS_FIXED(box->bounds.width));
         if ((gap >= 0) && (gap < -body->velocity_x))
         {
            moveBox(engine, box, -(-body->velocity_x / BOX_PUSH_DIVISOR), 0);
         }
      }
   }
}
