# Opening sword support

After the owl grants the sword, L or R starts a swing. The existing A/B jump
controls remain available. The equipped player uses the original directional
sword sprites for idle, running, jumping, crouching and swimming. Attack artwork
uses the first standing/crouching swing and the airborne swing, with authored
frame durations and asymmetric origins. Source dimensions and pixel coordinates
remain unchanged; the GBA converter reduces the sword/trail colours into the
player's reserved 32-colour OBJ range without dithering. Equipped sword frames
occupy OBJ colour indices 2–33, replacing the unarmed player. The range is aligned
for 32-bit DMA and index zero stays transparent; mechanism palettes begin at
index 52, allowing the equipped player
to stand beside the original levers without a palette conflict.

## Source correspondence

| Original engine | Portable implementation |
| --- | --- |
| `src/game/weapons/playersword.cpp` | `src/game/weapons/playersword.c` |
| `data/scripts/enemies/blob_2.lua`: `hit`, `_energy`, `startDying` | sword damage accumulator and existing blob death state |
| `data/scripts/enemies/frog_blue.lua`: `hit`, `_energy`, `startDying` | source 16px offset hitbox, three-hit death and owned orb removal |
| `src/game/mechanisms/destructibleblockingrect.cpp` | `src/engine/mechanisms/destructibleblockingrect.c`: source durability, collision removal and break frames |
| `data/sprites/animations.json`: `player_*_sword_*` | `tools/gba_assets.py`: `sword_frames`, logical source clip metadata |

The source sword applies damage 10 after a 200ms windup. Its hit permission is
cleared on the first update in the 120ms hit window, so the C version queries
once per swing. Standing hit bounds are 60x40px; crouching bounds are 40x20px.
Fractional source offsets are rounded to integer pixels. Facing is captured when
the swing begins. A blob starts with energy 30 and enters its original death
animation after three connecting swings. The blue frog also has energy 30;
three hits against its source offset hitbox start its 19-frame death animation
and hide its travelling fireball. The floating orb has no source `hit` callback
and remains immune. Enemy damage state resets on room load,
alongside the current enemy respawn behavior.

The Reading Room's source wooden barrier also takes three connecting swings.
The shared mechanism removes its collision as soon as durability reaches zero,
then plays the source break frames. Its destroyed state persists across room
changes.

This is bounded opening combat support. Combo selection,
impact particles, camera shake, attack movement restrictions and
airborne split-body animation are not implemented. Underwater attack input is
ignored. Rats have no source `hit` callback and are not made destructible. Other
unsupported enemy scripts are not given generic sword behavior. The SNES frontend
continues to run its separate prototype; it does not present the equipped sword.

## Checks

`playersword_tests` verifies the source windup, one impact per swing, three-hit
blob/frog death, frog hitbox placement, owned fireball removal, orb immunity,
captured facing, crouching bounds, airborne duration and room-state
reset. `test_playersword_assets.py` compares exported rectangles and cumulative
60Hz durations with the authoritative animation definitions and checks both
standing attack origins. ROM checks must additionally verify acquisition,
shoulder input, rendering and the absence of graphics resource errors.
The `sword` tutorial fixture verifies acquisition, inventory, the L-button windup
and a single attack from a held button in mGBA. The `sword-lever` fixture checks
the equipped player beside an operating source lever, guarding against OBJ
palette conflicts. Both preserve the production ROM after testing.
