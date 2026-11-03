# psdoom-plus

A remake of [psDooM](https://psdoom.sourceforge.net), the Doom-based process manager, for current
Linux, BSD and Haiku systems and for modern Doom source ports.

In psDooM every running process is a monster. Shooting one lowers its priority (`renice`);
killing it kills the process. psdoom-plus brings that back on top of current engines:

| Engine | Status |
|---|---|
| og (id's linuxdoom-1.10 + SDL3) | **playable on Linux** |
| Chocolate Doom, Crispy Doom | planned |
| GZDoom, UZDoom | planned |
| DSDA-Doom, Woof! | planned |
| Quake, Quake 3 (stretch) | planned |

Platforms: Linux, FreeBSD, OpenBSD, NetBSD, DragonFly and Haiku, plus MS-DOS for the og engine
(remote processes over the network, or local TSRs and drivers as monsters). Anything else (containers, pods,
cloud apps) can plug in through psdoom-ng's `PSDOOM_NG_*_CMD` hooks, so kubedoom-style helpers
work unchanged.

> **Warning:** this program sends real signals to real processes. In `classic` mode
> nothing is protected: it will happily kill your shell, your desktop or itself. Try it in a
> throwaway VM first.

The **og** engine (id's own linuxdoom-1.10 on SDL3) plays on Linux today; the other engines and
platforms follow. See [PLAN.md](PLAN.md) for the design and the phases.

```sh
tools/psdev build og                       # fetch id's source, apply engines/og/patches, build
build/og/psdoom-og -nopsact                # dry run first: shows processes, signals nothing
build/og/psdoom-og -iwad freedoom1.wad     # modern mode (protected session, TERM then KILL)
build/og/psdoom-og -psclassic              # psDooM 2000: renice +5, kill -9, nothing protected
```

## Layout

| Path | What |
|---|---|
| `PLAN.md` | design, decisions and phases |
| `libpsd/` | the process library every engine links: backends, kill modes, audit log ([API](libpsd/include/psd.h)) |
| `data/` | the original psDooM custom levels `psdoom1.wad` (Doom) and `psdoom2.wad` (Doom II) |
| `tools/psdev` | developer CLI: test VMs, `test unit [--vm <os>]` |
| `engines/og/` | og: upstream pin, patch series, SDL3 platform layer, psDooM glue |
| `tests/vm/` | guest bootstrap and end-to-end tests (`tools/psdev test og --vm debian`) |
| `orig/` | the original psDooM 2000.05.03.b sources and downloads, kept for reference |

## Credits

- **Dennis Chao**: *Doom as an Interface for Process Management* (UNM, 1999), the original idea and code
- **David Koppenhofer**: psDooM (2000), built on XDoom
- **Udo Munk** and the XDoom contributors
- **id Software**: Doom (source released under the GPL in 1999)
- psdoom-ng / psdoom-ng1 and kubedoom, for keeping the idea alive

## Licence

psdoom-plus is free software under the **GNU General Public License, version 2 or (at your
option) any later version** (`GPL-2.0-or-later`), the same terms as psDooM and XDoom. See
[LICENSE](LICENSE).

Engine patch series are applied to upstream engines under those engines' own licences. A build
combined with GZDoom or UZDoom is distributed under GPL-3.0, which "or later" permits.
