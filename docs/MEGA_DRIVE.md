# Mega Drive opening and prototype

The experimental Mega Drive cartridge now runs the same 26-room opening as GBA
and SNES. The normal-input emulator route passes all 56 checkpoints through the
key and lever handle, solar seal, headtorch, graveyard rubies, Crypt return,
altar ruby use, sword acquisition and attack. It finishes with 29 health and
zero deaths, matching native progression and damage. This establishes the
opening workflow; later game content, sound and persistent saves remain unfinished.

Performance remains below target. The verified full replay used 70,675 display
frames for 17,884 simulation ticks, approximately 15.2 simulation updates/s at
NTSC timing, including loading and rendering. The retained one-room proof of concept has a
separate verified 600-updates/600-display-frames result; its 60 Hz measurement
does not describe the opening.

## Build and run

```powershell
python tools/bootstrap_smd.py --emulator
python tools/build_smd.py --scene tutorial
$env:DECEPTUS_PACING_LOCKED = "1"; python tools/build_smd.py --scene tutorial --output-dir build/smd-verify
python tools/verify_smd_opening.py --rom build/smd-verify/deceptus-tutorial.bin --assets-dir assets/generated/smd-opening
Remove-Item Env:DECEPTUS_PACING_LOCKED
python run_smd.py
```

`run_smd.py` selects the opening explicitly. Use `--scene prototype` for the
small, faster test. `--prepare` performs setup without opening a window;
`--build` rebuilds the selected scene. `--rom`, `--core`, `--sdk` and `--data`
select existing inputs. Existing cartridges do not require downloading a
compiler. The `.bin` cartridge also works in another Mega Drive emulator.
The project player uses Genesis Plus GX with a six-button controller.

The build setup currently targets Windows x64. SGDK 2.11 is pinned by archive
hash in `bootstrap_smd.py`; extraction uses 7-Zip or `py7zr`. SDK tools,
converted artwork and ROMs are ignored outputs.

| Keyboard | Opening action |
| --- | --- |
| Arrows | Move; Up interacts, Down bends |
| Z / Space | B: jump |
| X | C: jump or go back |
| C | A: sword attack |
| Q | X: confirm |
| E | Z: go back |
| Tab | Mode: inventory |
| Enter | Start: pause/menu |

The prototype retains its older controls, including R to restart.

## Original artwork and cartridge resources

The converter reads original RGB artwork and lossless collision/mechanism
imports from bundled `assets/game/data/` by default. Portable opening C declarations are prepared
from those imports. It never reads colour-reduced GBA or SNES pixels.
Every pixel keeps its authored size and position; no resampling or dithering is
used. The native bitmap font retains every ink pixel, cropping only the common
transparent top/bottom rows into two 8px tile rows.

Two RGB333 palette banks serve background and foreground tiles. Actor artwork
shares bank 2: black, thirteen fitted source colours and a reserved UI white.
Other mechanisms and enemies share bank 3 with fifteen opaque colours. These
palettes remain stable across animation frames. This palette reduction is a
visible console adaptation, separate from the lossless import.

The 26-room library includes 1,065 logical sprite frames, original trim offsets,
whole-piece occupancy and exact 8x8 quadrant masks. Sprite pieces retain the
Mega Drive's column-major tile order. A bounded cache streams original-size
background/foreground graphics, while sprite cache slots and scanline budgets
report explicit errors rather than silently discarding artwork.

The initial converted graphics occupy 3,508,209 bytes, including random-access
RLE offsets, inside a flat 4 MiB cartridge budget. Raw source-derived target
binaries are retained beside compressed output for validation. Each 32-byte
world tile or 128-byte sprite piece is an independent byte-RLE block; map and
animation rows use four-byte cell-tuple RLE. The backend can read one cell
without decompressing a whole room. All 88,151 compressed blocks were checked
against their raw equivalents. Code, metadata and alignment consume additional
ROM space and are checked by the final linker/build.

The world cache has 1,152 logical slots. All static viewports and 285,137
individual animation/view cases were audited; simultaneous animation bounds
were also checked. The highest measured requirement is 1,085 patterns in the
Sewers. This covers tile capacity, not frame rate or hardware validation.

## Verification and profiling

The opening owns its bounded, direct presentation transfers and skips SGDK's
unused DMA queue allocation. It also skips SGDK's default font, which the
interface never uses: `VDP_init` unpacked it through a temporary heap buffer of
about 3 KB, nearly all the heap the static data leaves, so start-up failed as
soon as the static data grew by a few dozen bytes. The build compiles the SDK
translation units that make these calls (`sys.c`, `vdp.c`) separately without
LTO, so the linker wrappers for `DMA_init` and `VDP_loadFont` remain effective;
inlined or cloned calls would bypass them. The SDK files themselves remain
unchanged; actual cartridge startup is part of the opening replay.

`verify_smd_opening.py` compares the same ordinary inputs against desktop C and
the actual cartridge, including positions, inventory, flags, projectiles and
damage. It does not patch emulator RAM. Reports and screenshots are written to
`artifacts/smd-opening-check`. `--assets-dir` must point to the generated
metadata used for that cartridge.

```powershell
python tools/benchmark_smd_opening.py
```

The benchmark compiles separate stationary fixtures and uses 3,000 warm-up plus
1,200 sampled display frames. After sprite scanline-range optimization, measurements were:

| Scene | Simulation updates/s | Largest sampled work buckets |
| --- | ---: | --- |
| Cell | 49.7 | Sprite preparation 14.33% |
| Crypt | 19.55 | Sprite preparation 64.75% |
| Loop | 26.1 | Sprite preparation 41.83%; gameplay 7.92% |
| Altar | 16.75 | Background preparation 34.17%; sprites 19%; background commit 18.67% |

Remaining samples include VBlank waits and other work. These are display-boundary
phase samples from instrumented, warmed fixtures, not cycle-accurate profiles
or whole-route performance. The fixtures use compiled setup, leave production
sources and cartridges unchanged, and report their own ROM hashes and errors
in `artifacts/smd-benchmark`. The measured range-optimized results above are
retained in `artifacts/smd-benchmark-ranges`.

### Sprite and background preparation pass

A later pass removed per-presentation work without changing what is drawn or
simulated. Each request now indexes its resident pieces once instead of walking
its slot list for every piece, whole 16x16 pieces are written to the sprite
cache inline instead of through two seven-argument calls, and the fast budget
check only scans the lines sprites touched. The background hash deletes by
backward shift, so scrolling no longer leaves tombstones that forced a rehash
of every resident slot. An animated clip that steps to its next frame compares
both frames' rows side by side and only refreshes cells whose data changed,
and a refreshed cell whose pattern and attributes are already in the map keeps
its slot. The debug packet samples the heap's free size on room load instead
of walking the heap for every packet; the word keeps its meaning and position.

Measured on the same machine and emulator (Genesis Plus GX 1.7.4), default
pacing, `python tools/benchmark_smd_opening.py` (3,000 warm-up, 1,200 sampled
frames) and `python tools/capture_showcase.py smd` on the real route. CPU load
is the mean `SYS_getCPULoad` value per published packet, in percent of a frame:

| Scene | Updates/s before | Updates/s after | CPU load before | CPU load after |
| --- | ---: | ---: | ---: | ---: |
| Cell | 49.7 | 54.4 | 96.3 | 81.5 |
| Loop | 27.75 | 28.25 | 174.9 | 160.4 |
| Crypt | 24.2 | 27.2 | 214.8 | 186.3 |
| Altar | 16.85 | 23.2 | 302.0 | 223.0 |
| Route steps 127-134 (Main Hall to Altar) | 14.2 | 16.1 | | |
| Route steps 27-52 (Crossroads) | 14.6 | 16.8 | | |

The tick-locked cartridge (`DECEPTUS_PACING_LOCKED=1`) still passes all 56
checkpoints of `tools/verify_smd_opening.py`. The heap word in the packet is
72 bytes lower than before because the new caches live in work RAM.

The sampled phase buckets alias with the VBlank wait, so they show where the
CPU tends to be at a display boundary rather than how long each phase takes;
comparing `SYS_getCPULoad` and timing sections with the V counter gave a more
reliable picture. On the walk, background preparation is still the largest
cost (about two of the four display frames per presentation), mostly scroll
column and row loads and animated clips whose cells genuinely change; keeping
recently released clip patterns resident would avoid their re-decode and
upload. A per-row cursor for run-length cell lookups, and building with `-O2`
(15.2 route updates/s) or `-Os` (14.1), did not pay off; `-O3` stays.

### Resident background patterns and the sprite piece cost

A second pass followed the two leads above.

Background patterns whose last cell is released now stay in VRAM and in the
pattern hash. Their slots go to the back of a free ring and a pattern is only
evicted when its slot is taken again, so an animated clip that cycles back to a
frame, or a scroll that returns, finds its patterns resident instead of decoding
and uploading them again. Keeping up to all 1,151 slots hashed exposed the old
hash home (`pattern ^ pattern >> 5`): pattern numbers come in runs, and the runs
formed probe clusters long enough to make the Altar four times slower. A
Fibonacci hash (`pattern * 40503 >> 5`) keeps probes short. A clip cell that
changed with a frame step is taken from the row reader that found the change,
instead of resolving the cell again with a walk from the start of its run-length
row; a clip overlapped by a later enabled clip keeps the full lookup, because the
later clip wins there.

The sprite piece cost was measured by timing sections with the V counter
(`vtimer * 262` plus the remapped line), calibrated against a 10,000-cycle
`dbra` loop that read back as 10,100 to 10,600 cycles. In the Crypt fixture the
request emitter took about 100,000 cycles per presentation for 48 objects and 11
requests. No single step dominated: partial pieces cost about 1,800 cycles each
through `smdObjQuads` and one or two `smdObjPiece` calls with seven stacked
arguments; building and clearing the per-request link index about 700 cycles
per request; the hardware entries of whole pieces were cheap. The rest was loop
bookkeeping: byte counters re-extended for every index, globals re-read per
piece, and values spilled to the stack because the exact-budget path, slot
misses and the split path were all inlined into one loop. The fast-budget
emitter now has its own loop with word counters and a local object count,
enters the line budget once per row of whole pieces, and splits partial pieces
inline (`smdObjSplit`, covered by `tests/test_smd_sprite_runtime.py`). The
exact-budget pass, `smdObjQuads` and `smdObjSlotFor` stay out of line. The
emitter dropped to about 76,000 cycles in the Crypt. A fit over the Crypt and
Loop fixtures still puts the cost at roughly 1,000 cycles per piece plus 2,600
per request, and the shared appearance visit adds 7,000 to 25,000 cycles per
presentation; further gains need a different data layout, such as per-frame
lists of opaque pieces from the converter, rather than tuning this loop.

Same machine, emulator and commands as above; the walk is
`python tools/capture_showcase.py smd --first-step 127 --last-step 134`, the
first room is `--route docs/media/showcase-route.json --first-step 2
--last-step 5`:

| Scene | Updates/s before | Updates/s after | CPU load before | CPU load after |
| --- | ---: | ---: | ---: | ---: |
| Cell | 54.4 | 54.55 | 81.5 | 78.9 |
| Loop | 28.25 | 28.35 | 160.4 | 151.5 |
| Crypt | 27.2 | 28.3 | 186.3 | 165.4 |
| Altar | 23.2 | 25.75 | 223.0 | 185.7 |
| Route steps 127-134 (Main Hall to Altar), presentations/s | 16.1 | 22.3 | | |
| First room, route steps 2-5, presentations/s | 32.8 | 34.1 | | |

The tick-locked cartridge still passes all 56 checkpoints of
`python deceptus.py verify smd`.

Tried and dropped: clearing only the touched lines of the sprite line budget
instead of the whole 676-byte union made no measurable difference; replacing
the global read-modify-writes of whole pieces alone, without the loop split,
saved almost nothing, which is what pointed at loop bookkeeping instead. In the
stationary fixtures the Loop and Cell are now bound by the shared appearance
visit and by commit and preload overhead of 10,000 to 15,000 cycles each that
is present even when nothing is uploaded; that fixed cost is the next thing to
measure.

A third pass measured that fixed cost. Most of it was bookkeeping that scanned
every entry to find nothing. Testing one background map row by its dirty bit
(`1U << (row & 15)` on a word picked by `row >> 4`) took about 150 cycles,
and both the DMA byte count at the end of `smdBackgroundPrepare` and
`smdBackgroundCommit` tested all 64 rows of the two planes, so each spent close
to 10,000 cycles on a still screen. Both now walk only the set bits of the
dirty words. `smdSpritesMeasure` and `smdSpritesPreload` each scanned the 64
sprite slots for pending tiles (about 4,500 cycles each). A count of pending
slots now replaces the measure scan and lets the preload stop as soon as nothing
is left. The `held = used` copy in `smdSpritesCommit` handles four slots per
pass. In the Cell, background prepare dropped from 11,200 to 2,400 cycles,
background commit from about 13,600 to well under one V-counter line, and
sprite commit from 6,200 to 4,200. The Cell's remaining preload cost of about
10,000 cycles is real work: one presentation in five uploads two to ten sprite
pieces that were evicted from the 64-slot cache. Same commands as above:

| Scene | Updates/s before | Updates/s after | CPU load before | CPU load after |
| --- | ---: | ---: | ---: | ---: |
| Cell | 54.55 | 57.45 | 78.9 | 56.8 |
| Loop | 28.35 | 28.5 | 151.5 | 130.2 |
| Crypt | 28.3 | 28.35 | 165.4 | 138.4 |
| Altar | 25.75 | 26.5 | 185.7 | 163.1 |
| First room, route steps 2-5, presentations/s | 34.1 | 36.4 | | |

`python deceptus.py verify smd` still verifies the whole route.

### Game speed

The simulation runs at 60 ticks per second on every target; a presentation that
takes longer than a display frame catches up with up to three ticks
(`SMD_PACING_MAX_TICKS`, as on the SNES and the Amiga). The cartridge used one,
so the game ran at its frame rate: 29.9 ticks/s at 29.9 presentations/s in the
first room (route steps 2-5). With three it runs 37.1 ticks/s at 22.4
presentations/s. Catching up cannot reach full speed while a presentation takes
two or three display frames; the measurements below show where they go.

### Where a presentation goes, room by room

Timing every part of the loop with the V counter (as above, `vtimer * 262` plus
the remapped line, summed per room on the tick-locked route) showed that a tick
costs about 45 scan lines on average, a sixth of a display frame, not a whole
one. Most rooms spend 400 to 840 lines per presentation: background preparation
135 to 420 (loading the cells a scroll brings in), sprite preparation 50 to 200,
the transfers after vblank 100 to 270, and the tick 40 to 160. Room 22 was the
exception at about 2,000 lines: its 22-node grab rope cost 1,100 lines per tick
and its rope pieces another 350 in sprite preparation.

Two causes in the tick were plain waste. gcc compiles even a 16x16 product into
a call to `__mulsi3` and a division into `__divsi3`, so the shared engine now
uses the 68000 primitives in `src/platform/m68k/platform_arith.h` (`mulu`,
`muls`, `divs`, shared with the Amiga), including `arithMulU16` for the cell
lookup. And room 22's rope never went to sleep, because its box collider counted
as a moving solid; box colliders only stop moveable boxes, so the rope now sleeps
when nobody holds it. Together: tick time over the route -25%, room 22's tick
1,111 to 607 lines, route 56,972 to 54,152 display frames.

A cell load cost about 3,400 cycles: half fetching the cell (walking its
run-length row from the start, past the clips), half storing it in the pattern
cache. A new window row is now read in one pass over its run-length row
(`loadRow`); rows a clip covers keep the per-cell lookup, and cells are stored
in the same order, so the cache state is unchanged. Route: 53,068 to 50,325
display frames.

Room 22's grab rope is capped at six segments, as on the Amiga
(`SMD_ROPE_SEGMENTS` in `tools/smd_opening_assets.py`, the same mechanism as the
SNES's three): every segment costs the rope solve two square roots, a division
and collision checks per tick. The route replays unchanged with the shorter
chain; route 50,325 to 48,669 display frames.

A scroll column was still loaded cell by cell, each walking its run-length row
from the start. Each window row now keeps a cursor at the left and at the right
edge column (`SmdRowCursor`), so the next column is one step on; a column left of
the cursor's run starts it again. `loadRow` also reads the rows a clip touches,
and rows that reach past the room's edge, in one pass, taking only the covered
cells through the clip lookup. `smdDecodeBlock` copies with plain pointer loops
and checks bounds once per run; sprite piece uploads after vblank fell by about
a third. Background tile uploads did not: their cost is the number of new tiles
a scroll brings in, and uploading them with CPU writes instead of one DMA each
changed nothing. The background state (resident patterns and both plane maps)
is identical at every one of the route's 18,244 pictures, and the decoder gives
the same bytes for all 90,719 blocks of the generated data. Route (with the
music added since) 53,275 to 51,136 display frames.

## Retained small prototype

```powershell
python tools/build_smd.py --scene prototype
python tools/verify_smd.py
python run_smd.py --scene prototype
```

The POC imports the original 512x224 catacombs crop, shown at 1:1 in a 320x224
view. It preloads 1,644 of 1,664 logical tile slots using three background
palettes and one actor palette. `verify_smd.py` compares every observed update
against desktop C for movement, collision, jumps, scrolling, pause, restart and
checkpoint/portal return. Its idle sample records 600 simulation updates over
600 NTSC display frames, and all 1,010 post-boot display frames in the test have
one game-loop update. This smaller result remains distinct from the opening.
