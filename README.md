# Deceptus C Engine

A portable C game engine and console adaptation of Deceptus. **GBA is the first
complete-game target.** A working SNES prototype is retained; Mega Drive is a
future backend. The local folder currently keeps the name `deceptus-snes`.

The latest development build is the original prison opening, with twenty-six
imported rooms, source dialogue, inventory and lever interactions, automatic
room changes, native player artwork, foreground occlusion and menus. This is
an opening playtest; later game content, audio and persistent saves remain unfinished.
The normal-input verification route reaches the graveyard, takes the rubies,
returns through the crypt, uses them at the altar, acquires the sword and attacks.

`deceptus_engine` remains the authoritative repository for maps, artwork,
animation definitions, Lua scripts, music and sound effects. Build tools read
that checkout and generate ignored target resources. Artwork is not maintained
twice, resized to fit a console screen, or taken from an already reduced console
version.

## Quick start

To play an existing build, run from the repository root:

```powershell
python run_gba.py
python run_snes.py
```

Each launcher downloads its missing emulator core into `.tools/`, builds a
missing cartridge, and opens the project player. Existing cores and cartridges
are reused. `--build` rebuilds first; `--prepare` performs setup without opening
a window. Missing Python packages are installed in a project-local `.venv`.
Automatic emulator downloads support Windows x64; other hosts can supply
`--core PATH`. Building GBA still requires LLVM; SNES fetches its missing local SDK.

Keep both checkouts beside one another:

```text
workspace/
  deceptus_engine/data/
  deceptus-snes/          this repository; may still be named deceptus-snes
```

Install Python, the requirements below, and LLVM with `clang`, `ld.lld` and
`llvm-objcopy` on PATH. The verified environment is Windows x64 with LLVM 18.1.8
and Python 3.13. Other host platforms have not been verified.

```powershell
python -m pip install -r requirements.txt
python tools/bootstrap.py --target gba --emulator
python tools/build_gba.py --scene tutorial
python tools/play.py --backend gba --rom build/gba/deceptus-tutorial.gba
```

The ROM also runs in a standalone GBA emulator. Numbered local playtests are
kept in `build/playtest/`; build outputs and ROMs are not tracked in Git.
The bootstrap downloads optional emulator cores and build references into
`.tools/`; it does not bundle them into the game.

## Controls

| Project player keyboard | GBA input | Tutorial action |
| --- | --- | --- |
| Left / Right | D-pad | Move |
| Z / Space | A | Jump; confirm a dialogue or menu item |
| X | B | Jump during play; go back in menus |
| Up | Up | Interact |
| Down | Down | Bend down; crawling requires its separate skill |
| Down + Z | Down + A | Drop through a one-way floor |
| Enter | Start | Open pause menu; confirm a menu selection |
| Tab / R | Select | Inventory |
| C / Q / E | L / R | Swing the acquired sword |

Use Up/Down to browse owned inventory items. Up at the matching object uses a
key, seal or the rubies. Sword attacks require a fresh shoulder-button press.

The older four-area preview and SNES prototype use Select to restart instead
of opening inventory. A wall jump needs a fresh jump press while touching a wall.
State is held in RAM. Closing the emulator loses progression; save storage and
audio playback are not implemented yet.

## Current development build

The tutorial uses the original Deceptum bitmap font at its authored 13px size.
Large headings are exact 2x pixel copies, not newly rasterized 24px glyphs.
The opening also includes:

- Original ground-patrol blob, including its animated movement, contact damage and stomp death.
- Basic swimming using source atmosphere tiles, original swim sprites and a delayed A-button ascent.
- Original animated shrine column and a one-shot splash only when falling into water.
- Small water-exit hop, underwater colour tint and the original upward wind/wetsuit warning.
- New Game, Options and Quit; pause, controls, inventory and quit confirmation.
- Native-size dialogue windows, paged source text and animated interaction cues.
- The key, locker, lever handle and missing-handle rules, plus persistent flags
  and mechanism state when returning to a room.
- The Reading Room solar seal, Fool's Ledge headtorch chest, dark Sealed Cells,
  graveyard owl rubies, sinkhole/crypt return and delayed sword-cage release.
- Equipped sword artwork and basic source-timed attacks against supported blobs.
- Original room entrance rectangles, including Crossroads to Fool's Ledge and Main Hall to Mines Antechamber.
- Original one-way floor decoration and player occlusion by foreground artwork.
- Streamed room graphics and offline baked water surfaces; the player remains
  visible underwater. Complete water/progression fidelity remains unfinished.

See [current status](docs/STATUS.md) for verified coverage and open work. Imported
rooms and working isolated actions do not establish a complete playable route.

## Build and verification

```powershell
cmake -S . -B build/host
cmake --build build/host --config Debug
ctest --test-dir build/host -C Debug --output-on-failure
python tools/verify_tutorial.py
```

The tutorial verifier checks controlled scene starts, selected room connections,
UI, item rules, sword input and rendering. `tools/gba_emulator.py --scene tutorial`
replays the separate normal-input opening route. See
[building and testing](docs/BUILDING.md) for the LLVM path arguments and test
prerequisites.

The older curated four-area preview remains available:

```powershell
python tools/build_gba.py --scene tonight
python tools/gba_emulator.py --scene tonight
python tools/package_preview.py --build
```

`tonight` is a historical build-scene name; it does not appear in the game menu.
The default build scene is now `tutorial`.
`package_preview.py` packages that curated preview, not the current tutorial.

The separate SNES prototype uses project-local PVSnesLib:

```powershell
python tools/bootstrap.py --target snes --emulator
python tools/build_snes.py
python tools/emulator.py
python tools/play.py --backend snes
```

## Automated builds

The **Build GBA** GitHub Actions workflow runs on pushes and pull requests and
can be started manually. It checks out the original game data at `SOURCE_REVISION`,
builds the C engine and tutorial, runs tests, and uploads a downloadable cartridge
artifact. See [CI builds](docs/BUILDING.md#github-actions) for the download steps.

## Project guide

- [Building, playing and testing](docs/BUILDING.md)
- [C engine and backend architecture](docs/ARCHITECTURE.md)
- [Authoritative content and conversion pipeline](docs/CONTENT_PIPELINE.md)
- [Verified status and remaining work](docs/STATUS.md)
- [GBA memory layout and renderer](GBA.md)
- [Source inventory and full-game port strategy](PORTING.md)

## Ownership

Original code in this repository is covered by [LICENSE](LICENSE). Deceptus
content retains the terms in the main checkout's `data/LICENSE.md` and
`LICENSES/GAME-CONTENT.txt`; artwork remains credited to dstar. Dependencies
retain their upstream terms. Generated ROMs contain game content and are not
covered solely by this repository's code license.
