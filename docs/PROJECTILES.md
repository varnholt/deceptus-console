# Arrows and pirate cannons

`src/game/enemies/cannon_2.c` follows
`deceptus_engine/data/scripts/enemies/cannon_2.lua`. Its source object ids,
horizontal alignment, fire interval and time offset remain in the imported enemy
definitions. The source's 16fps firing/idle sequence uses the original directional
144x72px frames. The gun fires during animation index 17; its 1000ms use interval
prevents the repeated script calls during that frame from producing extra balls.

The cannon uses the existing caller-owned projectile pool. Cannonballs deal the
source damage of 60 and have no gravity. Their fixed velocity is 102/16px per
tick, rounded from the source unit impulse and a density-one circle of radius
0.2m. Collision uses a 20x20px box around the projectile instead of Box2D's
9.6px-radius circle. Fire intervals above 15 seconds and non-horizontal cannon
variants are rejected explicitly rather than overflowing the bounded animation
counter. Simulation timing is rounded to 60Hz.
The original projectile impact animation, impact audio and cannon firing samples
are not implemented; a blocked projectile is removed from the fixed pool.

The source's cannon body is a beveled polygon: relative to its scripted position,
its bounds are `(0,12,48,36)` pixels, with 2.4px corner bevels. The shared 8px
collision grid conservatively approximates those bevels. Cannon bodies block the
player. The source gun's collision mask excludes enemy bodies, so projectile
collision deliberately excludes cannon bodies, including the firing cannon.
This matters because its right-facing muzzle starts inside the body bounds.

## Projectile collisions

`src/game/enemies.c` advances projectiles in eight bounded substeps, each less
than one pixel under the shared velocity limit. Arrows use their existing point
collision representation. Both arrows and cannonballs collide with solid tiles,
wooden one-way floor tiles and active dynamic mechanisms, including collapsing
platforms. Collapsed/disabled platforms do not block them.

This projectile treatment of one-way tiles follows the original source:
`src/game/level/level.cpp` constructs closed chain loops for that layer, and
`src/game/physics/gamecontactlistener.cpp` only applies the upward/drop-through
exception to player fixtures. Projectiles therefore collide with the contour
from above, below and the sides. Player one-way movement remains independent.

## Graphics and checks

The converter keeps every opaque cannon, cannonball, door and wooden-plank pixel
at its original source coordinates and RGB555 colour. Transparent border trimming
only reduces hardware storage; the original logical rectangle and trim offsets
remain in the resource manifest. The descriptor's final 16 bits store the x trim
in the low byte and y trim in the high byte. Rendering adds those offsets to the
logical source-frame position.

Native tests execute the original cannon Lua for 720 updates and compare its
complete sprite sequence and gun firing schedule. They also cover both firing
directions, cannon-body collision filtering, player damage, cardinal and diagonal
arrow impacts, gravity and active/inactive dynamic floors. Converter tests check
the source gun properties and exact source pixels, transparency, trim offsets,
palette DMA alignment and packed resource descriptors. Actual GBA and SNES ROM
checks remain required after shared-code changes.
