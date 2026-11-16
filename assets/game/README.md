# Bundled Deceptus content

This directory makes the game builds independent of another engine checkout.
`data/` contains original game assets; `src/game/` contains selected original
C++ reference files read by offline converters. These C++ files are not compiled
into the portable runtime. Original paths are retained for textures and Lua imports.

Content terms are in `data/LICENSE.md` and `LICENSES/`. Bundling the content does
not change its ownership or make it part of the portable engine's MIT license.

`SOURCE.json` records the upstream revision, copied file hashes and any modified
source files at import time. Refresh explicitly with:

```powershell
python tools/import_game_content.py D:/path/to/deceptus_engine
```

Builds use this directory by default. A separate checkout is only needed when
refreshing from upstream or deliberately overriding the build's `--data` path.
