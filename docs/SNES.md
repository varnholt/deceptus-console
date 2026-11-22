# SNES opening backend

`tools/build_snes.py` defaults to the shared opening. The explicit command
`tools/build_snes.py --scene tutorial --rooms 26 --rom-mib 8` builds the shared
opening adventure into an 8 MiB ExHiROM cartridge. The retained one-room build
is selected with `--scene prototype`. Neither build establishes full-game
support. The initial two-room HiROM and full 26-room ExHiROM builds have booted
in Snes9x. The final 26-room ROM passes the full 56-checkpoint normal-input
route through graveyard rubies, Crypt return, altar use, sword pickup and attack,
with 29 health, zero deaths and no engine or renderer errors. Rendering performance
is still being improved; these builds do not establish 60 Hz simulation.

The final resource-batched stationary fixtures measured 7.26 simulation updates
per second in The Loop and 7.46 in The Crypt under Snes9x 1.63. Each sample used
3,000 display frames of warmup and 1,200 measured display frames at the core's
60.0988 Hz output rate, with no input, deaths or content/video errors. These
measurements describe those views; they are not a full-game performance bound.
Sprite preparation remains the largest measured cost, especially in The Crypt.

The converter reads the original import, independently of GBA palette output.
Pixels retain their source dimensions and grid, with seven world palette banks
and no dithering. Mode 1 uses two 4bpp world layers and a 2bpp interface layer.
VRAM word addresses are:

| Range | Use |
| --- | --- |
| `0000–07ff` | Background 64×32 map |
| `0800–0fff` | Foreground 64×32 map |
| `1000–13ff` | Interface map |
| `1400–1cff` | Source bitmap font, maximum 4,608 bytes |
| `1d00–1fff` | 48 foreground overflow patterns |
| `2000–5fff` | Shared 1,024-pattern world cache |
| `6000–7fff` | Sprite cache |

The interface character base is `1000`, so the font begins at tile index 128.
CGRAM entries 0–15 belong to the interface; world tiles use banks 1–7. Sprite
palettes use the separate OBJ half of CGRAM.

`snes_background_prepare` maintains both layers' active 33×29-tile window in a
caller-owned 32 KiB allocation in WRAM bank 7f. Pattern references, hash entries,
upload queues, conditional clips and map staging have fixed capacities. Room
loads and large camera jumps require forced blank; ordinary updates stage
incoming rows and columns. The backend reports malformed resources and cache
exhaustion instead of replacing art. Foreground uses character base `1000`, so
it can address 768 shared-cache slots plus 48 overflow patterns below `2000`.
Background uses all 1,024 shared slots. Patterns may share physical storage when
both planes can address it; otherwise the cache keeps an exact duplicate. A
fragmented allocation gets one full, atomic rebuild before reporting failure.
This rebuild uses forced blank. The caller gives sprites and dirty interface
maps separate VBlanks. `snes_background_dma_bytes` describes the next background
commit's transfer volume.

Column staging, DMA command selection and cache bookkeeping run before VBlank.
The ordinary commit consumes eight-byte commands through a small 65816 DMA
runner on channel 6, then publishes scrolling. A conservative transfer and
command-cost estimate reserves time for SDK NMI/OAM work; larger batches use
forced blank. Initial loads that exceed the bounded command array retain the
slower forced-blank path. This protects artwork while timing and throughput are
still being measured.

The SDK compiler needs `-H -F`. Its bank-zero constant sections are converted to
bankable far-data sections. ExHiROM places compiled code and tables in physical
banks 2–15, reserves SDK startup banks 0–1, and packs binary chunks into banks
16–63 and 65–125. CPU addresses use banks c0–ff for the lower four MiB and
40–7d for the upper portion. Bank 64 contains the reset-code mirror; banks
126–127 are excluded because their CPU addresses select work RAM. The linker
produces both headers and vectors, and the builder recalculates both checksums
after copying the reserved startup mirror.

Native cache tests exercise the actual C backend across bank-sized map chunks,
camera movement in both directions, diagonal movement, jumps, room edges and
conditional animation frames. Separate build tests verify bank placement and
the reset mirror's checksums. Actual Snes9x input routes remain necessary to
verify gameplay, display timing and source artwork together.

`python tools/verify_snes_background.py --assets-dir PATH` runs the actual cache
code over every viewport of every converted twelve-frame water clip. The initial
26-room conversion passed 157,008 views: Main Hall peaked at 991 resident patterns
with no compactions; Sewers peaked at 1,042 with three compactions over 81,672
views. This is a cache-capacity check, separate from console timing verification.

The OBJ backend uses the hardware 8×8/16×16 size pair. Converted metadata marks
each occupied 8×8 quadrant of a source 16×16 piece. Fully occupied pieces use
one large OBJ; partial pieces use only their occupied small OBJs. This keeps
the original pixels while reducing transparent fetches against the hardware
32-object/34-sliver scanline limits. Both limits are checked before publishing
OAM. A persistent 128-piece cache stages graphics before displaying new OAM;
resource-linked lookup avoids searching the full cache for every rope segment.

The interface preserves source bitmap glyphs and text in console-sized panels.
Ropes follow the shared simulated curve using source strips; the backend does
not rotate those strips. The headtorch uses a source light sprite approximation.
These are presentation adaptations, and neither a full original shader pipeline
nor complete source-menu layout parity is established.

The SDK emits exported initialized constants into a RAM-copy section even when
they are declared const. For generated immutable content units, the builder
reconstructs their labels at exact serialized byte offsets in banked ROM and
removes the redundant RAM allocations. Size mismatches fail the build. This
keeps source descriptors, stage definitions and occupancy tables in ROM.

Actual input replay also exposed 816-TCC early-return miscompilations in compound
guards. Confirmed cases are expressed as separate branches in shared C, with
native behavior checks and GBA ROM regression. Passing desktop tests alone is
insufficient for this compiler: the SNES replay checks room positions, input
consumption, damage events and renderer error fields.

Animated clips can accumulate deleted hash entries even when tile residency
remains within budget. The backend tracks those entries and rebuilds the hash
from live slot/domain pairs once the count exceeds 128. It preserves resident
pixels, maps and references. A controlled long altar check previously stalled
at tick 188; with maintenance it completes 318 presentations, including turning
off the sword ring. Native tests also exercise a deliberately saturated table.

`tools/benchmark_snes.py --help` exposes the warmup/sample settings for these
controlled ROM measurements. Fixture snapshots and their ROM hashes are kept
separate from normal-input progression evidence.

Source-resource expansion, quadrant selection, presentation reset, scanline
validation and OAM packing run in small 65816 backend functions. Cache allocation
and eviction retain the existing bounded C implementation; shared simulation
remains portable C90. The original C paths remain references. An actual Snes9x
test ROM compares 6,482 cases: 2,048 object writes, 2,048 quadrant expansions,
256 presentation resets and every one of the 1,065 resources in both directions.
Cases include clipped coordinates, mixed 8/16-pixel sizes, saturated scanlines
and full OAM. It checks every OAM byte, both scanline-counter arrays, object count,
overflow and complete cache-slot contents.
Run `python tools/verify_snes_sprites.py` after preparing the SNES assets.

With the same 3,000-frame warmup and 1,200-frame sample, the Loop advances 145
ticks (7.26 updates/s, previously 6.86); the Crypt advances 149 (7.46 updates/s,
previously 6.26). These are approximately 6% and 19% gains for those scenes.
Resource batching reduced the full 56-checkpoint route from 257,997 to 248,738
display frames. Restricting animated-background refreshes to the intersection
of the clip and visible window reduces this further to 239,841 frames. Each
refresh retains the same row order and releases all old references before
loading the new cells. Native edge/corner checks and the 157,008-view water
audit preserve the exact cache results.

In the animated Altar view, the same warm sample improves from 31 to 46 ticks
(1.55 to 2.30 updates/s); Loop and Crypt measurements remain unchanged. The
matched 318-presentation Altar release fixture drops from 12,607 to 9,530 display
frames. This remains substantially below a playable console frame rate.

### Frame-phase profile and loop changes

A local profiling build latched the PPU vertical counter at every phase write of
the main loop and summed scanlines per phase. On the Main Hall → Altar walk
(route steps 127–134), a presentation took about 2,200 scanlines (8.4 display
frames). The largest costs were background preparation (about 1,190 lines, of
which periodic hash-table rebuilds were roughly 250 and animated-clip refreshes
about 290), the simulation tick (about 200), sprite preparation (about 250) and
a full extra display frame inside `setScreenOn()`, which always waits for the
next VBlank. In the stationary Loop fixture the simulation tick alone was about
640 lines per presentation, a third of the frame; it remains portable C.

The loop now changes the screen only after it was actually blanked, so ordinary
presentations no longer wait for a second VBlank. Phase 15 is written as soon as
the debug packet is complete, because emulator checks sample the mailbox only in
phases 15 and later and those phases now fit inside VBlank. Background hash
deletions use backward shifting instead of tombstones, so the multi-frame C
rebuild no longer runs during play; lookups return the same slots. Each clip's
enabled plane is cached when the scenery flags change instead of being tested
for every cell lookup. An animated refresh skips cells whose resident pattern and
attributes already match the new frame, and reads the clip's frame words
directly when that clip is the enabled, topmost source of its rectangle. The OAM
shadow copy and slot protection after a sprite commit run in 65816 code.

Measured under Snes9x 1.63 at 60.0988 Hz with the 8 MiB ExHiROM opening:

| Measurement | Before | After |
| --- | --- | --- |
| Main Hall → Altar walk (`capture_showcase.py`, steps 127–134) | 6.8 presentations/s | 9.0 presentations/s |
| Stationary Loop (room 9, 370/696) | 7.51 updates/s | 8.46 updates/s |
| Stationary Crypt (room 25, 597/264) | 8.41 updates/s | 8.51 updates/s |
| Stationary Altar, cage open (room 7, 536/312) | 6.31 updates/s | 8.71 updates/s |

The stationary rows use the Mega Drive benchmark's fixture positions and
progress, pin the player each presentation, and run `benchmark_snes.py`'s 3,000
frame warmup and 1,200 frame sample; they are not the earlier SNES fixture
snapshots. The Crypt stays at seven display frames per presentation because its
sprite preparation, about 1,000 lines, dominates. The water-cache audit still
reports 157,008 views with peaks of 991 and 1,042 resident patterns and three
Sewers compactions. The tick-exact cartridge still passes the full 56-checkpoint
route with 29 health and zero deaths, now in 206,375 display frames instead of
239,841. Its final settle check accepts phase 15, which follows the previous
presentation's commits. `capture_showcase.py` no longer presses Start again when the
game has already left the title during its wait, which paused faster builds.

### Streaming and sprite lookup changes

A second profile of the same walk put row/column streaming at about 500 lines
per presentation for roughly 45 cell loads and releases, around 3,000 CPU
cycles per cell. Three parts of that path changed; each keeps the exact cache
results:

- The pattern-hash probe (`hashFind`) runs as 65816 code in
  `background_hash.asm`. Compiled C spent most of each probe on far-pointer
  arithmetic into the bank-7f work area. The C function remains the host
  reference, and a compile-time check ties the assembly's table offset to the
  `SnesBackgroundWork` layout.
- `snesBackgroundLoad` fills a table of row starts, so a cell lookup no
  longer calls the compiler's multiplication routine for `y * columns`. Rows
  beyond the 1,024-entry table keep the multiplication.
- Each plane keeps the list of its enabled animation clips in ascending order.
  A cell lookup visits only those clips instead of every clip of the room;
  later clips still override earlier ones.

In sprite preparation, `snesObjSlotFor` is now 65816 code that walks the
resource's slot list and marks a resident piece used. It calls the unchanged C
allocator, renamed `snesObjSlotAllocate`, only for a missing piece. Most drawn
pieces are resident, so the common case no longer enters compiled C at all.

| Measurement | Before | After |
| --- | --- | --- |
| Main Hall → Altar walk (`capture_showcase.py`, steps 127–134) | 9.0 presentations/s | 9.9 presentations/s |
| First room (`showcase-route.json`, steps 2–5) | 14.0 presentations/s | 14.5 presentations/s |
| Stationary Loop (room 9, 370/696) | 8.46 updates/s | 8.56 updates/s |
| Stationary Crypt (room 25, 597/264) | 8.51 updates/s | 9.11 updates/s |
| Stationary Altar, cage open (room 7, 536/312) | 8.71 updates/s | 9.27 updates/s |

The stationary rows use the same fixtures, warmup and sample as the previous
table. The hash probe and row table alone raised the walk to 9.7
presentations/s. The clip lists help mainly in rooms with several animations;
on the walk the phase holding streaming and clip timing still takes about 420
lines per presentation, while resource drawing fell from about 145 to 130. The
remaining streaming cost is spread over the C code around
the probe (map addressing, reference counting, dirty-row tracking), so moving
whole cell loads to assembly is the next step for scrolling. The water-cache
audit still reports 157,008 views with peaks of 991 and 1,042 resident patterns
and three Sewers compactions, and `tools/verify_snes_sprites.py` still passes
all 6,482 cases.
The tick-exact cartridge passes the full 56-checkpoint route with 29 health and
zero deaths in 194,370 display frames instead of 206,375.

The same phase marks placed around the calls in `deceptusTick` (a local build
only) show where the portable simulation spends its time on this CPU, in
scanlines per presentation:

| Part of the tick | Walk (steps 127�134) | Stationary Loop |
| --- | --- | --- |
| Input, pacing, adventure and campaign around the engine tick | 39 | 35 |
| `updatePlayer` | 78 | 195 |
| `deceptusEnemiesTick` | 40 | 251 |
| `deceptusMechanismsTick` | 28 | 143 |
| `updateActors` | 20 | 22 |
| Grab ropes, mechanism events, camera | 12 | 11 |
| Total | about 217 | about 657 |

The Loop's enemies, the player's collision and movement and the mechanisms
account for nearly all of its tick. No shared simulation code changed in this
round; any change there must stay behaviour-identical on every platform.
