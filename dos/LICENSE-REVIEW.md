# License review: psdoom-plus (DOS)

## Engine source

License: GPL-2.0-or-later. `LICENSE.TXT` at id-Software/DOOM `a77dfb9` is the
GPL v2 text with the "any later version" option (the 1999 re-release). The
per-file headers still name the DOOM Source Code License, which the GPL
re-release supersedes.
Redistribution terms (source): GPL; psdoom-plus is GPL-2.0-or-later as a whole
(`LICENSE` at the repo root).
Redistribution terms (compiled binary): GPL; a binary release carries the
licence and points to the source (this repository, at the tag it was built
from).
Notes: libpsd and the psDooM glue are psdoom-plus's own code, GPL-2.0-or-later.

## DOS-specific patches (`engines/og/patches/`, `#ifdef __DJGPP__`)

License: GPL-2.0-or-later, as the engine.

## SDL3 + `patches/SDL/`

License: zlib (see the hub's THIRD-PARTY.md). Linked statically; the SDL licence
text ships with a binary release.

## CWSDPMI r7

Read in `vendor/cwsdpmi/cwsdpmi.doc` (from `csdpmi7b.zip`, sha256 pinned in the
Makefile). It may be redistributed without source if:

- `CWSDPMI.EXE` is unmodified;
- users are told they may get the source and binary updates;
- the documentation names a site for the source.

A release ships `CWSDPMI.EXE` with `cwsdpmi.doc` and points to
<https://sandmann.dotster.com/cwsdpmi/>.

## Game assets

- `data/psdoom1.wad` and `data/psdoom2.wad`: psDooM's own levels, from the 2000
  release. They are distributed with psdoom-plus under the same terms
  (`README.md`, "Licence").
- IWADs: never shipped. The user supplies their own `DOOM.WAD`/`DOOM2.WAD`, or
  Freedoom (BSD-3-Clause), which could be bundled but isn't.

## Conclusion

A DOS release may contain:

- `PSDOOM.EXE`, `PSDPS.EXE`, `PSDOOM1.WAD`, `PSDOOM2.WAD`;
- `CWSDPMI.EXE` with `cwsdpmi.doc`;
- the GPL text, the SDL zlib licence, and a pointer to the source.

No IWAD.
