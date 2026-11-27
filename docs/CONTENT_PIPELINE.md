# Authoritative content pipeline

`assets/game/` contains the tracked source-content snapshot. Artwork, fonts,
maps, scripts and selected original C++ reference files are bundled so this
repository builds independently. The converters still read original source
pixels, not previously reduced target artwork. Generated target resources in
`assets/source/` and `assets/generated/` remain ignored.

To adopt content from an original checkout, run:

```powershell
python tools/import_game_content.py D:/path/to/deceptus_engine
```

The importer copies tracked game data, license notices and the C++ reference
files used to validate authored behavior. It records the original revision,
locally modified source paths and SHA-256 hashes in `assets/game/SOURCE.json`.
Review the copied changes together with converter changes before committing.
Both repositories remain independent after the copy; synchronization is explicit.
The optional `--data` argument still permits working directly from another
checkout. Source filenames and relative `data/` paths are preserved.

## Conversion stages

1. `content.py` catalogs original Tiled placements, properties, paths, source ids,
   hashes, reachable Lua modules and referenced host API calls.
2. `tiled.py` and `assets.py` render selected source layers losslessly, rasterize
   collision vectors and import native player animation frames. Nested offsets,
   flip flags and supported compressed map formats are handled at this stage.
3. `world_export.py`, `enemies.py` and mechanism/logic importers emit compact C
   definitions for supported behaviors. Unsupported content remains in reports
   or causes explicit export failures where required.
4. `gba_assets.py` fits target palette banks and packs tiles/objects. The SNES
   converter reads its own lossless import. No reduced SNES image feeds the GBA.
5. `campaign_assets.py` assembles room tables; `tutorial_assets.py` selects the
   opening's thirteen source rooms and binds supported interactions and portals.
6. Platform build scripts compile the shared C runtime, backend and generated
   resources into the cartridge image.

Room selection and narrative bindings are still curated code; importing Tiled
objects is not automatic support for the whole original level script. The
historical `tonight` scene uses a separate curated four-area route and goals.

## Artwork, layers and fonts

Preserve source sizes at 1:1. Background palette fitting is target-specific and
uses no dithering by default. Sprites retain opaque source RGB555 colours within
the supported palette reservations. Fidelity measurements are written to each
room's `budget.json`; background conversion does not guarantee every original
RGB value survives the console palette limit.

The tutorial separates foreground layers using source z values relative to the
player. Both `level_solid_onesided` and `level_soild_onesided_dec` are visual
layers as well as supplying one-way floor data; omitting their artwork leaves
only fragments of wooden platforms. Opaque water-body artwork is baked behind
the player. Ambient foam is excluded; the source player splash is an entry-only
sprite effect, independently of the baked waves.
Full original water compositing/stencil behavior remains unfinished.

`psd_layers.py` reads original PSD layer channels with their declared lengths.
`tutorial_ui.py` builds dialogue windows, page prompts, menus and interaction
cues from those layers and original sprite definitions.

`pixel_font.py` reads ASCII glyphs from the original `fonts/deceptum.png` atlas.
The native size is 13px, matching the source engine configuration. The original
13px TTF supplies advances; it is not rerasterized at 12px/24px to create the
visible glyphs. Larger text uses integer nearest-neighbour duplication. Current
bitmap coverage is ASCII; unsupported characters fail explicitly. Dialogue
wraps using the same advances as rendering, with at most two lines per page.

## Water and conditional scenery

`water.py` runs the source spring/neighbor equations offline using the original
surface dimensions, properties, emitters and gradient. Current clips have twelve
frames at six simulation ticks per frame. Random emitter timings become a
seeded periodic schedule so the clip loops deterministically.

`scenery.py` builds conditional source image/extra-animation patches: key,
locker contents, lever handle and the monk's current idle pose. Original frame
durations are compiled into bounded playback tables. Flags select patches;
picking up an item does not require maintaining a second hand-edited image.

Basic swimming now uses the original atmosphere tiles and full source player
frames. Falling water entry triggers the original splash once. The swimming
frames carry the underwater tint on every target: the converter moves their
colours a quarter of the way to the water colour (`tools/water_tint.py`) in
place of the source's semi-transparent water layers, which the consoles cannot
blend. The source art itself is not modified. Bubbles, complete
water effects and original cutscene actor choreography remain unfinished.

## Reports and limits

Useful outputs include `build/content/summary.json`,
`assets/generated/gba/budget.json`, per-area `budget.json`/`animations.json`,
`assets/generated/gba/tutorial.json`, and `build/gba/build.json`. The full-game
strict check deliberately fails while unsupported systems remain. A successful
normal build verifies a selected supported subset, not complete source coverage.

Never commit `assets/source/`, `assets/generated/`, downloaded tools or binary
build outputs. Preserve the original content ownership/dependency notices when
sharing a generated ROM.

## Original animated tiles

`tileanimation.py` reads Tiled animation frame ids and durations directly from
the original tilesets. The blue shrine column uses its original animation.
Background and foreground clips have separate pattern residency and alpha rules.
The ambient water-foam layer is deliberately excluded; entry splashes use the
original player animation instead.

Some source animations run faster than 60Hz. Frames with no display tick are
sampled out using cumulative timing; they never become zero-duration runtime
frames that reset a clip. Original source frame indices/durations remain in the
generated report. Visible pixels are copied from the source frames, never drawn
by a replacement effect. The existing spring-based water-surface bake is kept.

Normal ground blobs are imported from the original placement and `blob_2.lua`
constants. Jump/drop properties are explicitly rejected for this native subset.
The current shared GBA resource library has 638 frames, including full swim
poses, the blob's original idle, movement and death artwork, entry splashes and
native sword poses and attacks.

## Opening progression bindings

`tools/tutorial_progression.py` maps the additional opening rooms and the
Scriptorium drawer, solar seal, headtorch chest, graveyard owl, sinkhole and
catacombs sword cage to their source object identities. The catacombs and
graveyard keep separate object namespaces. Persistent flags retain the taken
rubies after the shrine consumes them. Source validation rejects changes to the
translated item rules, cage ids or shrine release delay instead of silently
building a different puzzle.

Conditional scenery uses the original chest, drawer, sword and owl images.
Graveyard background strips are baked at one static phase at their native size;
parallax, storm visuals, ghosts, audio and the sinkhole cutscene remain separate
unsupported work. The native sinkhole keeps its source delay and opens the
crypt passage. The headtorch is enabled immediately on acquisition.

For interaction and portal development after a full tutorial conversion:

```powershell
python tools/build_gba.py --scene tutorial --reuse-room-assets
```

Reuse is explicit and fails unless each room recipe, original map/artwork/script
inputs, image-library versions and converter sources match the SHA256 identity
recorded during a successful full conversion. Missing room outputs also fail.
A change to source content, room graphics or conversion code requires another
full conversion. Runtime interaction/portal metadata and UI are rebuilt every
time. Do not edit source data while a conversion or reuse run is in progress.

## Sword ward shader

`ringshader.py` translates the powered, untouched `ring.frag` equations offline,
using the original repeated and bilinearly sampled `grainy.png` noise texture.
The authored object 1339 center, ring scale, Gaussian double heartbeat and
heartbeat-driven turbulence produce a 128×128 clip at 1:1 pixels, sampled into
24 frames over the original two-second heartbeat. The clip occupies the fourth
animation slot in the Altar room and preserves original foreground occlusion.
A derived renderer flag follows the existing sword-cage state, so the visible
ward remains through the 27-tick insertion delay and disappears on release.

This is a bounded console approximation: the finite animation loop resets on
room entry, the foreground alpha threshold and target palette replace shader
blending, and the faint outer glow is cropped. The visible band is not cropped
or resampled, and dithering remains disabled. Player-dependent dents/recoil,
hit flashes and the expanding 1.2-second power-down dissolve remain unsupported.
An unreviewed fragment or heartbeat-equation change fails conversion explicitly.

## Headtorch attachment and light sprite

`headtorch_assets.py` reads original player eye positions and animation origins
for every displayed normal, swim and sword frame. The helmet uses its original
24×24 player-atlas rectangles at 1:1 size, follows the head's authored bob and
uses the original left/right lamp attachment points. Missing eye metadata or
unreviewed source attachment equations fail conversion.

The light effect is deliberately bounded for console sprite memory: the source
spotlight mask becomes a 64×32 sprite with 16 warm intensity levels using the
authored 255/200/100 color. This reduction applies only to the requested light
effect, with no dithering; world and character art keep their native pixel grid.
It does not reproduce source shadow casting, dust, landing tilt/jitter or
physical illumination of background palettes. Original headtorch configuration,
light textures and source C++ are included in the room reuse identity.

The Mines iron door retains source `item_required=red`. Native inventory item 7
represents that requirement without consuming the key; its page uses the source
generic key icon and the label “Red Key” because the upstream inventory atlas
has no separate `red` description. It is not granted as a progression shortcut.

## Grab rope artwork

`rope_assets.py` keeps the original rope's 7px width and single knot. The knot
uses the first 16 source rows; native 8px plain-strand slices and 1–8px tail
slices cover the remaining length exactly. The source engine stretches its
141px texture over the chain; this console adaptation tiles untouched plain
strand pixels instead, so the artwork is neither resized nor resampled. Each
slice receives only transparent 16×16 rotation padding. The backend can rotate
those tiles without scaling and render them directly from the bounded chain
point query, preserving the gameplay render queue.

The hanging player uses the source `player_jump_midair_r/l` animation: three
72×48 frames per side, origin 36/48 and cumulative 60Hz timing 4/4/3 ticks. The
existing armed midair cycle remains the sword equivalent. Helmet attachments
use the same original eye-position samples while hanging.
