/* original counterpart: src/game/mechanisms/spikeball.cpp with its default properties.
 *
 * the source hangs a ball from ten Box2D chain bodies and pushes it a little while it swings left.
 * here the ball is a point on a rigid chain, moved by position Verlet steps and pulled back to the
 * chain's length after each one; the push keeps the source's direction. the length and the push were
 * fitted to a Box2D run of the source setup, which settles into a swing between about 78 degrees left
 * and 73 degrees right that turns every 122 ticks; this one swings between 77 and 69 every 124. */
#include "deceptus/spikeball.h"
#include "arith.h"

/* positions are kept in 1/256 px. */
#define POSITION_SHIFT 8
/* the chain length the fit gave; the Box2D ball keeps 125 to 153 px from the anchor. */
#define CHAIN_LENGTH ((DeceptusI32)140 << POSITION_SHIFT)
/* 8 m/s^2 at 48 px per metre: 384 px/s^2, 0.107 px per tick per tick. */
#define GRAVITY 27
/* the source's impulse dt * push_factor 0.625 on the 0.64 kg ball, scaled by the fit. */
#define PUSH 4
/* the anchor is the middle of the 24 px object. */
#define ANCHOR_OFFSET 12
/* the ball's 0.45 m radius. */
#define BALL_RADIUS 21
/* links sit at the eighths of the chain; the eighth one lies under the ball. */
#define LINK_STEPS (SPIKE_BALL_LINKS + 1)
#define LINK_STEPS_SHIFT 3
typedef char link_steps_are_a_power_of_two[(LINK_STEPS == (1 << LINK_STEPS_SHIFT)) ? 1 : -1];

/* tan of the angles from straight down halfway between two ball images, in 1/256: image k shows the
 * ball turned 90 - 11.25 k degrees from straight down towards the right. */
static const DeceptusI16 rotation_bounds[SPIKE_BALL_ROTATIONS - 1] =
   {2599, 844, 479, 312, 210, 137, 78, 25, -25, -78, -137, -210, -312, -479, -844, -2599};

static const SpikeBall* findBall(const DeceptusMechanismSet* set, DeceptusU16 index)
{
   DeceptusU8 ball;
   for (ball = 0; ball < set->spike_ball_count; ++ball)
   {
      if (set->spike_balls[ball].mechanism_index == index)
      {
         return &set->spike_balls[ball];
      }
   }
   return 0;
}

static DeceptusI32 scaleToChain(DeceptusI32 value, DeceptusI32 length)
{
   return (value < 0) ? -((-value) * CHAIN_LENGTH / length) : (value * CHAIN_LENGTH / length);
}

static void swing(SpikeBall* ball)
{
   DeceptusI32 velocity_x, velocity_y, length;
   velocity_x = ball->x - ball->previous_x;
   velocity_y = (ball->y - ball->previous_y) + GRAVITY;
   if (velocity_x < 0)
   {
      /* the source pushes the ball left and down while it moves left. */
      velocity_x -= PUSH;
      velocity_y += PUSH;
   }
   ball->previous_x = ball->x;
   ball->previous_y = ball->y;
   ball->x += velocity_x;
   ball->y += velocity_y;
   length = (DeceptusI32)arithRootU32((DeceptusU32)(ball->x * ball->x) + (DeceptusU32)(ball->y * ball->y));
   if (length != 0)
   {
      ball->x = scaleToChain(ball->x, length);
      ball->y = scaleToChain(ball->y, length);
   }
}

static DeceptusI16 anchorX(const DeceptusMechanismSet* set, DeceptusU16 index)
{
   return (DeceptusI16)(set->definitions[index].bounds.x + ANCHOR_OFFSET);
}

static DeceptusI16 anchorY(const DeceptusMechanismSet* set, DeceptusU16 index)
{
   return (DeceptusI16)(set->definitions[index].bounds.y + ANCHOR_OFFSET);
}

/* nonzero when the circle at centre_x, centre_y with BALL_RADIUS reaches into bounds. */
static DeceptusU8 touches(DeceptusI16 centre_x, DeceptusI16 centre_y, const DeceptusRect* bounds)
{
   DeceptusI32 near_x, near_y;
   near_x = centre_x;
   near_y = centre_y;
   if (near_x < bounds->x)
   {
      near_x = bounds->x;
   }
   else if (near_x > bounds->x + bounds->width)
   {
      near_x = bounds->x + bounds->width;
   }
   if (near_y < bounds->y)
   {
      near_y = bounds->y;
   }
   else if (near_y > bounds->y + bounds->height)
   {
      near_y = bounds->y + bounds->height;
   }
   near_x -= centre_x;
   near_y -= centre_y;
   return (near_x * near_x + near_y * near_y) < (DeceptusI32)BALL_RADIUS * BALL_RADIUS;
}

static DeceptusU8 ballImage(const SpikeBall* ball)
{
   DeceptusI32 across;
   DeceptusU8 image;
   /* at or above the anchor's height: the outermost image on that side. kept to plain branches and
    * a bounded loop: a while loop guarded by image < count && comparison froze the SNES build. */
   if (ball->y <= 0)
   {
      if (ball->x > 0)
      {
         return 0;
      }
      return SPIKE_BALL_ROTATIONS - 1;
   }
   across = ball->x << POSITION_SHIFT;
   for (image = 0; image < SPIKE_BALL_ROTATIONS - 1; ++image)
   {
      if (across >= ball->y * rotation_bounds[image])
      {
         break;
      }
   }
   return image;
}

void spikeBallsInit(DeceptusMechanismSet* set)
{
   SpikeBall* ball;
   DeceptusU16 index;
   set->spike_ball_count = 0;
   for (index = 0; index < set->count; ++index)
   {
      if ((set->definitions[index].kind != MECHANISM_SPIKE_BALL) || (set->spike_ball_count == SPIKE_BALL_CAPACITY))
      {
         continue;
      }
      ball = &set->spike_balls[set->spike_ball_count++];
      ball->mechanism_index = (DeceptusU8)index;
      ball->x = CHAIN_LENGTH;
      ball->y = 0;
      ball->previous_x = ball->x;
      ball->previous_y = ball->y;
   }
}

DeceptusU8 spikeBallsTick(DeceptusMechanismSet* set, const DeceptusRect* bounds)
{
   SpikeBall* ball;
   DeceptusU8 index, touched;
   touched = 0;
   for (index = 0; index < set->spike_ball_count; ++index)
   {
      ball = &set->spike_balls[index];
      swing(ball);
      if (touches(
             (DeceptusI16)(anchorX(set, ball->mechanism_index) + (ball->x >> POSITION_SHIFT)),
             (DeceptusI16)(anchorY(set, ball->mechanism_index) + (ball->y >> POSITION_SHIFT)),
             bounds
          ))
      {
         touched = 1;
      }
   }
   return touched;
}

/* position * part / LINK_STEPS, truncated toward zero as c division, in pixels. the parts are drawn
 * every presentation: a 32-bit multiply and divide per link cost the 65816 a fifth of the spike ball
 * room's time, where at most seven additions and a shift do. */
static DeceptusI16 linkOffset(DeceptusI32 position, DeceptusU8 part)
{
   DeceptusI32 product;
   DeceptusU8 step;
   product = 0;
   for (step = 0; step < part; ++step)
   {
      product += position;
   }
   product = (product < 0) ? -((-product) >> LINK_STEPS_SHIFT) : (product >> LINK_STEPS_SHIFT);
   return (DeceptusI16)(product >> POSITION_SHIFT);
}

void spikeBallPart(const DeceptusMechanismSet* set, DeceptusU16 index, DeceptusU8 part, DeceptusU8* image, DeceptusI16* x, DeceptusI16* y)
{
   const SpikeBall* ball;
   ball = findBall(set, index);
   if (ball == 0)
   {
      *image = SPIKE_BALL_ROTATIONS / 2;
      *x = anchorX(set, index);
      *y = anchorY(set, index);
      return;
   }
   if (part == 0)
   {
      *image = ballImage(ball);
      *x = (DeceptusI16)(anchorX(set, index) + (ball->x >> POSITION_SHIFT) - SPIKE_BALL_FRAME_SIZE / 2);
      *y = (DeceptusI16)(anchorY(set, index) + (ball->y >> POSITION_SHIFT) - SPIKE_BALL_FRAME_SIZE / 2);
      return;
   }
   /* the source alternates its two link sprites along the chain, starting with the first. a ?: stored
    * through a pointer is miscompiled by 816-tcc (docs/SNES.md), hence the branches. */
   if (part & 1)
   {
      *image = SPIKE_BALL_IMAGE_LINK_A;
   }
   else
   {
      *image = SPIKE_BALL_IMAGE_LINK_B;
   }
   *x = (DeceptusI16)(anchorX(set, index) + linkOffset(ball->x, part) - SPIKE_BALL_LINK_SIZE / 2);
   *y = (DeceptusI16)(anchorY(set, index) + linkOffset(ball->y, part) - SPIKE_BALL_LINK_SIZE / 2);
}
