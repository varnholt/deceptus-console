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
unused DMA queue allocation. Its build compiles the SDK startup translation
unit separately without LTO, so the linker wrapper for `DMA_init` remains
effective. Allowing startup to inline that call can unexpectedly allocate the
queue and prevent boot within the 64 KiB RAM budget. The SDK files themselves
remain unchanged; actual cartridge startup is part of the opening replay.

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
