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
| `0000â€“07ff` | Background 64Ã—32 map |
| `0800â€“0fff` | Foreground 64Ã—32 map |
| `1000â€“13ff` | Interface map |
| `1400-187f` | Source bitmap font, identical glyph tiles shared, maximum 2,304 bytes |
| `1880-1fff` | 120 foreground overflow patterns |
| `2000â€“5fff` | Shared 1,024-pattern world cache |
| `6000â€“7fff` | Sprite cache |

The interface character base is `1000`, so the font begins at tile index 128.
CGRAM entries 0â€“15 belong to the interface; world tiles use banks 1â€“7. Sprite
palettes use the separate OBJ half of CGRAM.

`snes_background_prepare` maintains both layers' active 33Ã—29-tile window in a
caller-owned 32 KiB allocation in WRAM bank 7f. Pattern references, hash entries,
upload queues, conditional clips and map staging have fixed capacities. Room
loads and large camera jumps require forced blank; ordinary updates stage
incoming rows and columns. The backend reports malformed resources and cache
exhaustion instead of replacing art. Foreground uses character base `1000`, so
it can address 768 shared-cache slots plus 120 overflow patterns below `2000`.
Background uses all 1,024 shared slots. Patterns may share physical storage when
both planes can address it; otherwise the cache keeps an exact duplicate. A
fragmented allocation gets one full, atomic rebuild before reporting failure.

Most rooms give their static cells fixed slots instead. The converter
(`tools/snes_tile_slots.py`) colours each room's pattern copies so that no two
patterns that can be visible together share a slot, and marks those cells: bit 15
of the pattern word, with the slot's tile in the attribute word's low ten bits.
Loading such a cell only uploads its pattern when the slot last held another one;
it takes no reference and no hash entry. Animated clips and copies left over are
served by the cache from the room's pool (`pool_first`, `pool_end` in
`SnesStage`), which is the whole cache for rooms that keep it. Plans are cached
in `slot_plans` next to the converted assets.
This rebuild uses forced blank. The caller gives sprites and dirty interface
maps separate VBlanks. `snes_background_dma_bytes` describes the next background
commit's transfer volume.

Column staging, DMA command selection and cache bookkeeping run before VBlank.
The ordinary commit consumes eight-byte commands through a small 65816 DMA
runner on channel 6, then publishes scrolling. A conservative transfer and
command-cost estimate reserves time for SDK NMI/OAM work. A commit beyond it
runs over up to four VBlanks while the previous frame stays on screen; the map
row and column that left the window go first, so no visible cell shows a slot
whose pattern changes. Only a commit needing more uses forced blank. Initial loads that exceed the bounded command array retain the
slower forced-blank path. This protects artwork while timing and throughput are
still being measured.

The SDK compiler needs `-H -F`. Its bank-zero constant sections are converted to
bankable far-data sections. ExHiROM places compiled code and tables in physical
banks 2â€“15, reserves SDK startup banks 0â€“1, and packs binary chunks into banks
16â€“63 and 65â€“125. CPU addresses use banks c0â€“ff for the lower four MiB and
40â€“7d for the upper portion. Bank 64 contains the reset-code mirror; banks
126â€“127 are excluded because their CPU addresses select work RAM. The linker
produces both headers and vectors, and the builder recalculates both checksums
after copying the reserved startup mirror.

### SA-1 cartridge

`tools/build_snes.py --cartridge sa1` builds the same opening for an SA-1
cartridge (map mode `23`, type `34`, 128 KiB BW-RAM), where the game runs on the
SA-1 and the S-CPU keeps pacing, input, the interface and the video transfers.

ROM layout: both CPUs see four 1 MiB blocks at banks c0-ff, selected by
`$2220-$2223`. Block 0 (c0-cf) holds code and tables, blocks 1-2 (d0-ef) the
shared sprite, font and actor resources, and each room's resources sit inside one
of blocks 3-7, which the F0-FF window shows. `snesBackgroundLoad` writes the
room's block (`snes_stage_rom_blocks`, generated by the build) to `$2223`. The SA-1
shows 00:8000-ffff and 80-81:8000-ffff LoROM style, from blocks 0 and 2, so the
build copies the upper halves of ROM banks 0 and 1 (startup, vectors, FastROM
library code) there and keeps the copied ranges free. The cartridge header is the
copy at `7fc0`; the HiROM header at `ffc0` is blanked so emulators pick the SA-1
map. `main` writes the power-on block numbers once, because Snes9x maps the LoROM
windows like plain LoROM until the first write.

CPU split: `sa1_main.c` owns the adventure and frame in BW-RAM (bank 40) and
answers a mailbox in I-RAM (`sa1_mailbox.h`, 00:3100 for both CPUs): initialise;
load the room's background data and reset the sprite cache; prepare a
presentation (scenery flags, background window, sprites and their staged
transfers); and present: run the paced ticks, render, and unless a tick entered
another room prepare that frame.

The two CPUs overlap. A prepared frame goes into one of two presentation buffers
(a background packet, `SnesBackgroundPacket`, and a sprite staging), named by
the mailbox. At each commit vblank the S-CPU reads the pad, starts the next
presentation into the other buffer and then commits this one, so a picture costs
the longer of the SA-1 work and the commit rather than their sum. The packet
carries copies of the DMA list and of the map rows and columns it sends, the
staging a copy of the OAM shadow, because the SA-1 rebuilds both meanwhile. A
packet whose commit still reads the work area (a full refresh, an overflowed
list, more map words than the packet holds) is marked serial: the S-CPU then
commits before it starts the next presentation. The pad is read at the same
point as before, right after the vblank that latched it, so neither the input
delay nor the tick-locked route changes; phase 11 in the debug mailbox marks
that the next ticks have their input while the commit runs. Without a second CPU
(ExHiROM) the same loop runs the presentation after the commit and the commit
reads the work area directly.

The tutorial loop does not use the SDK's `WaitForVBlank`: after it, the SDK's
vblank interrupt copies its OAM shadow to OAM (about four lines of every vblank)
and reads the pads. `snesWaitForVblank` (`ppu_timing.h`) only watches the vblank
counter, the sprite commit writes OAM itself, `padRead` takes the pad from
`$4218` once the automatic read is done, and `snesScreenOn` replaces
`setScreenOn`. With that, the background commit starts with 29 of the 37 vblank
lines left instead of 25.
Background and sprites are split along that line on both cartridges:
`background.c` and `sprites.c` prepare without video access, while
`background_commit.c` and `sprites_commit.c` set up video at room loads (OBJ
registers, background pointers and palette, the room's ROM block) and perform
the transfers. The background work area moves to BW-RAM bank 41 on the SA-1
cartridge; both halves reach it, the sprite staging and the OAM shadow through
24-bit addresses. Sprite tiles that do not fit the commit VBlank are staged in
full; the S-CPU uploads them sixteen pieces per VBlank, or at once in forced
blank. Every object except `main.c` and `background.c` is built for the
SA-1: its initialised data becomes ROM, its globals move to BW-RAM, and its
arithmetic avoids the S-CPU multiplier/divider and their lock in work RAM
(`tcc__mul_sw`, `tcc__mull_sw`, `tcc__udiv_sw`, and `sa1_runtime.asm` for the
signed division and the `snesMulS16`/`snesSquareU16` primitives, which use the
SA-1 multiplier). The build rejects SA-1 code that touches work RAM and S-CPU
code that accesses BW-RAM globals with 16-bit absolute addressing (which would
go through the S-CPU's data bank); 24-bit accesses and addresses are fine. The
SA-1 stack lies in I-RAM below `7ff`; `snes_sa1_stack_low_water` reports its
deepest point.

Initialised statics in SA-1 objects become ROM, so the build rejects writable
ones. The SDK start-up zeroes only one section named `.bss`; the build gathers
every game object's work-RAM globals in `game_bss`, which `snesClearGameRam`
(`game_ram.asm`) zeroes first thing in `main`, and the SA-1 start-up zeroes its
BW-RAM globals.

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

The OBJ backend uses the hardware 8Ã—8/16Ã—16 size pair. Converted metadata marks
each occupied 8Ã—8 quadrant of a source 16Ã—16 piece. Fully occupied pieces use
one large OBJ; partial pieces use only their occupied small OBJs. This keeps
the original pixels while reducing transparent fetches against the hardware
32-object/34-sliver scanline limits. Both limits are checked before publishing
OAM, with one load word per scanline: objects plus 95 in the low byte, slivers
plus 93 in the high byte, so adding a piece's load sets bit 7 or 15 exactly when
a line would exceed a limit, and one add per line checks and counts (a piece
that does not fit gives back what it already added). A persistent 128-piece cache stages graphics before displaying new OAM;
resource-linked lookup avoids searching the full cache for every rope segment.
The grab ropes are drawn by the SNES sprites code through the appearance
definition's `rope_sprites` hook: the same pieces as the shared drawing, but each
link steps from piece to piece with a quotient and remainder instead of a 32-bit
multiply and divide per piece.

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
the main loop and summed scanlines per phase. On the Main Hall â†’ Altar walk
(route steps 127â€“134), a presentation took about 2,200 scanlines (8.4 display
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
| Main Hall â†’ Altar walk (`capture_showcase.py`, steps 127â€“134) | 6.8 presentations/s | 9.0 presentations/s |
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
| Main Hall â†’ Altar walk (`capture_showcase.py`, steps 127â€“134) | 9.0 presentations/s | 9.9 presentations/s |
| First room (`showcase-route.json`, steps 2â€“5) | 14.0 presentations/s | 14.5 presentations/s |
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

| Part of the tick | Walk (steps 127–134) | Stationary Loop |
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

### Cell streaming in assembly

A local profile of the walk (counters and the PPU line latch around each part,
not part of the repository) showed why the probe in assembly alone helped
little: about half of all streamed loads missed the pattern hash and almost half
of the releases dropped a pattern's last reference. Walking into new scenery
mostly brings patterns that are not resident yet. Both cases still ran the whole
cell in compiled C.

`background_cell.asm` now handles these cells in 65816 code:

- `snesBackgroundLoadCellFast` reads the cell's pattern and attributes from
  the room through a small view that `background.c` fills when the room or the
  scenery flags change. A resident pattern gains a reference; a new one takes
  the other plane's slot when that plane already holds it, or the last free
  slot the plane can address, and is queued for upload as in `acquirePattern`.
- `snesBackgroundReleaseCellFast` covers every release, including the last
  reference, with the same backward-shift deletion as `hashRemove`.
- `snesBackgroundRefreshClipFast` runs both passes of an animated refresh when
  the clip is the topmost enabled source of its cells and all its frame offsets
  are valid, the case in which the C already read the frame words directly.
  Loads go through `snesBackgroundPlaceCellFast`, which takes the words the
  refresh already read.

The C functions stay the reference for host builds and tests. They are also
the fallback for rare cells: outside the room, inside the union of the plane's
enabled clip rectangles, in rows beyond the row table, with an invalid pattern,
or with a full hash table or no free slot. The assembly decides this before it
changes any state, so the C path sees exactly what it would have seen.
Compile-time checks tie every work-area, view and refresh offset the assembly
uses to the C structures; 816-tcc aligns 16-bit members and pointers, so
several offsets differ from a packed layout.

To check that the cache is exact, tick-locked builds of the previous and the new
code played the 720-step route side by side (a local script): after every step,
the 32 KiB background work area, both tile maps and the background half of
VRAM were identical. Only a few bytes of sprite VRAM differed at some steps.
Sprite preloading uploads up to 2,048 bytes per VBlank, so on a slow frame a
write can land outside VBlank, which depends on timing and is not related to
the background.

| Measurement | Before | After |
| --- | --- | --- |
| Main Hall â†’ Altar walk (`capture_showcase.py`, steps 127â€“134) | 9.9 presentations/s | 13.0 presentations/s |
| First room (`showcase-route.json`, steps 2â€“5) | 14.5 presentations/s | 16.5 presentations/s |
| Full route, tick-exact cartridge, display frames | 194,370 | 171,403 |

Each step contributed: the resident-pattern load path raised the walk from 9.9
to 10.2, slot allocation and release to 12.1, and the clip refresh to 13.0. The
first room has no animated clips. The tick-exact cartridge still passes all 56
checkpoints with 29 health and zero deaths.

The profile of the walk after these changes, in scanlines per presentation
(about 1,260 in the profiling build):

| Part | Lines |
| --- | --- |
| Background prepare | 399 (streaming 199, clip refresh 70, room loads 50, rest 80) |
| Sprite preparation | 203 |
| Simulation ticks | 121 |
| `deceptusRender` | 117 |
| Sprite tile preload | 83 |
| UI and debug packet | 16 |
| Waiting for VBlank and commits | about 320 |

The preload line is mostly waiting: `snesSpritesPreload` waits for a VBlank
before it uploads pending sprite tiles, and the loop then waits for another
VBlank to commit. Uploading those tiles in the commit VBlank, when they fit next
to the background transfers, would save up to a display frame per presentation
that brings new sprite tiles.

### Grab rope on the SNES

The Mausoleum's grab rope has 22 nodes. Its physics and drawing cost the 65816
more than twenty display frames per simulation tick, so the SNES tooling caps
grab ropes at three nodes (`SNES_ROPE_SEGMENTS` in `snes_tutorial_assets.py`;
the Amiga uses six). The rope still swings; it bends at two joints instead of
21. Node k of n rests at k / n of the rope height, so fewer nodes would lift the
lowest node and the rope could no longer be reached from the floor. The shared
cap (`capped_rope` in `world_export.py`) therefore stretches the height so the
lowest node keeps the original depth: 234 to 335 pixels at three nodes.

The shorter chain swings differently and lands the player elsewhere, so the
recorded route replaces three steps after the swing for the SNES
(`platform_steps` in `opening-route.json`; the step count and every checkpoint
stay the same).

The rope arithmetic uses the SNES versions of the primitives in
`include/arith.h`, mapped by `src/platform/snes/platform_arith.h` to
`engine_math.asm`: squares from the CPU's 8x8 multiplier, the digit-by-digit
root, the signed 16x16 product and the 32/16 quotient (16 steps when the
quotient fits a word). The C references are in `src/engine/arith.c`. A local test build compared them with the C on the console over about
60,000 inputs without a difference. Drawing walks the chain once per frame
(`deceptusGrabRopePointFrom`) instead of from the anchor for every piece; a
host check of 670,000 points matched `deceptusGrabRopePointAt`.

| Room 22 (tick-locked cartridge) | Presentations/s |
| --- | ---: |
| 22-node rope, before | 0.8 |
| Root, corrections and bins without 32-bit divides (#28) | 1.3 |
| Eight nodes | 2.4 |
| 65816 root and quotient | 2.9 |
| Three nodes | 4.9 |
| One pass for drawing | 5.0 |
| 65816 product, 16-step quotient | 5.6 |

The tick-exact cartridge passes all 56 checkpoints with 29 health and zero
deaths in 106,066 display frames (143,238 before the cap). The room now costs
about 3,000 scanlines per presentation: the same ~1,700 as other rooms, the
rope's physics about 500, and the drawing of its 29 pieces about 1,000.

### Video writes inside vblank

Video memory, palettes and OAM accept writes only during vblank (lines 225 to
261 of 262) or forced blank. A local test build latched the PPU line counter
after every part of the commit over the whole route. On the previous code the
background transfers always finished in time, but the sprite palettes ended
after vblank in 14% of presentations and the OAM transfer in 27%, up to 20 lines
into the picture, and sprite tile uploads of many pieces ran late as well.
Snes9x accepts such writes by default; real hardware drops or misplaces them.

The commit now runs every transfer at the start of vblank and the CPU
bookkeeping afterwards:

- The background list, then the sprite part: the frame's object palettes copied
  into one buffer outside vblank and sent as one transfer, the OAM, and up to
  eight newly needed sprite pieces staged as a transfer list
  (`snesSpritesCommitTransfers`). The tiles and the OAM that refers to them stay
  in the same vblank.
- `ppu_timing.h` reads the line counter. When the background leaves fewer vblank
  lines than the sprite part needs (ten plus one per staged tile transfer), the
  sprite part waits for the next vblank.
- More new pieces are uploaded before the commit in vblanks of their own, at
  most sixteen per vblank, as a transfer list instead of one library call per
  half piece.
- The transfer runner (`vram_dma.asm`, shared by background and sprites) reads
  each command through the data bank and writes the DMA registers through the
  direct page. Per command its cost fell from about 0.6 to about 0.3 lines;
  with dozens of small transfers per commit this, not the bytes, decides the
  time.

Over the whole route every transfer now ends at most 35 lines into vblank. With
Snes9x's `snes9x_block_invalid_vram_access` enabled, the tick-exact cartridge
produces the same video memory after every one of the 720 route steps as
without it, and the background state equals the previous code's. The SNES route
verifier and `capture_showcase.py` now run with that option, so late writes show
up as they would on hardware. Frame rates per room are unchanged; the full route
takes 105,830 display frames.

### Picture check

`tools/verify_snes_picture.py` plays the route on a pacing-locked cartridge and
checks what the console shows, independent of the game state the route
verifier compares:

- every display frame during play: a mostly black picture away from a room
  change is a flash (forced blank);
- every N presentations, after the commit: each cell of the cached background
  window in video memory equals the converted room data (tile bytes, palette,
  flips, current animation frames); each visible object uses a slot of the
  current frame whose tiles equal its sprite resource; exactly one player body
  is requested.

Each detector was confirmed by corrupting one background tile and one object
tile in video memory, which it reported. On the current code it finds the black
flashes from sprite slot pressure (32 in the first 40 route steps) and no wrong
background or object tiles. A paced build additionally requests two player
bodies in some frames of the Main Hall walk; that was the water splash (see
below). The SNES rebuild uses this check as its acceptance test.

### Compiler: `a || !b`

816-tcc (PVSnesLib 4.7.0) generates wrong code for a logical or whose right
operand is a logical not. In `if (a || !b)` a true `a` jumps past the body
instead of into it, so the body runs only when both `a` and `b` are false; the
same holds for `a || !b` used as a value or loop condition. Parentheses do not
change it; `a || (b == 0)`, `a || b` and `a && !b` compile correctly.

The water splash showed it: `if (++splash_frame >= 20 || !water)` never ended
the splash, its frame number ran past the last of its 20 images, and resource
numbers 520 and up are the sword player. The Main Hall then showed a second,
sword-holding player standing on the water.

Shared code writes these tests as `(a) || (b == 0)`, which also reads more
plainly, and `tools/build_snes.py` stops with the file and line of any `|| !` in
the code it compiles, so the pattern cannot come back. The check is textual and
also rejects the harmless `a || (!b && c)`.

### Compiler: `p->x = a && b`

816-tcc also miscompiles the value of an `&&`, `||` or `?:` expression stored
through a computed pointer. It computes the target address, but saves it to a
stack temporary on one branch of the condition only; the other branch stores
through whatever that temporary held. In `staging->deferred = !forced && pieces
&& pieces <= 8` with `forced` false, the store went to SA-1 I-RAM 00:0100, the
mailbox's request counter, and made the SA-1 run a presentation twice. Through a
fixed address (`snes_sprite_staging.deferred = ...`) the same line compiled
correctly, which is why it only appeared once the stagings were doubled. The
SNES platform code sets such values with `if`/`else` instead; conditions in `if`
and loop headers are not affected.
