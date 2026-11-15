# Mega Drive proof of concept

This is a real SGDK cartridge using the shared C90 engine. It covers the original
512×224 catacombs playtest crop, at its original pixel size, in a 320×224 view.
It is a one-room CPU/video proof of concept, separate from the 26-room opening.
The prototype patrol and portal retain the existing playtest behavior; this
does not establish support for all original enemies, mechanisms or ropes.
Music, sound and persistent saves are not implemented in the POC.

```powershell
python tools/bootstrap_smd.py --emulator
python tools/build_smd.py
python tools/verify_smd.py
python run_smd.py
```

The launcher prepares a missing cartridge and core. `--prepare` performs setup
without opening a window; `--build` rebuilds; `--rom`, `--core`, `--sdk` and
`--data` select existing inputs. Existing cartridges do not require downloading
a compiler. The POC build driver and automatic setup currently target Windows x64. SGDK 2.11 is pinned
by archive hash in `bootstrap_smd.py`; extraction uses 7-Zip or `py7zr`.

Controls: arrows move, Z/X jump, Up interacts, Enter pauses/resumes and R
restarts the prototype. The `.bin` cartridge is also usable in another Mega
Drive emulator. The project player uses Genesis Plus GX.

The converter imports the original source into `assets/source/smd-poc` before
performing target colour conversion into `assets/generated/smd`. It never reads
GBA/SNES reduced images. Three 15-colour background palettes and one actor
palette use Mega Drive RGB333 entries. Pixels are neither resampled nor dithered.
The POC uses the existing lossless prototype import, including its explicit
prototype portal graphic. The 26 original player frames are retained.

VRAM byte layout:

| Range | Use |
| --- | --- |
| `0000–bfff` | 1,536 graphics tiles |
| `c000–cfff` | 64×32 background A map |
| `d000–dfff` | 128 additional graphics tiles |
| `e000–efff` | Blank background B map |
| `f000–f27f` | Hardware sprite table |
| `f400–f7ff` | Horizontal scroll table reservation |

The SDK text/window facilities are unused; font tiles are available to artwork.
Actor pieces are aligned to four tiles and skip the map allocation as a whole,
preserving each 16×16 sprite's contiguous column-major tile sequence. All room
and actor tiles are preloaded. Larger rooms need a streaming cache before the
full opening can use this backend. The current crop consumes 1,644 of 1,664
available logical graphics slots, including alignment and the blank tile.

Frame processing uses fixed arrays and low-level SGDK sprite/scroll APIs, with
bounded DMA at VBlank. The backend reports overflow instead of silently dropping
sprites. Shared code remains unchanged. Platform-local `stdint.h` and `string.h`
adapt SGDK's missing standard header and its 16-bit memory-length ABI; all engine
pool sizes fit that bound. SDK headers remain confined to platform code.

`verify_smd.py` builds an independent desktop reference from the same room and
shared C sources. It compares every observed game update: position, camera,
health, clock, pause, grounded state, checkpoint and portal return. It also
requires one game-loop update per display frame throughout the test, and
measures 600 idle simulation updates across 600 NTSC display frames. Reports and
screenshots are written to `artifacts/smd-check`; SDK tools, assets and ROMs are
ignored outputs. These small-scene measurements are not a full-game guarantee.
