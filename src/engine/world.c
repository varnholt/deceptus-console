#include "deceptus/physics.h"

DeceptusI16 deceptusFloor(DeceptusFixed value)
{
   if (value >= 0)
   {
      return (DeceptusI16)(value / DECEPTUS_ONE);
   }
   return (DeceptusI16)(-1 - ((-1 - value) / DECEPTUS_ONE));
}

DeceptusU8 deceptusCell(const DeceptusRoom *room, DeceptusI16 column, DeceptusI16 row)
{
   if (column < 0 || column >= room->columns || row < 0)
   {
      return DECEPTUS_SOLID;
   }
   if (row >= room->rows)
   {
      return DECEPTUS_EMPTY;
   }
   /* an explicit 16x16 -> 32-bit product: the same value everywhere, and one multiply instruction
      instead of a library call on 16-bit cpus. */
   return room->collision[(DeceptusU32)(DeceptusU16)row * (DeceptusU16)room->columns + (DeceptusU16)column];
}

DeceptusI16 deceptusPositionCell(DeceptusFixed position)
{
   DeceptusI16 pixel;
   pixel = deceptusFloor(position);
   if (pixel >= 0)
   {
      return pixel / DECEPTUS_CELL_SIZE;
   }
   return -1 - ((-1 - pixel) / DECEPTUS_CELL_SIZE);
}

static DeceptusU8 collisionCell(const DeceptusRoom *room, const DeceptusMechanismSet *mechanisms, DeceptusI16 column, DeceptusI16 row)
{
   DeceptusRect bounds;
   DeceptusU8 value;
   value = deceptusCell(room, column, row);
   if (!mechanisms || !mechanisms->count || (value & DECEPTUS_SOLID))
   {
      return value;
   }
   /* a cell lies in one column bin and one row bin of the solid grid: without a candidate there,
      deceptusMechanismSolid finds nothing, so the probe rectangle is not even built. */
   if (column >= 0 && row >= 0 && column < DECEPTUS_SOLID_BIN_COUNT * (DECEPTUS_SOLID_BIN_SIZE / DECEPTUS_CELL_SIZE) &&
       row < DECEPTUS_SOLID_BIN_COUNT * (DECEPTUS_SOLID_BIN_SIZE / DECEPTUS_CELL_SIZE) &&
       !((mechanisms->solid_columns[column / (DECEPTUS_SOLID_BIN_SIZE / DECEPTUS_CELL_SIZE)] &
          mechanisms->solid_rows[row / (DECEPTUS_SOLID_BIN_SIZE / DECEPTUS_CELL_SIZE)]) |
         mechanisms->solid_outside))
   {
      return value;
   }
   bounds.x = column * DECEPTUS_CELL_SIZE;
   bounds.y = row * DECEPTUS_CELL_SIZE;
   bounds.width = DECEPTUS_CELL_SIZE;
   bounds.height = DECEPTUS_CELL_SIZE;
   if (deceptusMechanismSolid(mechanisms, &bounds))
   {
      value |= DECEPTUS_SOLID;
   }
   return value;
}

DeceptusU8 deceptusBodyClear(const DeceptusRoom *room, const DeceptusMechanismSet *mechanisms, DeceptusFixed x, DeceptusFixed y,
                             DeceptusU8 half_width, DeceptusU8 height)
{
   DeceptusI16 column, row, left, right, top, bottom;
   left = deceptusPositionCell(x - DECEPTUS_FIXED(half_width));
   right = deceptusPositionCell(x + DECEPTUS_FIXED(half_width - 1));
   top = deceptusPositionCell(y - DECEPTUS_FIXED(height));
   bottom = deceptusPositionCell(y - 1);
   for (row = top; row <= bottom; ++row)
   {
      for (column = left; column <= right; ++column)
      {
         if (collisionCell(room, mechanisms, column, row) & DECEPTUS_SOLID)
         {
            return 0;
         }
      }
   }
   return 1;
}

void deceptusMoveHorizontalDynamic(const DeceptusRoom *room, DeceptusEntity *entity, DeceptusU8 half_width, DeceptusU8 height,
                                   const DeceptusMechanismSet *mechanisms)
{
   DeceptusFixed next_x;
   DeceptusI16 column, top, bottom, row;
   next_x = entity->x + entity->velocity_x;
   top = deceptusPositionCell(entity->y - DECEPTUS_FIXED(height));
   bottom = deceptusPositionCell(entity->y - 1);
   if (entity->velocity_x == 0)
   {
      return;
   }
   column = deceptusPositionCell(next_x + DECEPTUS_FIXED(entity->velocity_x > 0 ? half_width - 1 : -half_width));
   for (row = top; row <= bottom; ++row)
   {
      if (collisionCell(room, mechanisms, column, row) & DECEPTUS_SOLID)
      {
         if (entity->velocity_x > 0)
         {
            next_x = DECEPTUS_FIXED(column * DECEPTUS_CELL_SIZE - half_width);
         }
         else
         {
            next_x = DECEPTUS_FIXED((column + 1) * DECEPTUS_CELL_SIZE + half_width);
         }
         entity->velocity_x = 0;
         break;
      }
   }
   entity->x = next_x;
}

void deceptusMoveVerticalDynamic(const DeceptusRoom *room, DeceptusEntity *entity, DeceptusU8 half_width, DeceptusU8 height,
                                 const DeceptusMechanismSet *mechanisms)
{
   DeceptusFixed next_y;
   DeceptusI16 row, left, right, column, old_bottom;
   DeceptusU8 cell, blocked;
   next_y = entity->y + entity->velocity_y;
   left = deceptusPositionCell(entity->x - DECEPTUS_FIXED(half_width));
   right = deceptusPositionCell(entity->x + DECEPTUS_FIXED(half_width - 1));
   old_bottom = deceptusFloor(entity->y);
   row = deceptusPositionCell(entity->velocity_y >= 0 ? next_y - 1 : next_y - DECEPTUS_FIXED(height));
   entity->grounded = 0;
   for (column = left; column <= right; ++column)
   {
      cell = collisionCell(room, mechanisms, column, row);
      blocked = (cell & DECEPTUS_SOLID) != 0;
      if ((cell & DECEPTUS_ONE_WAY) && !entity->drop_ticks && entity->velocity_y >= 0 && old_bottom <= row * DECEPTUS_CELL_SIZE)
      {
         blocked = 1;
      }
      if (blocked)
      {
         if (entity->velocity_y >= 0)
         {
            next_y = DECEPTUS_FIXED(row * DECEPTUS_CELL_SIZE);
            entity->grounded = 1;
         }
         else
         {
            next_y = DECEPTUS_FIXED((row + 1) * DECEPTUS_CELL_SIZE + height);
         }
         entity->velocity_y = 0;
         break;
      }
   }
   entity->y = next_y;
}

void deceptusMoveHorizontal(const DeceptusRoom *room, DeceptusEntity *entity, DeceptusU8 half_width, DeceptusU8 height)
{
   deceptusMoveHorizontalDynamic(room, entity, half_width, height, 0);
}

void deceptusMoveVertical(const DeceptusRoom *room, DeceptusEntity *entity, DeceptusU8 half_width, DeceptusU8 height)
{
   deceptusMoveVerticalDynamic(room, entity, half_width, height, 0);
}

DeceptusI16 deceptusWallSide(const DeceptusRoom *room, const DeceptusEntity *entity, const DeceptusMechanismSet *mechanisms)
{
   DeceptusI16 side, column, first, last, row;
   first = deceptusPositionCell(entity->y - DECEPTUS_FIXED(24));
   last = deceptusPositionCell(entity->y - DECEPTUS_FIXED(8));
   for (side = -1; side <= 1; side += 2)
   {
      column = deceptusPositionCell(entity->x + DECEPTUS_FIXED(side * 9));
      for (row = first; row <= last; ++row)
      {
         if (collisionCell(room, mechanisms, column, row) & DECEPTUS_SOLID)
         {
            return side;
         }
      }
   }
   return 0;
}
