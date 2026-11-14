# Remaining port work

Source inventory checked on 14 November 2026 against the three levels in
`deceptus_engine/data/config/levels.json`: catacombs, intro and graveyard. They
contain 979 objects, 145 room rectangles and 66 enemy placements. Unconfigured
test maps are excluded. Counts below describe source content, not verified
complete support for every placement or variant.

## Mechanisms

Native components now cover subsets of collapsing platforms (44 placements),
on/off blocks (30), spike blocks (12), sensors (12), blocking rectangles (6),
checkpoints (6), levers (6), damage rectangles (3), destructible barriers (2),
doors (1) and grab ropes (1). Wind has 15 placements but only a subset of its
force/presentation behavior is ported. Selected water, dialogue, pickup, shader
and lighting features also exist; these do not establish family-wide parity.

| Remaining source content | Count | Main dependency |
| --- | ---: | --- |
| Moving platforms | 4 | Source polylines, cosine timing, passenger carry and crush handling |
| Moveable objects / box colliders / enemy walls | 2 / 3 / 6 | Bounded dynamic bodies, pushing and collision-category rules |
| Button rectangles | 3 | Contact state, linked actions and original script callbacks |
| Crushers / spike balls / bubble cubes | 3 / 7 / 3 | Swept moving hazards, analytic or constrained motion and damage |
| Gateways / level transitions | 1 / 2 | Complete room/level graph, progression conditions and persistence |
| Dialogues / extras / treasure chests | 41 / 8 / 3 | Complete source interactions, callbacks and inventory transactions |
| Water surfaces / emitters | 9 / 14 | Source links, disturbance events and water gameplay beyond baked presentation |
| Sound emitters | 4 | Spatial looping, gain curves and playback ownership |
| Ropes with light / static lights / shader quads | 31 / 295 / 49 | Target-specific lighting/effects; selected approximations are insufficient |
| Dust / fireflies / smoke / weather | 37 / 26 / 32 / 6 | Bounded effect pools, weather state and source parameters |
| Interaction help / controller help / info overlays | 38 / 2 / 1 | Source action prompts, input mapping and reusable menu/UI state |

Object counts omit tile mechanisms. Catacombs additionally has **8 interval-spike,
8 trap-spike and 49 toggle-spike nonempty cells**. These are cells, not 65
independent mechanism instances; preserve the source grouping and timing when
importing them. No configured placements were found for conveyors, fans,
lasers, rotating blades, ordinary ropes, water-damage rectangles, skill gates,
zoom rectangles or death blocks. Their source classes still require deliberate
future support rather than successful empty backends.

The ten still-unported placed enemy scripts account for 23 placements. Existing
families also retain explicit limitations, including source blob variants and
three orb placements with no intersecting source contour. See
[Sealed Cells](SEALED_ENEMIES.md). Full-game persistence must handle 145 source
rooms; the current bounded opening room cache is not that implementation.

## Reusable menus and audio

The source already separates `MusicPlayer` transition logic from `MusicBackend`,
and `Audio` sample ownership from `AudioBackend`. Preserve those names and
responsibilities in C90, replacing allocation/threads with caller-owned state
and bounded commands. Source music supports immediate change, finish-current,
crossfade, fade-then-new, looping and playlists. `SoundRotation` uses playback
generation handles to avoid acting on a recycled voice. Preserve that rule.

Source menus include the main/pause/options/audio/control/file screens and the
in-game Map, Inventory and Archives pages. Extract navigation and named actions
from the current adventure menu switch without moving platform drawing or SDK
code into shared runtime. Audio settings need master/music/SFX gain and updates
to active playback, not silent controls.

Measured with `ffprobe`: all source audio is 44.1kHz Vorbis. Music has four files
(three tracks plus one-second silence), **10,826,780 bytes** and 633.77 seconds;
all are stereo. Sounds have **115 files, 11,387,379 bytes**, 453.07 seconds;
112 are stereo and three mono. The longest ambience files are about a minute.

| Music asset | Bytes | Duration |
| --- | ---: | ---: |
| `level_test_track_muffler_awakening.ogg` | 3,243,641 | 198.87s |
| `level_test_track_muffler_ancestors.ogg` | 4,503,973 | 241.89s |
| `menu_test_track_muffler_callisto.ogg` | 3,075,051 | 192.00s |

The source selects Callisto for menus, Awakening by default in catacombs and
Ancestors in the sewers music zone, including checkpoint-based restoration.
Original recordings are not tracker/module sources; a successful graphics ROM
does not establish a music decoder or console arrangement.

Existing engine events already identify jump, hurt, door, checkpoint, item,
lever, handle insertion, dialogue, step and land. Bind source sound assets to
those first: `player_jump_liftoff`, `player_jump_land`, the stone/grass sets in
`config/footsteps.json`, `mechanism_switch_on/off` and handle insertion. Add
explicit attack, impact, death, pause and menu actions where events are missing.
Source pause/resume sounds and five in-game menu sounds already have direct
bindings in `GameAudio` and `InGameMenuAudio`.

Keep lossless source audio separate from target conversion. Plan GBA timed PCM
mixing/DMA independently of the VBlank graphics transfers. SNES can use the
installed PVSnesLib BRR/soundbank interfaces; reserve and measure sound-driver
memory and music/effect voices explicitly. Mega Drive still needs its own real
backend. Shared commands must not encode hardware channels or falsely report
unsupported playback as successful.

## Suggested implementation order

1. Short source SFX and menu navigation/volume services; verify real emulator
   audio output and bound voice/command overflow. Preserve source credits.
2. The 65 spike cells, four moving platforms, buttons and collision categories;
   verify carrying, underside contact and damage against source placements.
3. Moving boxes, crushers, spike balls and bubble cubes using shared fixed-point
   geometry, while retaining each original mechanism's own state machine.
4. Complete interaction/transition callbacks and compact persistent state keyed
   by source ids, then expand the room graph beyond the opening.
5. Music zones, ambience and remaining weather/light/effect families, with
   measured target budgets and reviewed adaptations. Offline loudness envelopes
   can preserve the source wind/leaf coupling without runtime floating point.

Evidence: original mechanism deserializer constants and configured TMX files;
`src/game/audio/`, `src/game/ingamemenu/`, `src/menus/`, `playeraudio.cpp`,
`level-catacombs/level.lua`, `config/footsteps.json`; current portable headers and
backend code. Repeat native, GBA and SNES integration checks for shared changes.
