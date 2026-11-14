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

The final quadrant-backed stationary fixtures measured 5.06 simulation updates
per second in The Loop and 4.51 in The Crypt under Snes9x 1.63. Each sample used
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

Baseline stationary measurements of the quadrant-backed renderer in Snes9x
used 3,000 display frames of warmup and 1,200 sampled frames. The Loop advanced
102 simulation ticks (5.11 updates/s); the Crypt advanced 90 (4.51 updates/s),
with no renderer errors or deaths. OBJ preparation dominated the Crypt sample.
The opening is functional, but this throughput is substantially below a playable
console frame rate and needs further work. These measurements are specific to
those views and do not establish a whole-game performance guarantee.

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

The scanline validation and OAM packing routine now runs as a small 65816
backend function. Shared simulation remains portable C90. The C writer remains
a reference: an actual Snes9x test ROM compares 2,048 calls, including clipped
coordinates, mixed 8/16-pixel sizes, saturated scanlines and full OAM. It checks
every OAM byte, both scanline-counter arrays, object count and overflow result.
Run `python tools/verify_snes_sprites.py` after preparing the SNES assets.

With the same 3,000-frame warmup and 1,200-frame sample, the Loop advances 137
ticks (6.86 updates/s, previously 5.11); the Crypt advances 125 (6.26 updates/s,
previously 4.51). These are approximately 34% and 39% gains for those scenes,
respectively. This remains substantially below a playable console frame rate.
