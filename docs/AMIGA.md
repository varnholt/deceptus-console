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
configuration and built-in AROS. Its static footprint is approximately 702 KiB,
including approximately 474 KiB of Chip RAM. A local normal-input replay now passes all 56 opening checkpoints through the
rubies, crypt return, altar and sword acquisition/attack under accelerated
68000 emulation with the same RAM budget. This verifies progression/resource
loading, not cycle-exact performance or complete source cutscene fidelity.
Action-scene frame rates remain under verification. Room loading lends the unused
ends of both resource banks to the graphics caches. A directory provides
constant-time tile lookup; changing rooms or body variants invalidates both
leases and caches together.
Row maps decode directly into native words, avoiding an intermediate byte buffer.
A cycle-exact moving-Cell check with the larger cache and direct Chip sprite
access measured 18.4 displayed frames/s and 60 simulation ticks/s over 709 PAL
frames at the 1 MiB configuration, with no additional dropped-time count during
the sample (previously 2.75 frames/s and 15 ticks/s before these caches). This remains far
below the target rate; the complete opening is not performance-ready. The
renderer also avoids restoring background pixels that the next opaque UI box
will cover. These changes retain fixed ownership and add no runtime heap.


## Walking and jumping regression

The walking-only measurement above is insufficient to establish playability.
Repeated full jumps in the same Cell on the earlier shallow-cache build measured
2.2 displayed frames/s and 12.25 simulation ticks/s, with large frame stalls.
The taller cache alone improved that sample to 15.1 displayed frames/s and
52.1 simulation ticks/s. Cached foreground restoration and temporary blitter
priority during CPU waits produced the following earlier, short Cell samples:

| Normal-input movement | Displayed frames/s | Simulation ticks/s | Presentation intervals in PAL frames |
| --- | ---: | ---: | --- |
| Alternating left/right walking | 45.8 | 59.97 | 614 at one, 26 at two, 17 at three to five |
| Walking with repeated full jumps | 41.8 | 60.02 | 696 at one, 112 at two, 26 at three to five |

Samples cover 717 and 998 PAL frames respectively, after startup, with no
additional dropped-time count in either. These are warmed Cell measurements
on the same 512 KiB Chip + 512 KiB slow-RAM machine, not all-room results.
Both still miss the steady 50 fps target. Walking and jump frame pacing remain
part of the playability gate; average simulation speed alone is insufficient.

Longer runs followed by reversals exposed a separate failure: moving the backing
window rebuilt a large strip on the CPU, causing pauses of roughly one second
even with no sprite-cache miss. The short warmed samples above missed this.

Static rooms now first try to keep their complete composited background and
foreground mask in Chip RAM. The Cell uses 220,320 bytes for these six planes;
its world/sprite archives and this image fit the existing resource bank. Two
368x232 display buffers then scroll against that source, without reconstructing
background strips or moving a third reference buffer. The 320x224 visible view,
original pixels and machine settings are unchanged.

A cycle-exact 1 MiB Cell check with 90-tick runs and repeated reversals measures
32.4 displayed frames/s and 60.01 simulation ticks/s over 994 PAL frames. Its
645 presentation intervals comprise 396 at one PAL frame, 154 at two, and 95
at three to five, with none above five and no additional dropped game time.
This removes the previous long refill freezes in that sample, but does not
establish steady 50 fps or all-room playability. Jumping, cold animation loads
and rooms that cannot retain a full image need separate timing checks.

A narrow Lower Cells (monk room) movement check measured 24.8 displayed
frames/s and 60.01 simulation ticks/s over 999 PAL frames, with no additional
dropped game time. Of 495 presentation intervals, 488 take two PAL frames and
seven take three to five. The same repeated-jump/reversal check in Crossroads
measures 15.4 displayed frames/s and 60.02 simulation ticks/s over 998 PAL frames.
Longer runs with camera scrolling subsequently reproduced a 118-PAL-frame
(2.36-second) stall. The narrow sample above does not establish playability. A corrected cache
partition retains tile capacity for scrolling rooms, but the broader rerun
still exceeds one second per frame in some cases. Further work is required.
These original-speed, 1 MiB results remain below the target; a stable simulation
clock does not establish smooth presentation. WinUAE's 50 Hz display counter
must not be reported as the game's rendered frame rate.

The `.acp` file stores five composited colour planes followed by one foreground
mask plane, with word-aligned rows. Build reports hash it alongside the original
archives and native background. Animated-clip rooms use a scrolling composite
reference whose changed clips are rebuilt in the original layer order.
When a full static image cannot fit, larger windows can instead borrow compressed
`.acr` columns from normal RAM. Each `AC01` column decodes five colour planes and
one mask, using a bounded normal-RAM staging lease. Its header records dimensions,
16-pixel columns and a checked offset table. Space-constrained rooms retain the
tile path; this fallback does not establish equivalent performance.

## Adaptive scrolling windows

The visible game viewport remains 320x224 with the original pixel grid. Larger
backing buffers reduce how often camera motion requires copying the whole
viewport; they do not widen or scale the player's view.

After resident world/sprite resources, the Chip bank can lend 199,680 bytes for
two 512x312 backing buffers, 170,240 bytes for two 448x304 buffers, or
156,000 bytes for two 400x312 buffers. Each masked candidate also reserves
one foreground bitplane and at least 16 KiB of remaining Chip storage.
If none fits, an unmasked larger pair or the static
368x232 buffers are used. Full static images take priority over these larger windows.
Selection can change with the room and normal/sword graphics variant.
These are subregions of the existing 360 KiB resource bank, not new allocations.

In a larger mode, the inactive compact buffers become one opaque background
reference (at most 99,840 bytes, within their combined 106,720-byte capacity).
It follows a shared scroll-window origin, scrolling and rebuilding only exposed
background strips from the tile archive or compressed columns. Both display buffers use that reference
for sprite/UI cleanup. A leased foreground mask (at most 19,968 bytes)
tracks static foreground and foreground clips. The reference contains the
background, background clips, foreground and foreground clips in source order.
Opaque cleanup copies that image; masked blits restore foreground occlusion
after sprites. Changed clips rebuild only their affected reference rectangles.
The mask scrolls with the reference and exposed strips rebuild both together.
Rooms without mask storage retain the original tile drawing path. Directional
rebases leave more space ahead of movement and above the camera, with vertical
origins clamped to the room extent, and coarse bitplane pointers
are derived from the actual buffer origin together with fine scrolling.

Room/body reloads invalidate display buffers, the background reference and every
cache lease together. The loader reserves viewport storage before handing the
remaining bank tails to caches. Compact mode can use the native background file
below; larger modes use their retained background reference instead.

Local checks exercise 385,312 camera moves and compare 50,000 complete rendered
frames against the original tile path across current and boundary geometries. They include
scroll directions, clipping, clip-frame transitions, old-sprite cleanup and
foreground occlusion, with full-room, compressed-column, static-mask and animated-clip paths. UI pixels
and dirty rectangles also match the previous renderer at every supported
horizontal cache offset. Complete accelerated route
checks establish logical correctness; the Cell measurement above does not prove
all-room performance, hardware compatibility or complete game readiness.

Pending blits receive priority only while the CPU waits for completion; the
previous priority bit is restored before independent CPU work resumes. This
avoids spending Chip bus slots polling a blit that must finish first. See the
[hardware reference on blitter DMA arbitration](https://amigadev.elowar.com/read/ADCD_2.1/Hardware_Manual_guide/node012B.html).
The source font resides in CPU-accessible RAM. Resource loading, archive helpers
and UI code use size optimization to leave room for AmigaDOS. Generated read-only
Chip data stays outside LTO's writable Chip section. Actual AROS loading is
checked in addition to static bank totals; larger buffer candidates that fail
to load are not usable configurations.

## Native background restoration

Each room/body variant also exports an `.abg` file containing its opaque
background as five uncompressed native bitplanes. Rows are padded to a 16-pixel
word boundary; the byte count is `ceil(width / 16) * 2 * height * 5`. This uses
the same palette indices and pixel grid as the tile archive. The converter
records its hash, and asset reuse validates the file with the other resources.

In compact mode, after loading compressed resources, the loader optionally places this
bitmap in the Chip bank's unused tail. It keeps enough space for at least one
decoded tile group in one of the banks. Rooms that cannot fit the bitmap retain
the tile renderer. Missing/truncated/oversized bitmap files fail loading when the
native path is selected, rather than silently changing presentation.

Opaque dirty rectangles and newly exposed scroll strips can then use five
word-aligned A-to-D blits with independent source/destination strides. Horizontal
restoration expands to word boundaries before all actors and UI are redrawn.
Out-of-world rectangles use the checked tile path. Animated background clips,
foreground occlusion and foreground clips retain their original drawing order;
only the opaque base copy changes. All cache leases exclude the resident bitmap.

Local checks compare every bitmap byte against independent tile reconstruction
for all 52 variants, and check 10,000 blit rectangles against a CPU reference,
including edge fallback, strides and destination guards. These checks complement
the actual emulator replay and cycle-exact measurement.

Sprites whose resource, orientation, position and layer slot are unchanged
can remain in their display buffer. Changed sprite, clip and UI rectangles
propagate damage through overlapping actors before drawing in source order.
Scrolling invalidates this reuse. Local checks compare 8,000 complete frames
against full redraws, including overlap chains, removal, reordering and clips.
Immutable sprite descriptors reuse their request slots until a room/body reload.
Aligned memory copies and fills use native long transfers in one compiled
implementation, with byte fallbacks for alignment and tails.
Camera offsets also stay within each backing buffer after quantized rebases;
this fixes bottom-edge reads beyond the buffer.

## Opening sprite resources and cache ownership

`tools/amiga_opening_assets.py` emits `AG03` world archives (`.agp`) and
compressed sprite blocks (`.asp`). Rebuild assets after a format/converter
change; `--reuse-assets` rejects stale converter identities and altered files.
The decoder rejects older archive versions rather than interpreting their
sprite descriptors with the new layout.

The 48-byte big-endian header retains room dimensions/counts and the palette,
tile, row-map, clip and sprite-table offsets. Sprite descriptors are now 28
bytes: resource ID (`u16`), normal/flip X trim and Y trim (`i16` each), width and
height (`u16` each), then normal offset/length and mirrored offset/length (`u32`
each). A zero mirrored length requests runtime fallback. Both orientations use
independent LZ4 blocks, five planar colour planes and one mask plane, with
word-aligned rows and a trailing guard word for shifted blits. Identical blocks
share storage. Dimensions, trimming, transparent padding and pixels are retained.

Conversion adds mirrored blocks in resource order while they fit. It aims to
leave 24 KiB of resource-bank headroom beyond the largest decoded sprite;
rooms whose base data already exceeds that target retain their smaller budget.
Missing offline mirrors do not omit sprites or change gameplay. The loader splits
only at complete normal or mirrored block boundaries between the 360 KiB Chip
and 88 KiB normal resource banks.

`prepareCaches` borrows the unused tails of both resource banks until the next
graphics load. Tile storage holds a directory and up to 128 decoded groups,
capped by the room's group count. Decoded sprites can use up to 64 KiB. Only when a full room composite avoids scroll-strip tile reconstruction does
the larger tail prioritize sprites if the other bank can hold the tile minimum. Otherwise the
original tile-first partition applies; unused space can enlarge the sprite lease. Regions never overlap resident resources or one
another. Neither cache allocates memory at runtime. The planar writer and strip rebuild
keep a single compiled implementation to bound executable code size as well. A bank too small for a tile
group causes a checked renderer failure; a small sprite cache remains usable
through direct decoding. These are opportunistic graphics leases, not an audio
budget: future sound/music storage must be excluded before cache partitioning.

`AmigaSpriteCache` owns only metadata; its caller owns the supplied byte buffer.
Initialize it whenever room/body-variant resources change. Source graphics must
remain immutable for that lease. Thirty-two entries identify decoded blocks by
source offset/length, dimensions, stride and orientation. A bounded ring evicts
entries overlapping a new frame. Cache misses decode and, when necessary,
mirror once. Frames in Chip cache storage feed the blitter directly; frames in
CPU-only storage copy into the existing Chip staging buffer. Oversized frames
decode directly into staging. `amigaSpriteCachePixels` returns pixels valid until
the next cache call or graphics load. The renderer waits for the previous blit
before any subsequent cache mutation or staging reuse, including the clipped
CPU path. Bank selection explicitly tracks DMA accessibility; CPU-only addresses
are never submitted to the blitter. Decode errors still propagate to the renderer.

Decoded row maps retain four ways per set. Rooms with spare cache space borrow
up to sixteen sets (64 rows); large rooms fall back to four static rows. A hash
of map offset and row selects a set, bounding each lookup to four comparisons.
Unused tile-bank space or space beyond the sprite-cache cap supplies the extra
rows, without taking away resident tiles or decoded-sprite capacity. This lease
is also invalidated on every graphics reload.

Local verification covers both orientations of every sprite in all 52 archives,
including odd widths and split-bank reads, plus cache hits, overlapping eviction,
room reset, malformed resources and guarded output. Target performance is measured
separately from accelerated full-route checks; see the opening scope above.

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
`build/`; they are not part of the source publication.
