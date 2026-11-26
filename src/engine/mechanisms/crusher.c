/* original counterpart: src/game/mechanisms/crusher.cpp, interval mode, pointing down with a chain.
 *
 * the source moves a Box2D body: a solid block with the spiked blade beneath it. here only the blade
 * is kept, as a rectangle that kills on contact. the block is left out because bodies meet solids at
 * 8px collision cells, and a block moving pixel by pixel cannot carry a body standing on it. */
#include "deceptus/crusher.h"

/* the source's tile size; the crusher's pieces are laid out in tiles. */
#define CRUSHER_TILE 24
#define CRUSHER_WIDTH (5 * CRUSHER_TILE)
/* the blade's top below the crusher's top left corner, and the blade body's top below that. */
#define CRUSHER_BLADE_TOP (2 * CRUSHER_TILE)
#define CRUSHER_BLADE_BODY_TOP (4 * CRUSHER_TILE)
/* BLADE_TOLERANCE_M (0.06 m at 48 px per metre) keeps the deadly edges inside the drawn blade. */
#define CRUSHER_BLADE_INSET 3
#define CRUSHER_MOUNT_X (2 * CRUSHER_TILE)

#define CRUSHER_RETRACTED 0
#define CRUSHER_EXTRACTING 1
#define CRUSHER_EXTRACTED 2
#define CRUSHER_RETRACTING 3
/* extraction_time_max_s is 1 s advanced at rate 1.0, retraction_time_max_s 1 s advanced at rate 0.4. */
#define CRUSHER_EXTRACT_TICKS 60
#define CRUSHER_RETRACT_TICKS 150

/* 1.0 of the easing curves below. */
#define EASING_SHIFT 10
#define EASING_ONE (1L << EASING_SHIFT)

/* easings.h easeOutBounce. with y = 11 x its four parabolas are (y - centre)^2 / 16 + base, which
 * keeps them exact in integers: the centres are 0, 6, 9 and 10.5 elevenths, the bases 0, 3/4, 15/16
 * and 63/64, and each piece ends at 4, 8 and 10 elevenths. */
static DeceptusI32 easeOutBounce(DeceptusI32 x)
{
   DeceptusI32 y, centre, base;
   y = 11 * x;
   if (y < 4 * EASING_ONE)
   {
      centre = 0;
      base = 0;
   }
   else if (y < 8 * EASING_ONE)
   {
      centre = 6 * EASING_ONE;
      base = EASING_ONE * 3 / 4;
   }
   else if (y < 10 * EASING_ONE)
   {
      centre = 9 * EASING_ONE;
      base = EASING_ONE * 15 / 16;
   }
   else
   {
      centre = EASING_ONE * 21 / 2;
      base = EASING_ONE * 63 / 64;
   }
   return ((y - centre) * (y - centre)) / (16 * EASING_ONE) + base;
}

/* 1 - easings.h easeInSine, which is cos(x pi / 2): Bhaskara's approximation, 4 (1 - x^2) / (4 + x^2),
 * stays within 0.2% of it. the divisor is scaled down first so the products fit 32 bits. */
static DeceptusI32 easeOutCosine(DeceptusI32 x)
{
   return (4 * (EASING_ONE * EASING_ONE - x * x)) / ((4 * EASING_ONE * EASING_ONE + x * x) >> EASING_SHIFT);
}

void crushersInit(DeceptusMechanismSet *set)
{
   DeceptusU16 index;
   for (index = 0; index < set->count; ++index)
   {
      if ((set->definitions[index].kind == MECHANISM_CRUSHER) && (set->timings != 0))
      {
         /* the source's time offset shortens the first wait only. */
         set->states[index].phase = CRUSHER_RETRACTED;
         set->states[index].timer = set->timings[index].phase_offset_ticks;
      }
   }
}

static DeceptusI16 crusherOffset(const DeceptusMechanismSet *set, DeceptusU16 index)
{
   const DeceptusMechanismState *state;
   DeceptusI32 travel;
   state = &set->states[index];
   travel = set->definitions[index].value;
   if (state->phase == CRUSHER_EXTRACTING)
   {
      return (DeceptusI16)((travel * easeOutBounce(((DeceptusI32)state->timer << EASING_SHIFT) / CRUSHER_EXTRACT_TICKS)) >> EASING_SHIFT);
   }
   if (state->phase == CRUSHER_EXTRACTED)
   {
      return (DeceptusI16)travel;
   }
   if (state->phase == CRUSHER_RETRACTING)
   {
      return (DeceptusI16)((travel * easeOutCosine(((DeceptusI32)state->timer << EASING_SHIFT) / CRUSHER_RETRACT_TICKS)) >>
                           EASING_SHIFT);
   }
   return 0;
}

/* the source alternates waits with drops and lifts: wait, drop, wait, lift. */
static void crusherTick(DeceptusMechanismSet *set, DeceptusU16 index)
{
   DeceptusMechanismState *state;
   DeceptusU16 duration;
   state = &set->states[index];
   ++state->timer;
   duration = (state->phase == CRUSHER_EXTRACTING)   ? CRUSHER_EXTRACT_TICKS
              : (state->phase == CRUSHER_RETRACTING) ? CRUSHER_RETRACT_TICKS
              : (state->phase == CRUSHER_EXTRACTED)  ? set->timings[index].on_ticks
                                                     : set->timings[index].off_ticks;
   if (state->timer >= duration)
   {
      state->timer = 0;
      state->phase = (DeceptusU8)((state->phase + 1) & 3);
   }
}

static DeceptusU8 bladeTouches(const DeceptusMechanismSet *set, DeceptusU16 index, const DeceptusRect *body)
{
   const DeceptusRect *bounds;
   DeceptusI16 left, top;
   bounds = &set->definitions[index].bounds;
   left = bounds->x + CRUSHER_BLADE_INSET;
   top = bounds->y + CRUSHER_BLADE_BODY_TOP + crusherOffset(set, index);
   return (body->x < left + CRUSHER_WIDTH - 2 * CRUSHER_BLADE_INSET) && (body->x + body->width > left) &&
          (body->y < top + CRUSHER_TILE) && (body->y + body->height > top);
}

DeceptusU8 crushersTick(DeceptusMechanismSet *set, const DeceptusRect *bounds)
{
   DeceptusU16 index;
   DeceptusU8 touched;
   touched = 0;
   if (set->timings == 0)
   {
      return 0;
   }
   for (index = 0; index < set->count; ++index)
   {
      if (set->definitions[index].kind == MECHANISM_CRUSHER)
      {
         crusherTick(set, index);
         touched = (DeceptusU8)(touched | bladeTouches(set, index, bounds));
      }
   }
   return touched;
}

/* the chain hangs from the blade and is lowered with it; links are cut off where they disappear into
 * the mount, so one link per mount tile of gap between the mount and the blade. */
DeceptusU8 crusherPartCount(const DeceptusMechanismSet *set, DeceptusU16 index)
{
   DeceptusI16 gap;
   gap = CRUSHER_BLADE_TOP - CRUSHER_TILE + crusherOffset(set, index);
   return (DeceptusU8)(2 + (gap + CRUSHER_LINK_HEIGHT - 1) / CRUSHER_LINK_HEIGHT);
}

void crusherPart(const DeceptusMechanismSet *set, DeceptusU16 index, DeceptusU8 part, DeceptusU8 *image, DeceptusI16 *x, DeceptusI16 *y)
{
   const DeceptusRect *bounds;
   DeceptusI16 blade_top, mount_bottom, link_top, hidden;
   bounds = &set->definitions[index].bounds;
   blade_top = bounds->y + CRUSHER_BLADE_TOP + crusherOffset(set, index);
   if (part == 0)
   {
      *image = CRUSHER_IMAGE_BLADE;
      *x = bounds->x;
      *y = blade_top;
      return;
   }
   *x = bounds->x + CRUSHER_MOUNT_X;
   if (part == 1)
   {
      *image = CRUSHER_IMAGE_MOUNT;
      *y = bounds->y;
      return;
   }
   mount_bottom = bounds->y + CRUSHER_TILE;
   link_top = blade_top - (DeceptusI16)(part - 1) * CRUSHER_LINK_HEIGHT;
   hidden = (link_top < mount_bottom) ? (DeceptusI16)(mount_bottom - link_top) : 0;
   *image = (DeceptusU8)(CRUSHER_IMAGE_LINK + hidden);
   *y = link_top + hidden;
}
