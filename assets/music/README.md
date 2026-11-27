# Music

The original game plays recorded music (OGG). The consoles play tracked modules instead, chosen among
the ones their authors released into the public domain or under CC0, and converted by
`tools/music_assets.py` (`tools/song_assets.py` reads the XM format).

| File | Module | Author | Source | License |
| --- | --- | --- | --- | --- |
| `drozerix_-_chemical_16.xm` | Chemical 16 | Drozerix | [The Mod Archive #177684](https://modarchive.org/index.php?request=view_by_moduleid&query=177684) | Public Domain |

Which module replaces which original track is the `TRACKS` table in `tools/music_assets.py`. Console
songs have at most four channels: the Amiga plays them on Paula's four hardware channels.
