# Building and testing

## Prerequisites

This checkout includes game content in `assets/game/data/`; no neighboring
engine checkout is required. Install
the pinned packages in `requirements.txt`: Pillow, NumPy and luaparser. NumPy
2.3.4 requires Python 3.11 or newer; Python 3.13 is the verified interpreter.
The source engine itself does not need to compile for asset conversion.

GBA requires LLVM's ARM-capable Clang, LLD and llvm-objcopy. Native tests require
CMake and a desktop C compiler. SNES additionally requires PVSnesLib, installed
by the project bootstrap or selected with `--sdk`.

```powershell
python -m pip install -r requirements.txt
python tools/bootstrap.py --target gba --emulator
python tools/build_gba.py --scene tutorial
```

For the verified Windows tool locations:

```powershell
python tools/build_gba.py --scene tutorial --clang C:/build_utils/llvm/bin/clang.exe --linker C:/build_utils/llvm/bin/ld.lld.exe --objcopy C:/build_utils/llvm/bin/llvm-objcopy.exe
```

Pass `--data D:/other/deceptus_engine/data` to select another source checkout.
The tutorial build generates twenty-six rooms, UI resources, water clips and
native definitions before compiling the freestanding C engine and GBA backend.
It produces `build/gba/deceptus-tutorial.gba`, the latest-build alias
`deceptus.gba`, ELF symbols, a linker map and `build.json` with ROM size/hash.
A later scene build replaces the alias and generated resources; numbered
playtest copies remain unchanged.

## Play

The root launchers handle missing emulator cores and cartridges:

```powershell
python run_gba.py
python run_snes.py
python run_smd.py
python run_gba.py --build
python run_snes.py --prepare
```

The Mega Drive launcher defaults to the opening cartridge,
`build/smd/deceptus-tutorial.bin`. Use `python run_smd.py --scene prototype`
for the separate one-room [proof of concept](MEGA_DRIVE.md).
The selected scene is forwarded when `--build` is used; `--rom PATH` plays an
existing cartridge with the selected scene's controller setup.

They cache cores in `.tools/` and do not download SDKs merely to play an existing
ROM. Missing Python dependencies use `.venv`, without changing global packages.
`--rom PATH` selects an existing cartridge, `--core PATH` selects an installed
native libretro core, and `--data PATH` selects original content for a build.
GBA defaults to the tutorial; `--scene tonight` selects the older preview.
Automatic core downloads currently require Windows x64 Python. On other hosts,
provide a matching native core and a Python installation with Tk support.

```powershell
python tools/play.py --backend gba --rom build/gba/deceptus-tutorial.gba
```

The project player uses its downloaded mGBA libretro core. `--core PATH` selects
another native core. Standalone mGBA can open the same ROM. Use an integer window
scale and disable smoothing to preserve the artwork's pixel grid. No external
BIOS is distributed. Audio is currently silent in both ROM and project player.

## Native and conversion checks

```powershell
cmake -S . -B build/host
cmake --build build/host --config Debug
ctest --test-dir build/host -C Debug --output-on-failure
python -m unittest discover -s tests -p test_*.py
```

CMake's `-DDECEPTUS_DATA_DIR=PATH` changes the source data folder. The native
preview uses the small SNES-oriented scene; it does not display the full GBA
adventure. Tests cover engine collision, mechanisms, source callbacks, enemy
schedules, adventure interactions, animation, target graphics and pixel fonts.
Some generated-resource checks skip when their inputs have not been built.
Build the tutorial before claiming its conversion checks have run.

Where available, `LUA_REFERENCE_SOURCE_DIR` supplies the original Lua sources
for differential tests; a system Lua 5.4 installation is also supported. Without
either, expected-state checks run but do not
establish equivalence against actual Lua execution.

## Real GBA scene checks

After building `--scene tutorial`:

```powershell
python tools/verify_tutorial.py --clang C:/build_utils/llvm/bin/clang.exe --linker C:/build_utils/llvm/bin/ld.lld.exe --objcopy C:/build_utils/llvm/bin/llvm-objcopy.exe
```

This builds controlled-start integration fixtures from the same game and assets.
It checks dialogue, menu navigation, rotation cadence, underwater visibility and
the Crossroads/Fool's Ledge return connection. It restores production build
outputs afterward. Reports and screenshots go to `artifacts/tutorial-check/`.
It deliberately does not claim to play the entire tutorial with normal input.

The separate normal-input route starts at New Game and checks progression
against both native C and the production ROM:

```powershell
python tools/gba_emulator.py --scene tutorial
```

This does not inject player positions, inventory, flags or emulator memory.
Its recorded checkpoints and scope live in `tests/fixtures/opening-route.json`;
reports and screenshots go to `artifacts/opening-check/`. Room loading may consume
display frames without a simulation tick, so the verifier holds ordinary inputs
until the matching native simulation tick is reached.

The older preview has its own complete curated input route:

```powershell
python tools/build_gba.py --scene tonight
python tools/gba_emulator.py --scene tonight
python tools/package_preview.py --build
```

Standalone `lever`, `platforms` and `water` scenes have dedicated emulator checks.
Use `python tools/gba_emulator.py --scene water` after building that scene.
Generated resources are scene-specific, so rebuild before switching tests.

## SNES regression check

```powershell
python tools/bootstrap.py --target snes --emulator
python tools/build_snes.py --scene prototype
python tools/emulator.py
```

This verifies the retained prototype's movement, jump, pause, checkpoint and
portal route in Snes9x. It does not run the GBA tutorial. GBA, SNES and desktop
share C engine code, so shared simulation changes need both console checks.

The opening adventure has a separate banked SNES build:

```powershell
python tools/build_snes.py --scene tutorial --rooms 26 --rom-mib 8
```

It uses the shared adventure rules and converts the original artwork directly
to Mode 1 backgrounds and 4bpp sprites at native size. The generated cartridge
is `build/snes/deceptus-tutorial.sfc`. `--rooms 2 --rom-mib 4` builds the smaller
initial-room integration case; entering a room without converted graphics fails
explicitly. `--reuse-assets` requires matching source and converter identities.
`--assets-dir PATH` selects an isolated tutorial conversion directory. See
[SNES.md](SNES.md) for the video layout and current verification limits.

## Parallel console builds

```powershell
python tools/build_consoles.py
```

This prepares the original opening imports and common metadata once. Mega Drive
simulation metadata and target graphics are prepared in separate directories
before the workers start. The GBA, SNES and Mega Drive tutorial builders then
run concurrently; GBA validates prepared imports and Mega Drive reuses its
prepared graphics. Each converter reads the authoritative original data.
Logs are separate in `build/consoles/gba.log`, `build/consoles/snes.log` and
`build/consoles/smd.log`; `build/consoles/build.json` records each result
and successful cartridge hashes. A failed target makes the command fail even if
the other targets succeed. Pass `--targets gba`, `--targets snes` or
`--targets smd` to select one, and `--data`, `--sdk` (SNES), `--smd-sdk`,
`--clang`, `--linker`, `--objcopy` to select local inputs and tools.
Do not launch another build of these targets into the same checkout
while this command runs.

CMake exposes `console_roms` for the same parallel command and `snes_rom` for
the SNES builder. Build success does not replace the emulator checks
above. The cartridges share simulation code and use separate video/input
backends. The parallel command selects the opening tutorial. SNES and Mega Drive
opening performance remains experimental and below display rate; the smaller
standalone regressions remain available through `build_snes.py --scene prototype`
and `build_smd.py --scene prototype`.

## Packaging

`tools/package_preview.py` verifies and packages the curated four-area preview.
The current numbered tutorial builds are local intermediate snapshots containing
the ROM, controls, build hash and source content notices. They are not a retail
release process. Do not accidentally package a controlled-start test fixture
as the playable game. Build folders, downloaded SDKs, original imports and ROMs
are all excluded from version control.

## GitHub Actions

The `.github/workflows/build-gba.yml` workflow builds on Ubuntu 24.04 with LLVM,
Python 3.13 and system Lua 5.4 for the source-reference tests. It runs on pushes
to `main`, pull requests and manual **workflow_dispatch** runs.

CI checks out this repository only and uses its bundled content. The engine and
content therefore change atomically in one commit. `assets/game/SOURCE.json`
records the original source revision and copied file hashes; `SOURCE_REVISION`
is provenance, not a request to download a second repository.

To download a cartridge: open the repository's **Actions** tab, select a
successful **Build GBA** run, then download its `deceptus-gba-<commit>` artifact.
The ZIP contains `Deceptus.gba`, controls, content notices and `BUILD.json` with
both source revisions and the ROM hash. Artifacts are retained for fourteen days.
Failed runs retain available diagnostics for seven days.

CI runs native, Lua-reference and conversion checks. Local mGBA/Snes9x integration
checks remain separate; a successful build action is not a full-game or hardware
verification claim. Downloaded SDKs and generated artwork stay out of Git.
