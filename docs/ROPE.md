# Grab rope

The shared C rope ports the original `mechanisms/rope.cpp`,
`mechanisms/grabrope.cpp`, `player/playerrope.cpp` and
`player/playerropehold.cpp` concepts. The imported Mausoleum rope retains
Tiled object **109**, `mausoleum_rope`, at world anchor `(2652, 1392)`
(room anchor `(396, 48)`). Its authored polyline is 234 pixels long with
22 chain bodies. As in the original setup, the final body is one segment
short of the nominal polyline end.

An airborne player automatically grabs a nearby rope. Left and Right pump
the swing; Up and Down have no effect, matching the current original
`updateHold` implementation. A fresh Jump releases the player, adds half
the normal jump lift and preserves horizontal momentum for 36 ticks.
Reattachment is suppressed for 60 ticks. Entering water, placing the
player or respawning clears the attachment. Holding Jump before the grab
does not immediately release it.

## Shared library boundary

`DECEPTUS_MECH_GRAB_ROPE` definitions are immutable room content. Bounds
identify the anchor and nominal length; `value` is the chain body count.
The importer accepts the source's straight, vertical, plain rope with
zero automatic push strength. Other variants remain explicit unsupported
content, rather than silently becoming this rope.

`src/engine/mechanisms/grabrope.c` owns chain integration, boundary
collision and constraints. `deceptus_engine.grab_ropes` holds the
caller-owned positions, previous positions and cached arc lengths for at
most two ropes of at most 24 bodies each. No heap storage is used.

`src/engine/player/playerrope.c` owns automatic attachment, pumping and
release. Its caller-owned `deceptus_engine.player_rope` exposes
`attached`, `rope_index` and `node_index`, along with bounded link,
radius and cooldown state. Ordinary player collision remains shared
engine collision. Attached animation uses `DECEPTUS_PLAYER_ROPE` (19),
with three frames per direction at the source's 60/60/60 ms timing,
represented by a repeating 4/4/3 tick sequence.

The narrow chain API is declared in `include/deceptus/grabrope.h`:

| Function | Contract |
| --- | --- |
| `deceptusGrabRopesInit(engine)` | Validate definitions and initialize caller state; return zero on capacity or geometry failure. |
| `deceptusGrabRopesTick(engine)` | Advance enabled chains once per simulation tick. |
| `deceptusGrabRopePin(engine, rope, node, x, y)` | Constrain a grabbed chain body to the player's hand in room 12.4 coordinates. |
| `deceptusGrabRopeLength(engine, rope)` | Return visible arc length in pixels, or zero for an unavailable rope. |
| `deceptusGrabRopePointAt(engine, rope, distance_px, x, y)` | Query a room-pixel point along the visible chain, clamping past its end; return zero for an unavailable rope. |

Arc lengths are cached during simulation. Drawing queries walk at most
24 cached intervals and do not recompute square roots. The backend owns
sprites, affine matrices, palettes and OAM; it does not update rope or
attachment physics. GBA draws original seven-pixel-wide artwork with a
native knot cap and native strand slices along queried tangents. Tiling
the strand instead of stretching the original 141-pixel image preserves
the original pixel grid.

Initialization also checks immutable mechanism bounds against a padded
rope reach. If no supported stationary solid can be reached, node queries
skip the dynamic mechanism scan while retaining static grid collision.
Nearby solids and unknown future families retain the complete query.

## Console solver differences

The original uses Box2D chain bodies and distance joints. The C port uses
12.4 Verlet positions, two bounded distance-constraint passes and
integer square roots. Chain nodes collide with boundary cells through
two-pixel point boxes; player attachment uses the original hand offset,
a shortening link and an anchor-radius constraint. This is a bounded
approximation, not a claim of identical Box2D trajectories. Damping is
quantized once per second, and velocity is capped below one eight-pixel
collision cell per tick. Lengths above 512 pixels and more than 24 chain
bodies are rejected.

`tests/playerrope_tests.c` checks source geometry, cached endpoint
queries, airborne attachment, inert vertical controls, swing movement,
release timers, grounded rejection, hanging rendering and capacity
failure. ROM fixtures additionally exercise rendered attachment and
simulation cadence; the opening route remains a separate ordinary-input
integration check.
