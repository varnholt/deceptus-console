# Project conventions

- `assets/game/` contains the versioned Deceptus source-content snapshot for standalone builds. Preserve original artwork and notices. Use `tools/import_game_content.py` to refresh from an original engine checkout and record file hashes/provenance; converted target outputs remain ignored. A neighboring checkout is optional via `--data`.
- Preserve the artwork's original size and pixel grid at 1:1. Do not shrink or resample it to widen the view. Improve framing and scrolling instead. Console palette conversion is a separate constraint.
- Keep the lossless content-import stage separate from target palette/tile conversion. A future GBA build must read the original import, never a colour-reduced SNES image. Do not enable dithering without a deliberate visual review.
- Keep shared runtime and gameplay in portable C90. PVSnesLib's 65816 compiler has 16-bit `int` and `long`, and a 32-bit `long long`; use the types in `include/deceptus/types.h`.
- Follow the language-specific naming and formatting rules below. Do not apply Python naming rules to C.
- Keep platform SDK headers and registers inside `src/platform/`. Shared code must build on a desktop compiler without a console SDK.
- Preserve the original engine's identity when porting: use recognizable mechanism names and source enemy-script basenames, retain source object ids and property meanings, and keep a direct original-to-C file map. Prefer faithful C translations of existing concepts over new generic abstractions. Document necessary console-specific differences.
- Keep reusable collision, entity allocation, event queues and mechanism services in `src/engine/`, Deceptus rules in `src/game/`, and build-time conversions in `tools/`.
- Story progression, item requirements, enemy behaviors, quest flags, cutscene sequences and equipment rules belong in the game layer or imported game content. Platform backends implement hardware and presentation; they must not decide progression. The engine library must build and link without the game library.
- Use caller-owned memory, fixed-capacity pools and bounded event/render queues. Do not add heap allocation or floating point to frame-time runtime code.
- For constrained targets, verify executable loading with the actual OS and RAM-bank configuration. Static byte totals alone do not prove that the required contiguous blocks fit.
- Positions are signed 12.4 fixed point in room coordinates. Velocities must stay below one 8px collision cell per tick. Larger worlds must use room/chunk origins, not overflow local coordinates.
- Treat logical frames and palette roles as game assets. Encode SNES/GBA/Mega Drive tile, palette, DMA and sprite details in converters and backends.
- GBA, SNES, Mega Drive and Amiga are active parity targets. Implement the original opening workflow in the shared game layer and verify each backend independently. DOS and STM32 are later targets. Preserve the working Amiga prototype until opening support is verified; do not report a prototype or slow but logically correct replay as complete-game readiness.
- Verify changes with native engine/converter tests and the actual ROM integration routes: `tools/gba_emulator.py` for GBA and `tools/emulator.py` for SNES. Shared changes must preserve both.
- Keep unsupported mechanisms and Lua constructs explicit in content reports and build errors. A passing prototype build does not establish full-game support. Compare compiled callbacks against original Lua execution where possible.
- Feature and documentation parity with the original repo are explicit goals. Update the relevant authoring, API, build/control and platform-limit guides with each feature; use `docs/PARITY.md` to track missing behavior and guide coverage. Distinguish logical traversal, visual fidelity, target performance and hardware verification.
- Preserve ownership and dependency notices. Do not add attribution trailers to commits.

## C naming and formatting

- Struct, enum and typedef names use CamelCase: `DeceptusEngine`, `DeceptusAnimation`.
- Functions use camelCase, including namespaced APIs: `deceptusInit()`, `deceptusAnimationAdvanceTimed()`. Private helpers follow the same rule.
- Variables, parameters and struct fields use snake_case: `player_health`, `velocity_x`. Macros use UPPER_SNAKE_CASE.
- Preserve required C library, compiler-runtime and platform SDK names, such as `uint16_t`, `memcpy`, `__aeabi_memcpy` and SDK entry points. These are external contracts.
- Keep existing filenames and original-to-C content mappings. Do not rename original source assets or third-party code for style.
- Match Deceptus C/C++ formatting: three-space indentation and Allman braces, with braces on all branches and loops. Run `clang-format` with this repository's `.clang-format` on changed C code. Keep the code portable C90.
- Use lowercase explanatory comments except for proper nouns, acronyms and code references. Explain intent and constraints rather than narrating statements.

## Shared clean-code practices

- Inspect neighboring implementations and existing utilities before writing code. Match the established module's conventions; resolve contradictory instructions against the user's stated conventions.
- Give functions one clear purpose. Extract a helper when it makes intent clearer, not merely to hide a few lines. Keep public interfaces small and use meaningful domain names.
- Separate engine behavior, game-specific content, asset conversion and platform integration. Keep artwork, layout and timing choices in assets/configuration where appropriate.
- Prefer straightforward control flow and explicit ownership. Avoid speculative frameworks, unnecessary indirection, duplicated logic and hidden mutable state.
- Reuse established parsers, easing functions and graphics helpers. Validate external input at the boundary and report useful errors at an appropriate boundary rather than silently swallowing failures.
- Comments explain intent, constraints and non-obvious decisions. Do not narrate obvious code or add boilerplate documentation to trivial helpers.
- Preserve behavior during structural changes. Update callers, generators and tests together, and run checks appropriate to the affected platforms.

## Python

- Follow PEP 8: CapWords classes, snake_case functions/methods/variables and UPPER_SNAKE_CASE constants. Use four-space indentation.
- Follow the existing module's organization and imports. Keep Python readable and idiomatic; C/C++ naming and formatting rules do not apply to Python.
- Python bindings and generated C/C++ text must use the actual exported C/C++ symbol names, even when those differ from Python naming conventions.


## Local verification artifacts

- Newly created unit tests, integration checks, throwaway harnesses, test-only fixtures, recordings and build outputs are local verification artifacts by default. Do not commit or push them unless the user explicitly asks to version them.
- Keep local checks outside tracked source paths where practical, and stage publication files explicitly rather than adding all untracked files.
- Maintain existing versioned tests when an API change requires it, but do not treat that as permission to publish new throwaway checks.
