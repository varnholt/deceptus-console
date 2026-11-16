# Current status

Updated for standalone content builds, 16 November 2026.

## Available targets

| Target | Current scope |
| --- | --- |
| GBA tutorial | Twenty-six imported rooms; prison, headtorch, graveyard rubies, crypt return, owl altar and sword progression |
| GBA curated preview | Four areas with a separately verified title-to-finish route, enemies, mechanisms and replay |
| SNES | Same 26-room opening in an 8 MiB ExHiROM; retained one-room test is explicit `--scene prototype`; performance remains substantially below target |
| Native host | Shared simulation and small preview, plus engine/converter tests |
| Mega Drive | Same 26-room opening in a flat 4 MiB cartridge budget; normal-input route passes, performance remains experimental; separate small prototype retains its measured 60 Hz result |

## Opening content

The tutorial imports The Cell, Lower Cells, Crossroads, Upper Cells, The Cellar,
Guards' Office, Main Hall, Altar of the Blind Owl, Clockwork Antechamber, The
Loop, The Sewers, Fool's Ledge and Mines Antechamber. Source room ids are retained in generated
reports. It also includes Sealed Cells, Flooded Chamber, Rift Engine, Syphon,
Scriptorium, Reading Room, Skull Room, Cannon Fodder, Graveyard, Mausoleum,
Old Graves, Owl Shrine and Crypt. Original ownership is unchanged. This repository now bundles a versioned copy
of source content in `assets/game/` and builds without the main engine checkout.

Implemented opening rules include source cellmate/bed text, the key and locker,
taking the lever handle, installing it, operating supported linked mechanisms,
room-state persistence, and the original darkness warning. The monk currently
uses an idle pose. Crossroads links toggle immediately; the original camera and
timed cutscene sequence is not reproduced. Cannon Fodder's eleven blocks use
the original inverse relationship to its lever; its camera/delayed cutscene is
also simplified to immediate group toggling.

The normal-input native, GBA, SNES and Mega Drive ROM routes complete the key/handle chain, the Loop,
Sewers, Cannon Fodder gate, Reading Room solar seal, Fool's Ledge headtorch,
Sealed Cells, graveyard owl rubies, sinkhole and crypt return, and the return to
the catacombs altar for sword acquisition and a shoulder-button attack. All 56
checkpoints pass across 23 rooms, with 18,058 input ticks and 17,884 simulation
ticks, zero deaths and 29 health remaining. The route includes live cannon/enemy
damage and 66 native simulation ticks attached to the original Mausoleum rope.
Guards' Office and Fool's Ledge use authored arrival positions rather
than assuming every adjacent room's walkways have identical heights.

## Latest presentation fixes

- Original ground blob patrol, source animation, contact damage and stomp death.
- Basic swimming with source water detection and full directional swim sprites.
- Original shrine tile animation and one-shot falling-entry splash; no ambient looping splash.
- Underwater tint, a small exit hop, original upward wind and delayed wetsuit warning.
- Main Hall/Mines Antechamber passage verified in both directions.
- Mechanism files named after their original counterparts; public C namespace `deceptus_`.
- GitHub Actions build and source-revision-recorded cartridge artifacts.
- Original bitmap font at 13px; title headings use exact 2x copies of those pixels.
- Smooth affine title rotation, changing on 59 of 60 sampled display frames.
- Whole wooden one-way floor artwork, with original foreground occlusion.
- Fixed screen-position room-title sprites while interaction cues and the camera move.
- Pause header lowered two pixels, functional options/controls and confirmation.
- Lower Crossroads/Fool's Ledge connection and western cellar darkness warning.
- Persistent headtorch tunnel, emptied graveyard owl and released sword cage.
- Source 27-tick ruby-insertion delay and automatic crypt return rectangle.
- Source inventory icons with paged browsing, plus equipped sword poses and
  basic source-timed attacks against the supported ground blob.
- Player visible underwater, with water-body artwork behind the baked surface.
- Source-derived animated sword ward, visible equipped helmet and warm light sprite.
- Reusable-key iron door and source wooden barrier with three-hit sword destruction.
- Source cannon firing, cannonballs and arrows that collide with one-way floors.
- Source orb contour and blue frog with owned sine projectile and sword/stomp death.
- Automatic rope grab, directional swing, jump release and source regrab/grace timers.
- Player collision excludes the source movable-box-only boundaries; artwork is unchanged.

## Evidence

The local build passes twelve native C test groups and 91 Python conversion
checks, including an exact comparison of large text against a nearest-neighbour
2x copy of the original glyph rendering. SNES rebuild and Snes9x integration
checks pass for the retained prototype.

GBA checks boot the actual ROM and exercise menu navigation. Controlled-start
fixtures check floor/UI scenes, the darkness warning, underwater visibility,
water-surface rendering, original shrine frame advancement, a blob scene,
a Crossroads/Fool's Ledge return trip and the Main Hall/Mines connection.
A 240-display-frame scrolling water check recorded one simulation tick per
display frame, with zero missed ticks. The player pauses when unfocused and
reuses its display image buffer. This timing result covers the tested scene;
it is not a whole-game performance guarantee. The delayed wetsuit warning
keeps the simulation running while the original upward wind applies. Their reports
and screenshots are generated locally by `tools/verify_tutorial.py`. These are
fixture checks, distinct from the normal-input route. No physical hardware testing or
complete-game performance guarantee is claimed.

Thirty-five controlled-start fixtures cover ruby pickup, consumption, the exact
cage delay, sword acquisition/inventory/shoulder input, the equipped player next
to a lever, the Cannon Fodder inverse gate, headtorch passage, sinkhole and the
automatic crypt return, source enemies, doors, cannon fire, the sword ward,
headtorch poses, rope swing/release and corrected grave-cap collision. Both rope
checks, including sword/headtorch and title expiry, advance 90 simulation ticks
over 90 display frames. Fresh assets and both console builds pass; strict room
reuse produces the same 16 MiB GBA ROM byte for byte. The content cache checks
source bytes, converter code, image-library versions and room recipes.

The full mGBA replay uses ordinary inputs from New Game without changing player
positions, inventory, flags or emulator memory. Reports and screenshots are in
`artifacts/opening-check/`. It records 345 additional display frames for loading
and rendering across the route. This is distinct from a hardware or all-scenes
60fps guarantee. A renderer regression also exercises simultaneous rat/block
palettes; conflicting OBJ indices relocate without changing source colours.

## Remaining work

1. Close remaining opening presentation and behavior differences: timed lever
   camera sequences, monk encounter, graveyard weather/ghosts and sinkhole effects.
   The headtorch currently turns on automatically when acquired.
2. Refine swimming/entry/exit fidelity and add remaining traversal abilities;
   refine collision fidelity and profile demanding rooms.
3. Complete cutscenes, dialogue presentation and original menu/inventory behavior.
4. Port additional enemies and mechanisms to reusable C services, retaining
   source settings and differential checks where possible. Rat, arrow trap and
   normal ground blob are implemented; blob jump/drop variants and other enemy
   families remain unsupported. Cannon, generated-contour orb and blue frog are
   also implemented. Three authored orb placements have no matching source
   physics contour and remain explicitly reported; see `SEALED_ENEMIES.md`.
5. Convert source music/SFX offline and add platform playback. Event ids have
   begun to be exposed, but no sound currently plays in the ROM or project player.
6. Add persistent save storage and versioned progression serialization.
7. Extend complete-level/game verification, hardware testing and release packaging.

The first commercial-quality target remains GBA. The reusable C engine can grow
alongside that work; SNES and Mega Drive remain secondary compatibility and
performance efforts. Neither opening currently meets the intended frame rate.

The Mines entrance is connected. Its farther iron door supports its reusable
source red-key requirement; red-key and wetsuit acquisition are not yet connected.
The current water blocker stays enabled.

GitHub Actions successfully built and tested revision e3c67cd and uploaded its
cartridge artifact; subsequent revisions require their own successful run.

## SNES opening verification

The default SNES builder and launcher now select the opening, independently of
the retained prototype artifact. Eight controlled ROM fixtures cover the Loop,
camera movement, altar ring and release, long animation history, headtorch,
attached rope and the Crypt bridge.
These pass engine, background, UI and OBJ error checks. The actual background
cache also passed 157,008 source-water viewport/frame combinations.

The normal-input SNES replay now passes all 56 checkpoints from New Game
through the graveyard, ruby collection, Crypt return, altar ruby use, sword
acquisition and attack. It matches native positions, inventory, progression,
projectile counts and damage, ending with 29 health and zero deaths. The final
8 MiB ROM hash is `aa667831b20123263a71da238ce51c5bdd4aa8175cbc1e02cc1f207b4360cbc8`.
Warm measurements in the Loop and Crypt are approximately 6.3-6.9 simulation
updates/s; major optimization remains necessary. See [SNES details](SNES.md).

## Mega Drive opening verification

The actual opening cartridge passes all 56 normal-input checkpoints through
the graveyard, Crypt, altar and sword. It matches native progression, damage,
inventory and projectiles, ending with 29 health and zero deaths. The verified
full replay used 70,675 display frames for 17,884 simulation ticks, approximately
15.2 simulation updates/s including loads and rendering. This is not a 60 Hz
opening claim.

The independent converter preserves native pixel size, uses two background and
two stable sprite palette banks, and checks all 88,151 random-access compressed
blocks against raw target data. All 1,065 logical sprite resources retain trim
and occupancy metadata. The 1,152-slot world cache covers the measured maximum
of 1,085 patterns, including animated water and simultaneous-clip bounds.

Compiled stationary fixtures, with 3,000 warm-up and 1,200 sampled display
frames each, measured Cell 49.7, Crypt 19.55, Loop 26.1 and Altar 16.75
simulation updates/s with zero content/background/graphics/OBJ/UI errors.
Sprite preparation dominates Crypt and Loop; background preparation and commit
also dominate Altar. These display-boundary samples are not cycle-accurate
profiling or a substitute for the normal-input route. See
[Mega Drive build and performance details](MEGA_DRIVE.md).

## Retained Mega Drive prototype

`python run_smd.py --scene prototype` opens the separate one-room playtest. The actual cartridge
passes per-update comparisons against desktop C for movement, collision, jumps,
scrolling, pause/resume, restart and checkpoint/portal return. The 600-frame idle
sample advances 600 simulation ticks (59.92 updates/s in Genesis Plus GX). All
1,010 post-boot display frames in the test produce one game-loop update. This
does not establish full-opening, complex-enemy or rope performance. See
[Mega Drive scope and build instructions](MEGA_DRIVE.md).

## Standalone content verification

Builds default to the tracked `assets/game/data/` snapshot. A fresh native build
passes all twelve native groups, including actual Lua comparisons, and all 157
Python checks pass. The GBA opening was rebuilt from bundled content and its
normal-input emulator route passes. The SNES prototype was rebuilt from bundled
content and passes its emulator check. The existing SNES opening route also
passes; it was not rebuilt as part of this content-packaging change.

CI now checks out this repository only. Source-content bytes are preserved by
Git attributes; provenance and SHA-256 hashes are recorded in `assets/game/SOURCE.json`.
