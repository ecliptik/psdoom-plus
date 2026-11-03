# Original psDooM (reference only)

Historical sources, kept unmodified for reference while porting. Nothing here is built.

| Path | What |
|---|---|
| `dist/` | the release files exactly as downloaded from SourceForge (`psdoom` project, status "Inactive") |
| `psdoom-2000.05.03.b/` | the full source tree of the last release: the 2000.05.03 source + the `.a` patch + the `.b` patch |
| `psdoom-data/` | the custom process-management levels and their README (also copied to `../data/`) |

## Files in `dist/`

| File | Released | SHA-256 |
|---|---|---|
| `psdoom-2000.05.03-src.tar.gz` | 2000-05-04 | `24bcd05f10eec92e50119c732053bd0f431c2a69406cf5ab91fa717711fce825` |
| `psdoom-2000.05.03.a-patch.tar.gz` | 2000-05-31 | `d810204aa986d3ac429fe07aa7c14b26f79472a25f8322d0316210a27ee71be2` |
| `psdoom-2000.05.03.b-patch.tar.gz` | 2000-06-14 | `09014acfe26b6519ca6c63a5470c61f1770bb5c48accd9a11da5d7eed52e5bf3` |
| `psdoom-2000.05.03-data.tar.gz` | 2000-05-04 | `b975dab0372fa262108834711a77c8f82fa157ddd3876aaeb1afadf11af0cbf3` |

Source: <https://sourceforge.net/projects/psdoom/files/>

## Rebuilding `psdoom-2000.05.03.b/`

```sh
tar -xzf dist/psdoom-2000.05.03-src.tar.gz            # -> psdoom-src/
tar -xzf dist/psdoom-2000.05.03.a-patch.tar.gz        # -> psdoom-devpatch/ (.a)
(cd psdoom-src && patch -p1 < ../psdoom-devpatch/psdoom-2000.05.03.a-patch)
tar -xzf dist/psdoom-2000.05.03.b-patch.tar.gz        # -> psdoom-devpatch/ (.b)
(cd psdoom-src && patch -p1 < ../psdoom-devpatch/psdoom-2000.05.03.b-patch)
```

Both patches apply cleanly. The psDooM changes to XDoom are marked `*** PID BEGIN ***` /
`*** PID END ***`; the core is `xdoomsrc/xdoom/pr_process.c`.

## Licence

GPL (psDooM's `psdoomdoc/COPYING`; XDoom sources: "version 2 or any later version").
Copyright 1993-1996 id Software, 1997-2000 Udo Munk and the XDoom contributors,
1999 Dennis Chao, 2000 David Koppenhofer.
