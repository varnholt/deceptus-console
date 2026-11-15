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
python tools/verify_smd_opening.py --rom build/smd/deceptus-tutorial.bin --assets-dir assets/generated/smd-opening
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
imports from `../deceptus_engine`. Portable opening C declarations are prepared
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
