# Engine architecture

The engine is a growing portable C90 runtime with a Deceptus game layer and
separate hardware backends. Its interfaces are not a frozen general-purpose
engine ABI. GBA is the first complete-game target; the SNES opening exercises
shared code under a very different compiler and resource budget.

## Code boundaries

| Area | Responsibility |
| --- | --- |
| `include/deceptus/engine.h` and component headers | Reusable simulation state, collision, entities, mechanisms, animation and event contracts |
| `include/deceptus/game.h`, `campaign.h`, `adventure.h`, `enemies.h` | Game controller, campaign, story/inventory and enemy contracts |
| `src/engine/world.c` | Grid collision, dynamic blockers, one-way floors and body clearance |
| `src/engine/entities.c`, `events.c` | Bounded entity allocation and transient event queues |
| `src/game/enemies.c`, `enemies/` | Deceptus enemy behaviors, projectile rules and source-script dispatch |
| `src/engine/player/playerwater.c` | Source atmosphere water detection, swim motion and source-frame timing |
| `src/engine/player/playerrope.c` | Airborne rope grab, attachment, swing control and release/grace state |
| `src/engine/mechanisms/`, `logic.c` | Original-named mechanism modules, bounded state/queues and compiled actions |
| `src/game/adventure.c` | Inventory transactions, interaction conditions, dialogue/menu modes, portals and room state |
| `src/game/deceptus.c`, `campaign.c` | Player/game rules and the curated preview campaign |
| `src/game/weapons/playersword.c` | Source sword timing, hit bounds and supported blob damage |
| `src/game/player/playeranimation.c` | Shared source sword pose and frame selection for every backend |
| `src/game/items/itemheadtorch.c` | Game-owned headtorch equipment rules |
| `src/platform/gba/` | Registers, input, DMA, tile residency, OAM, menus, UI and cartridge startup |
| `src/platform/snes/` | SNES/PVSnesLib world cache, OBJ staging, input and opening frontend |
| `src/platform/smd/` | SGDK POC video/input, tile layout and private C header adapters |
| `src/platform/amiga/` | OCS bitplanes, blitter, copper, input and system takeover/restoration |
| `src/platform/host/` | Native preview bridge |
| `tools/` | Offline authoritative-content import, conversion, compilation and verification |

Console registers and SDK headers belong only in platform code. There is no
runtime SFML, Lua interpreter, heap allocation or floating-point simulation.
The GBA uses a small freestanding runtime rather than a host C library.
Public C functions use the `deceptus` prefix and camelCase; constants use
`DECEPTUS_`, types use CamelCase, and headers live in
`include/deceptus/`. Names of external emulator protocols retain their upstream API.
Newer interfaces such as `include/arith.h` use neutral names without the project
prefix.

## Platform specializations

Shared code is the reference behaviour on every platform and never names a CPU
or console. A platform specializes it without `#if` blocks in shared files, in
two ways:

- **Arithmetic primitives** (`include/arith.h`): `arithSquareU16`,
  `arithMulS16`, `arithMulU16`, `arithDivS32U16` and `arithRootU32`, each with an exact
  contract and a portable reference in `src/engine/arith.c`. A platform that
  defines `PLATFORM_ARITH` provides `platform_arith.h` in its own source folder,
  which its build puts on the include path: the Amiga and the Mega Drive share
  the 68000 one in `src/platform/m68k`, which maps them to inline `mulu`, `muls`
  and `divs` where those are exact, the SNES maps them to 65816 routines in
  `engine_math.asm`. Other platforms use the reference.
- **Platform content limits** in the platform's tooling, such as re-timed
  animation clips on the Amiga or the SNES grab rope cap
  (`SNES_ROPE_SEGMENTS`). These change behaviour on purpose, so the platform's
  reference changes with them: its native replay is built from the same
  converted content, and the route may replace individual steps for that
  platform (`platform_steps` in `opening-route.json`). A limit stays with its
  platform; other targets export their own content.

Each specialization is checked against the reference: the primitives by tests
that compare them with the `*Reference` functions over broad input ranges (on
the SNES a test build runs this comparison on the console), and every platform
by its tick-exact route replay.

Game-specific story callbacks, inventory rules, equipment choices and enemy
behaviors belong in `src/game/` or the imported game content. The engine library
contains reusable simulation services and links independently of the game
library. Backends translate game presentation into hardware operations; they do
not decide when rubies are acquired, when the ground collapses or when a sword
becomes available.

`src/game/enemies.c` includes its private enemy modules as one translation unit,
matching the mechanism build pattern. Compile that entry rather than its private
fragments separately. This preserves existing compiler optimization and bounded
runtime behavior while making ownership explicit.

## Streamed room content

`DeceptusAdventureInitWithRoomProvider` accepts a caller-owned room loader. The
adventure chooses the destination using its content-defined portals; a platform
loader only supplies that room's resources. Cartridge builds retain the existing
static room tables and `deceptusAdventureInit` entry point. A streamed campaign
may omit its static room-pointer table. Returned room storage, including its
collision bytes and referenced arrays, must remain valid until the next load.
A failed load halts the adventure with a content error before arrival placement.
Restarting the adventure retains the provider and its context.

`tools/room_content.py` exports `DRM1` files from the same lossless collision grid
used by `world_export.py`. `src/game/roomcontent.c` decodes explicit big-endian
fields into caller-owned arrays; it never casts file bytes to C structures.
It validates the exact file length, dimensions, capacities and target/path
ranges. Ordinary mechanism/enemy initialization still checks semantic content.
The collision grid points into the resident file bytes instead of a second copy.
Enemy definitions keep this codec in the game library, independent of the engine.

The opening needs at most 16,522 bytes for one simulation file, 16 mechanisms,
12 target links, five enemies and six path points. These are measured content
requirements, not new global engine limits. Local checks compare all 26 decoded
rooms to the static definitions and replay the entire opening with one reusable
room slot; newly created verification artifacts remain outside version control.

## Shared game presentation

`src/game/appearance.c` visits original-source sprite appearances for the player,
equipment, enemies, mechanisms and grab ropes. It owns pose/attachment choices
and emits logical family/frame coordinates. Backends map those families to their
converted graphics, cull offscreen requests and enforce their existing bounded
hardware queues. The visitor adds no intermediate frame buffer or heap storage.
Sprites emitted earlier are drawn in front of later ones. Mechanisms the source
draws above the player's depth (z over 20, `MECHANISM_IN_FRONT`: levers, wooden
planks, crushers, spike balls, spike layers) are emitted before the player and the
rest after her; the GBA renderer keeps the same order. The Amiga player is one
hardware sprite in front of the whole playfield, so its renderer clears her pixels
under the static foreground tile masks and under the masks of the bobs emitted
before her. Grab ropes are emitted as pieces between points along the rope, the
knot piece from the anchor and then 8px links, each picked from 17 pre-turned
images by its direction (`src/game/ropelink.c`, no trigonometry).
While the inventory is open the visitor emits only the selected item's picture
(`itemicon.h`, from the source `inventory_items.png`). The Mega Drive gives it the
sprite priority bit, the SNES clears its menu layer's priority under it and turns
the room's layers off while the menu is open, and the Amiga draws it after the
menu. The GBA keeps its pre-rendered inventory screens.
SNES and Mega Drive consume this path; it is also available to the Amiga opening.
GBA still has its existing renderer, with shared sword pose and equipment rules.

## Mechanism source correspondence

Keep the original engine's vocabulary and recognizable file names when porting.
The reusable C versions live under `src/engine/mechanisms/`; their original C++
counterparts live under `deceptus_engine/src/game/mechanisms/`.

| Original file | C module | Responsibility |
| --- | --- | --- |
| `gamemechanism.cpp` | `gamemechanism.c` | Binding/validation, source-id lookup, event queue, enable requests, update coordination |
| `lever.cpp` | `lever.c` | Handle restriction, animation/cooldown, linked-target transaction |
| `onoffblock.cpp` | `onoffblock.c` | Initial visibility/collision state, inversion and collision query |
| `spikeblock.cpp` | `spikeblock.c` | Initial frame and damaging animation frames |
| `collapsingplatform.cpp` | `collapsingplatform.c` | Foot contact, collapse, respawn and collision |
| `checkpoint.cpp` | `checkpoint.c` | One-shot reached notification |
| `checkpoint.cpp` (clock) | `checkpointclock.c` | Clock image: waiting, activation once reached, ticking loop; hidden and raised clocks |
| `sensorrect.cpp` | `sensorrect.c` | Enter/leave notifications |
| `damagerect.cpp` | `damagerect.c` | Contact damage notification |
| `blockingrect.cpp` | `blockingrect.c` | Common dynamic-solid query for supported blocker families |
| `door.cpp` | `door.c` | Reusable-key opening, source frame timing, cooldown and collision |
| `destructibleblockingrect.cpp` | `destructibleblockingrect.c` | Sword damage, destruction animation and persistent removed blockers |
| `rope.cpp`, `grabrope.cpp` | `grabrope.c` | Bounded chain constraints, static/dynamic collision, cached arc lengths and render-point queries |

`switchanimation.c` holds the shared on/off/spike animation queue implementation.
Its frame rules come from the original families, not a new visual design.
The mapping describes responsibility, not a claim that every feature of each
original C++ class has been ported.

`src/engine/mechanisms.c` is a small unity-build entry that includes the private
modules. Build this entry, not the individual implementation files as separate
objects. This intentionally preserves static cross-family inlining, the existing
GBA IWRAM placement and the SNES compiler's optimization scope. CMake lists the
private modules as header-only sources for IDE navigation. The public API stays
in `include/deceptus/mechanisms.h`; no virtual dispatch, heap objects or additional
per-instance state are introduced by the source split. Mechanism definitions
are immutable imported data; mutable instances live in the caller-owned
`DeceptusMechanismSet`. Source ids link levers, interactions and saved room
state to those instances. Collision queries and logical visual state are shared;
the backend selects console graphics without implementing mechanism rules.

Binding also builds a fixed spatial index for solid mechanism candidates. Two
arrays of 32 bitsets cover 64px columns and rows; a query intersects their masks
before checking the original collision rules. Enabled, timed and destroyed
states remain live checks. Unusual or out-of-range rectangles conservatively
retain all relevant candidates. The index belongs to the caller-owned set and
allocates no memory; changing definition bounds or kinds requires reinitializing
the set. This optimization serves every backend and contains no story rules.

Use matching source basenames for future enemy ports, such as `blob_2.c` for
`blob_2.lua`. Keep source object ids, properties and state meanings. The C90,
fixed-point and fixed-capacity constraints can change representation, but should
not obscure the original engine concepts.

## Simulation and memory

Positions are signed 12.4 fixed point in room-local coordinates. Large authored
world coordinates are converted to a room origin plus local position; rooms
must remain below 2048px per axis. Collision uses an 8px grid and bounded dynamic
rectangles instead of Box2D. Slopes and many complex physical mechanisms still
need explicit treatment.

### Simulation timing

The simulation is tuned for exactly 60 ticks per second, independent of how
fast a backend draws. `deceptus/pacing.h` turns the display frames that passed
since the previous presentation into the number of ticks to run, also on 50 Hz
PAL displays (1.2 ticks per frame on average). It is the console counterpart of
the original engine's fixed time step (`FixedTimeStep` in `src/game/physics`).

```c
deceptusPacingInit(&pacing, 60, 2);             /* display rate, most ticks per presentation */
ticks = deceptusPacingTicks(&pacing, frames);   /* display frames since the last presentation */
while (ticks)
{
   --ticks;
   if (deceptusAdventureTick(&adventure, buttons))
   {
      /* room changed: load it, deceptusPacingReset, count the load as one frame */
      break;
   }
}
```

Time beyond the per-presentation limit is discarded rather than banked, and a
room load is not made up for. Below `60 / max_ticks` presentations per second
the game slows down instead. Each backend sets the limit for its renderer: every
extra tick is extra camera travel for the next presentation to stream, and a
limit set too high pushes incremental background streaming into heavier updates
until every presentation hits the limit. Catching up only pays where a tick is
cheap next to a presentation. Under Genesis Plus GX, walking in the first room
with a limit of five dropped the Mega Drive from 26-38 to 2.5-3.5 presentations
per second. A limit of two still helped there (20-37 presentations, 37-59 ticks
per second), but in the Crossroads it fell from 14.6 to 6.0 presentations and
from 14.6 to 11.9 ticks per second, and the SNES dropped to 2.2 presentations
per second. Both therefore run one tick per presentation until their renderers
are faster.

| Backend | Display frames from | Limit |
| --- | --- | ---: |
| GBA | timer 2 at F/64, 4389 counts per frame | 2 |
| SNES | `snes_vblank_count`, 50 or 60 Hz from `snes_50hz` | 1 |
| Mega Drive | SGDK `vtimer`, 50 or 60 Hz from `IS_PAL_SYSTEM` | 1 |
| Amiga | vertical blank count, PAL 50 Hz | 3 |

Input is sampled once per presentation and applied to all of its ticks; button
edges are detected against the previous tick, so a press still fires once. The
emulator route verifiers replay ordinary inputs tick for tick and need one tick
per presentation: build the cartridge they check with `DECEPTUS_PACING_LOCKED=1`
in the environment, which every build script passes on as
`-DDECEPTUS_PACING_LOCKED`.

Callers own all state. Entity, projectile, mechanism, event and draw queues have
fixed capacities. Content generation rejects unsupported features and resource
limits instead of inventing successful behavior. `deceptus_engine.content_error`
exposes runtime resource failures for debugging.

Crouching uses an 18px body instead of the standing 30px body and checks headroom
before standing. Crawling remains a separate skill. One-way drop-through has a
short bounded collision exemption. These rules are shared C; imported native
player frames supply the presentation.

## Adventure state

`deceptus_adventure` wraps the campaign/engine and stores inventory counts,
progression flags, active dialogue/page, menu selection, portal cooldown and
per-room mechanism snapshots. Current capacities are thirty-two rooms (twenty-six imported) and sixteen
inventory slots, including reserved item zero. Progression is in-memory only.

Interactions refer to source identities and bounded rectangles. Required and
forbidden flags select available actions. Item consumption, item grant and
handle installation are validated before committing the transaction. Fitting
a handle consumes the interaction input so it cannot also operate the lever on
the same press.

Persistent source bindings reapply the headtorch tunnel, released sword cage and
graveyard sinkhole state when entering a room. Ruby insertion consumes the gems
immediately; a bounded 27-tick timer releases the linked cage rectangles. Leaving
and returning during this delay preserves its remaining time. Sword ownership
persists across rooms; its animation and enemy damage state reset with the room.

Horizontal portals use the original authored entrance rectangles and arrival
positions. Room changes save and restore mechanism state and keep inventory and
flags. They do not preserve every enemy, projectile or arbitrary Lua global.
Dialogue and menu modes stop the normal adventure simulation. The title's
background has a separate presentation clock so it can keep rotating.

## Music

The consoles play tracked modules in place of the original's recorded tracks
(`assets/music`, public domain or CC0; the README there lists them).
`tools/music_assets.py` maps each original track to a module and converts it with
`tools/song_assets.py` into `assets/generated/music/songs.c`: XM patterns keep their
own cell packing, samples become signed 8-bit with ping-pong loops unrolled, and
instruments keep their key maps and volume envelopes. Console songs have at most
four channels, the Amiga's Paula hardware channels.

The shared player (`include/deceptus/song.h`, `src/audio/song.c`) follows
FastTracker 2's tick rules with linear frequencies and its effect memories, and
leaves out panning: the consoles play in mono. It only decides what each channel
plays after a tick (sample, start offset, frequency in 1/256 Hz, volume); a
platform turns that into sound. `songAdvance` steps the song for one display frame
at the display's real rate, so a platform calls it from a timer or scanline
interrupt and the music keeps time however long the game takes for a frame.

The GBA mixes four voices into Direct Sound A at 10,512 Hz, exactly 176 samples
per 280,896-cycle frame, from a VCOUNT interrupt at line 100, outside the vblank
the video commit needs. DMA 1 feeds the FIFO from two frame buffers; the inner
loop (`src/platform/gba/mixer.s`) runs as ARM code from IWRAM and adds 16 samples
at a time into an IWRAM chunk. It costs about 20 of the frame's 228 scan lines.
Compared with libopenmpt rendering the same module, the GBA's recorded output
follows its loudness (correlation 0.98) and has the same dominant pitch in 83% of
50 ms windows; the rest is the 8-bit, 10.5 kHz output.

## Sound effects

The game asks for sounds through cues (`include/deceptus/soundcue.h`), one per
sound file of the original: the jump lift-off and landing, footsteps, hurt, the
checkpoint's spawn sound, item pick-ups, lever inserts and message boxes. After
each simulation tick the adventure queues the cues of the engine's events
(`src/game/soundcue.c`), and an action queues its own, since the engine clears its
events when the next tick starts. Footsteps alternate feet and sound on the
room's surface, stone or grass, as `level.json` and `config/footsteps.json` give
it. The platform plays and empties the queue once per frame.

`tools/sound_assets.py` maps each cue to its file and volume (the footsteps'
volumes are `footsteps.json`'s) and converts a platform's bank: decoded with the
`soundfile` package, mixed to mono, resampled to the platform's rate, signed
8-bit, with the trailing silence trimmed. The GBA plays cues on two more voices of
its mixer at its own 10,512 Hz, a new cue taking the voice started longest ago.

The SNES plays both on its SPC700 with PVSnesLib's snesmod, which runs the song
by itself, so it keeps time like the others. snesmod reads Impulse Tracker
modules: `tools/it_writer.py` writes each song as IT (instrument mode, with the
XM key maps, volume envelopes and fadeout; effects mapped to their IT
counterparts) and the cues as a module of one sample each at 8 kHz, the first
module of the soundbank, whose samples snesmod loads as its effects. `smconv`
packs them into a soundbank in ROM bank 40, which both cartridges map at
`$e8` and keep for it. libopenmpt renders the IT conversion like the XM
(loudness correlation 0.985); recorded from the SNES, the music follows it
with a correlation of 0.86 at the same pitch, the rest being snesmod's BRR
samples at 32 kHz. snesmod adds 0.16% to the SNES route's display frames.

The Amiga plays the song on Paula's four channels from its vertical blank
interrupt (`src/platform/amiga/audio.c`), at the PAL rate of 49.92 Hz. Song and
cue samples sit in chip RAM. A channel restarts over three blanks, as Paula needs:
its DMA stops while the start registers are written, starts on the next blank, and
the blank after that writes the loop, or a silent word, for Paula to repeat. Cues
play at 8 kHz (period 443) on the fourth channel, which the song gets back when the
cue ends. While a room loads the system owns the machine and the music pauses.

## Rendering


The engine emits logical frame/draw information. Platform code translates it
into hardware objects and palette/tile references. Source sprite sizes remain
unchanged; a smaller display reveals less of the room.

The GBA tutorial uses separate background and foreground planes around the
player, plus a UI plane. A reference-counted tile cache adds/removes only the
incoming/outgoing rows and columns during ordinary scrolling. Dirty animated
rectangles update separately. Initial loads or large camera jumps rebuild the
visible window. See `GBA.md` for address ranges and target constraints.

UI is baked offline from the original PSD layers and bitmap font. Small text
uses the authored 13px bitmap; headings use integer nearest-neighbour copies.
The title uses an affine hardware background and sine lookup table, not a
software 3D renderer. Water spring motion is also baked offline and played as
bounded tile clips.

## Adding another backend

Keep gameplay rules and source identities in shared code, then implement the
new platform's input, graphics/resource layout, audio and save adapters. Reuse
the lossless source import, not another console's reduced palettes or tiles.
Mega Drive currently has no backend; adding one is future work, not a build
option that already succeeds.

## Water and blob behavior

Water cells are imported from the original atmosphere layer's physics tile ids,
then stored as a separate bit in the existing collision grid. The player water
module uses source horizontal/vertical speed limits and the 500ms stroke delay.
Holding A raises the player after that delay. Gravity, acceleration and the
surface-exit impulse are fixed-point approximations rather than Box2D-equivalent
forces. Entry splashes, bubbles, full progression gating and complete original
water behavior remain incomplete.

Swim artwork uses the original full 72x48 frames for both directions rather
than cropping or mirroring the other direction. The GBA packs these separately
from the standing player's metasprites. Their colours remain exact RGB555 within
a distinct OBJ palette reservation.

`src/game/enemies/blob_2.c` maps directly to `blob_2.lua`. Normal patrol uses the
source idle/walk animation cycles, pulsed movement, contact damage and stomp
lifecycle. Ground-patrol sprite transitions are compared against execution of
the original Lua with identical position inputs. Jumping and ceiling-drop
variants are reported as unsupported rather than being approximated as patrols.

`mechanisms/wind.c` maps the source wind direction to a bounded player-force
event. `player/playerwater.c` owns swim motion, exit impulse and the fixed-state
one-shot splash. Delayed adventure notifications advance independently of the
simulation, so the wetsuit warning does not freeze the upward water blocker.
