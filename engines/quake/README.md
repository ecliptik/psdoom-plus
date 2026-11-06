# Quake: processes as monsters

In psdoom-plus's Quake every process is a monster in the level:

| Process | Monster | |
|---|---|---|
| user process | Grunt (`ps_monster_user`, default `monster_army`) | psDooM's zombiemen |
| daemon (no terminal) | Fiend (`ps_monster_daemon`, `monster_demon1`) | psDooM's Demons |
| protected (modern mode, `-psshowprotected`) | Zombie (`ps_monster_protected`, `monster_zombie`) | it can't be hurt |

Look at one and `pid:command` shows in the middle of the screen. Hurting it renices the
process, and killing it kills the process. The rules are the same as in the Doom engines:
classic and modern modes, the protected list, the audit log and psDooM's options.

The engine is [QuakeSpasm](https://quakespasm.sourceforge.net) 0.97.0 (GPL-2.0-or-later) with a
small patch: five hooks for `engines/quake/psdoom/sv_psd.c`. **No QuakeC changes:** the monsters
come from the game's own spawn functions, and the QuakeC interpreter reports every call to
`T_Damage` and `Killed`. It works with id's `progs.dat`, shareware or registered, and with
mods that keep those functions.

## Running it

You need Quake's data: `id1/pak0.pak` from the shareware release (`quake106.zip`, freely
redistributable) or your registered Quake. `psdoom-quake` looks for it next to itself, in
`~/.quakespasm`, `~/quake`, `/usr/share/games/quake`, `/usr/local/share/quake` and Haiku's data
folders; set `PSQUAKE_BASEDIR` (the directory holding `id1/`) otherwise.

```sh
tools/psdev build quake                    # fetch QuakeSpasm, patch, build (SDL2 + OpenGL)
build/quake/psdoom-quake -nopsact          # dry run: monsters for your processes, nothing signalled
build/quake/psdoom-quake -psclassic        # psDooM 2000: renice +5, kill -9
```

Shareware Quake ignores `+commands` on the command line (id's rule, which QuakeSpasm keeps), so
start a game from the menu or type `map e1m1` in the console. With registered Quake,
`psdoom-quake` starts E1M1 directly.

| Cvar | Default | |
|---|---|---|
| `ps_monsters` | 16 | at most this many process monsters on a map |
| `ps_monster_user`, `ps_monster_daemon`, `ps_monster_protected` | `monster_army`, `monster_demon1`, `monster_zombie` | which monsters play them (from the next map on) |

## Rules

- Process monsters appear near the level's own monsters and player starts, on every map
  except in deathmatch (where QuakeC removes all monsters). They don't count towards the
  level's monster total.
- Only players can hurt a process monster. Other monsters, lava and slime can't; to allow
  them, use `-nopssafety`.
- A killed process monster leaves its corpse. If the process survives (protected, or it
  ignored the signal), a new monster appears for it.
- A root process (when you play as root) has to be killed twice within 30 seconds; psDooM asked
  y/n instead.

## Tests

`tools/psdev test quake --vm <os>` runs the same real-signal test as the Doom engines
(`tests/vm/doom-pstest.sh`) against a one-slot dedicated server with the shareware data (on
the `start` map, since shareware ignores `+map`). Tested 8/8 on Debian, FreeBSD, OpenBSD and
NetBSD. The data comes from `~/.cache/psdoom-plus/quake/id1/pak0.pak` (`PSQUAKE_DATA`).
