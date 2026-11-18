# Original-game parity and documentation

The target is the original Deceptus game workflow on GBA, SNES, Mega Drive and
Amiga, with reusable engine services kept separate from game rules. A buildable
backend or passing scripted route does not establish feature parity. DOS and
STM32 remain later targets.

## What counts as complete

For each feature, record the original asset, object property or script behavior;
its engine service and game-owned rule; and each backend's presentation and
input behavior. Check authored timing, interruptions, room changes, restart and
failure cases as well as the successful path. Preserve the original pixel grid.
Document approximations and unsupported settings explicitly.

Verification must distinguish logical traversal, visual fidelity, real-time
performance, RAM-bank loading and physical-hardware testing. An accelerated
emulator proves neither target speed nor hardware compatibility. New harnesses
and captures stay local under `build/`; publish the source changes and the
reproducible verification commands/results, rather than throwaway fixtures.

## Feature and guide coverage

The original repo's `doc/readme.md` is the reference index. The source topics
below refer to that repo's `doc/` tree; they are comparison references, not
additional checkout dependencies for normal builds.

| Original topic | C-port guide | Current boundary / remaining work |
| --- | --- | --- |
| Building and running | [Build setup](BUILDING.md), [README](../README.md), platform guides below | Four active backends; all opening performance claims need platform-specific evidence |
| `level_design/designing_a_level.md` | [Content pipeline](CONTENT_PIPELINE.md) | Original Tiled inputs are imported; the C collision grid and bounded exports differ from desktop physics; a complete authoring walkthrough is still needed |
| `level_design/mechanisms.md`, `mechanism_editor.md`, `mechanism_lasers/readme.md` | [Porting backlog](PORTING_BACKLOG.md), [Architecture](ARCHITECTURE.md) | Selected mechanisms/settings only; full property-by-property authoring reference and editor parity remain open |
| `level_design/enemies.md`, `lua_interface/readme.md` | [Enemy coverage](SEALED_ENEMIES.md), [Projectiles](PROJECTILES.md) | Native translations of selected behaviors; arbitrary Lua APIs/scripts are not supported |
| `level_scripts/readme.md` | [Opening bindings](CONTENT_PIPELINE.md#opening-progression-bindings), [Status](STATUS.md) | Opening rules are compiled into the game layer; a full script-operation compatibility reference remains open |
| `cutscene_system/readme.md` | [Status](STATUS.md), [Porting backlog](PORTING_BACKLOG.md) | Complete action sequencing, timed camera/control locks and source cutscene fidelity remain unfinished |
| `game_physics/readme.md` | [Architecture](ARCHITECTURE.md), [Ropes](ROPE.md), [Sword](SWORD.md) | Fixed-point C services replace desktop physics; additional abilities, dynamic bodies and collision fidelity need verification |
| `level_design/visual_effects.md`, `level_design/advanced_topics.md` | [Content pipeline](CONTENT_PIPELINE.md), platform guides | Native-size artwork retained; lighting, weather, particles and shaders have gaps or bounded approximations |
| `development_hotkeys.md` | [Controls](../README.md#controls), platform guides | Game controls documented; desktop editor/debug hotkeys are not portable runtime features |
| Audio and music behavior in source code / level scripts | [Status](STATUS.md#remaining-work) | No playback yet; game chooses cue/track IDs, backends may use tracker/sequenced or streamed resources; timing must be independent of rendering |
| Saving and later-game workflow | [Status](STATUS.md), [Porting backlog](PORTING_BACKLOG.md) | Persistent storage and complete later-level progression remain unfinished |

Platform instructions: [GBA/build setup](BUILDING.md), [SNES](SNES.md),
[Mega Drive](MEGA_DRIVE.md), [Amiga](AMIGA.md).

## Documentation delivered with a feature

Update the relevant guide in the same change as the runtime or converter:

- State the purpose, original behavior, supported properties and concrete usage.
- Explain public API ownership, capacities, lifetime and failure behavior.
- Give build/run commands and input mappings, including platform differences.
- Describe content conversion, formats and memory/performance constraints.
- Record verification conditions, results and known deviations from the original.

Keep the README as the entry point, architecture as the ownership/dependency
reference, content guides as the authoring/conversion reference, and status and
backlog as the honest account of missing behavior. The table above tracks guide
coverage as well as runtime gaps; it does not claim that either has reached the
original repo's completeness.
