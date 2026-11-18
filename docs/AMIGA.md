# Amiga 500 backend

The Amiga backend runs the existing one-room catacombs scene with the shared
portable C simulation. The default launcher retains this verified prototype. An experimental
26-room opening is available as a separate scene; its complete workflow and
performance are still being verified.

## Run

On Windows x64, from the repository root:

```powershell
python run_amiga.py
```

The launcher prepares a project-local Python environment when needed, downloads
and verifies the pinned 200 MB compiler/emulator bundle, builds a missing
executable and starts WinUAE. Use `--build` to rebuild or `--prepare` to prepare
everything without opening the emulator.

Cursor keys move and right Ctrl jumps using WinUAE's keyboard joystick layout.
A joystick in Amiga port 2 uses left/right and the fire button. Hold both mouse
buttons to exit and restore the operating system. A focus click keeps the game
running.

The default firmware is WinUAE's built-in AROS replacement. To use your own
Kickstart 1.3 ROM:

```powershell
python run_amiga.py --kickstart C:/path/to/kick13.rom
```

The configured machine has 512 KiB Chip RAM and a 512 KiB slow-RAM expansion.
`--slowmem 0` selects an unexpanded machine, but that configuration currently
fails to load the executable under AROS because too little memory remains.
A 512 KiB machine with original Kickstart has not been verified.

## Experimental opening

```powershell
python run_amiga.py --scene tutorial --build
```

Subsequent launches can omit `--build`. `--scene prototype` selects the original
single room. The executables and assets live separately under `build/amiga/`
and `build/amiga-opening/`.

The opening uses the shared game controller for portals, dialogue, inventory,
headtorch, ruby/altar progression and sword behavior. Hardware code loads the
requested room and presents its source frames. Up interacts, Return confirms,
Tab opens inventory, Space attacks and Esc pauses or returns from a menu.
Cursor keys and right Ctrl retain movement and jump controls. The source font
is rendered at its authored size. Audio, persistent saves and complete source
cutscene fidelity remain unfinished.

Room graphics are disk-backed files on WinUAE's mounted filesystem. This is an
AmigaDOS executable, not a bootable floppy image. Both normal and sword body
variants are prepared offline; only the currently used variant is resident.
Fixed buffers divide resources between CPU-accessible and Chip RAM, alongside
two scrolling display buffers. `build/amiga-opening/build.json` records exact
static sizes; the OS, loader and largest free block in each bank must also fit.
The prototype's measured frame rate below does not establish opening performance.
The opening title has now been
verified under cycle-exact WinUAE with the same 512 KiB Chip + 512 KiB slow RAM
configuration and built-in AROS. Its static footprint is approximately 690 KiB,
including 467 KiB of Chip RAM. A local normal-input replay now passes all 56 opening checkpoints through the
rubies, crypt return, altar and sword acquisition/attack under accelerated
68000 emulation with the same RAM budget. This verifies progression/resource
loading, not cycle-exact performance or complete source cutscene fidelity.
Action-scene frame rates remain under verification. Room loading lends the unused end of its resident sprite
buffer to the tile cache. A directory provides constant-time cache lookup;
changing rooms or body variants invalidates the lease and cache together.
Row maps decode directly into native words, avoiding an intermediate byte buffer.
A short cycle-exact moving-Cell check after this change measured 2.75 displayed
frames/s and 15 simulation ticks/s at the 1 MiB configuration. This remains far
below the target rate; the complete opening is not performance-ready. The
renderer also avoids restoring background pixels that the next opaque UI box
will cover. These changes retain fixed ownership and add no runtime heap.


## Verified prototype scope

Cycle-exact WinUAE checks used a PAL 68000 machine, OCS graphics, 512 KiB Chip
RAM, 512 KiB slow RAM and no fast RAM. A local scripted-input replay verified
movement in both directions, jumping, camera movement, sprite masking and
returning to the AROS shell after exit. No physical Amiga has been tested.

During the movement replay, 151 video frames contained 151 presentations and
181 simulation updates: nominally 50 presented frames/s and 59.93 simulation
updates/s. The simulation uses elapsed vertical blanks to preserve its 60 Hz
timing independently of presentation. Catch-up is bounded to five video frames;
the diagnostic block records any excess delay.

The first build occupies approximately 356 KiB of static memory, including
311 KiB of Chip RAM, before operating-system, stack and loader overhead.
`build/amiga/build.json` records the exact sizes for each build. These figures
and frame rates describe this single room, not the full opening or later content.

The display uses a 320x224 viewport over the 512x224 imported room. Artwork
retains its original pixel size. Five OCS bitplanes provide 32 palette slots;
conversion fits a shared RGB444 palette using perceptual Oklab distance with
a source-RGB fidelity term, without
resampling or dithering. Actor colours are weighted across the complete animation
sheet, and transparency uses its own mask instead of spending a colour slot.
The current room uses 27 background colours, compared with 15 in the first
prototype. Against the original room, RGB error fell from 10.77 to 6.07 RMSE
and Oklab error from 0.02510 to 0.01452, approximately 44% and 42% lower. Exact per-build
measurements are recorded in the asset report. Emulator captures confirmed pixel-exact background
scrolling against the converted preview, including non-word-aligned positions.

The prototype has no opening menus, audio, disk streaming or persistent saves.
The imported layers are flattened as in the existing one-room console prototype.
The synthetic patrol and portal are disabled; the original player is the only
rendered actor.

## Implementation and build

- `src/platform/amiga/` owns OCS registers, copper lists, double buffering,
  masked blitter operations, joystick input and AmigaOS takeover/restoration.
- `tools/amiga_assets.py` starts from the lossless content importer and exports
  native bitplanes and masks. Shared gameplay and the bundled source snapshot
  are unchanged.
- `tools/build_amiga.py` produces `build/amiga/deceptus-amiga.exe`, an AmigaDOS
  Hunk executable, plus its ELF, map, converted previews and memory report.
- `tools/bootstrap_amiga.py` installs the pinned SDK locally. The implementation
  uses [Bartman's Amiga toolchain bundle 1.8.2](https://github.com/BartmanAbyss/vscode-amiga-debug/releases/tag/1.8.2),
  including its GCC 15.1.0, arithmetic support, elf2hunk and WinUAE. Their notices
  remain in the downloaded bundle.

For an explicit build:

```powershell
python tools/bootstrap_amiga.py
python tools/build_amiga.py
```

CMake also exposes an optional `amiga_poc` target after SDK setup. The normal
host, GBA, SNES and Mega Drive builds do not require the Amiga toolchain.

New verification harnesses, captures and generated files remain local under
`build/amiga/`; they are not part of the source publication.
