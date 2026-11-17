# Sealed Cells enemies

`src/game/enemies/orb.c` and `frog_blue.c` map to the original scripts of those
names. The source room is Tiled object 1110, imported as room 13. Its supported
enemies are orb object 1396 and blue frog object 1721. Original sprite pixels,
animation frames and source object ids are retained.

## Actual generated orb path

`tools/enemy_paths.py` follows the original `TmxEnemy::addPaths` selection against
the original optimized physics OBJ contours. It preserves world-layer order,
clockwise chain reversal, the last intersecting chain, `inverse_path`, and the
Lua script's 24px expansion and nearest-key alignment.

Object 1396 selects the island contour `(336,1968)`–`(456,2040)`. The Lua-expanded
route has corners `(324,1956)` and `(468,2052)`. Its imported speed of 2 gives
76.8px/s, represented with a bounded fractional fixed-point accumulator. Native
motion follows the actual contour, including corner crossings, and source
animation runs at the placement's 20fps. Its circular contact radius is 11px and
source contact damage is 2. The source supplies no hit callback.

The other three authored orb placements, 1397, 1638 and 1639, do not intersect any
original physics contour with their 24x24px placement rectangles. Their Lua
updates have no path to follow. They remain explicitly unsupported in import
reports; no replacement paths or shifted placements are invented. Diagonal,
subpixel-key and room-exiting generated paths are also rejected explicitly.

The `bat_guard.lua` property on room object 1110 is not an enemy placement.
`Level::parseTMX` sends only objects in the `enemies` group to `TmxEnemy`; rooms
are processed by `Room::deserialize`, which does not execute that property. The
import report records this ignored room property instead of spawning a bat.

## Blue frog and travelling fireball

The native state machine preserves idle/blink animations, the source one-second
windup, fire/hold/close frames, five-second attack interval, four-second orb
lifetime and 19-frame death. Idle blink choices use the existing deterministic
random-state approach. The source frog has 30 health and dies after three sword
hits or a stomp.

The travelling blue fireball belongs to its frog and uses the fixed projectile
pool. Its authored speed is 180px/s, collision area 10x10px, wave strength 20px
and wave speed 0.8. A 256-entry integer sine table with interpolation replaces
runtime floating point. Its source script ignores world collision; the native
orb likewise passes through tiles and mechanisms, damages the player once and
then hides. Death of its owner hides it immediately. Projectiles beyond the
loaded room's visible extent are removed before local coordinates can overflow.

The frog's Box2D rectangle is approximated by the shared fixed-point body and
8px collision grid. Its native feet are 14px below the scripted body position.
Audio is not implemented. No general Lua interpreter or stand-in enemy behavior
is introduced.

## Verification

Native tests compare 600 orb updates against original Lua, including all eight
frames and positions within 0.13px. They compare 900 blue-frog updates, including
blink choices, every attack frame, fireball visibility and lifetime, horizontal
position within 0.13px and sine motion within 0.3px. The fireball test crosses a
solid wall to guard its source intangible behavior. Separate tests cover
one-shot player damage, projectile pool reuse, sword hitboxes, frog death and
owned-fireball cleanup. Converter checks retain exact pixels and transparent
trim offsets; source-contour hashes remain in the import report.
