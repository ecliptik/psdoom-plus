# Quake 3: processes as bots

Quake 3 has no monsters, so in psdoom-plus every process is a **bot** named `pid:command`.
Quake 3's own crosshair names and scoreboard work as the labels. Shooting a process bot renices
the process, and fragging it kills the process. The rules are the same as in the Doom engines:
classic and modern modes, the protected list, the audit log and psDooM's options.

| Process | Bot (from OpenArena's `bots.txt`) |
|---|---|
| user process | Grunt (`ps_bot_user`) |
| daemon (no terminal; psDooM's Demons) | Gargoyle (`ps_bot_daemon`) |
| protected (modern mode, `-psshowprotected`) | Penguin (`ps_bot_protected`); it can't be hurt |

The game is [OpenArena](https://openarena.ws) 0.8.8, a free Quake 3 game, on the stock
[ioquake3](https://ioquake3.org) engine. There's no engine patch: psdoom-plus builds OpenArena's
game logic (`qagame`, from [OpenArena/gamecode](https://github.com/OpenArena/gamecode) at the
`oa-0.8.8` branch, GPL-2.0-or-later) with libpsd linked in, and the engine loads it as a native
mod (`fs_game psdoom`, `vm_game 0`). OpenArena's own client game and menus are used unchanged.

## Running it

You need ioquake3 and OpenArena 0.8.8's data (on Debian: `apt install openarena`).

```sh
tools/psdev build q3                        # fetch OpenArena's game code, patch, build
build/q3/psdoom-q3 -nopsact                 # dry run: bots for your processes, nothing signalled
build/q3/psdoom-q3                          # modern mode, on oa_minia
build/q3/psdoom-q3 -psclassic +map aggressor
build/q3/psdoom-q3 -dedicated -psuser www   # a server: players frag www's processes
```

`psdoom-q3` passes psDooM's options to the game as the `ps_args` cvar, and everything from the
first `+` on to ioquake3. It finds the engine and the data in the usual places; set
`PSQ3_ENGINE`, `PSQ3_BASEPATH` (the directory with `baseoa/`) or `PSQ3_HOME` (default
`~/.psdoom-q3`) if yours are elsewhere.

| Cvar | Default | |
|---|---|---|
| `ps_args` | | psDooM's options, e.g. `-psclassic -psuser me` |
| `ps_bots` | 12 | at most this many process bots (also capped by `sv_maxclients`, 24 by default) |
| `ps_skill` | 2 | their bot skill, 1 to 5 |
| `ps_bot_user`, `ps_bot_daemon`, `ps_bot_protected` | Grunt, Gargoyle, Penguin | which bots play them |

## Rules

- Only human players can hurt a process bot. Other bots, lava and falling damage can't; to
  allow them, use `-nopssafety`.
- A fragged process bot stays down until libpsd answers. If the process is gone, the bot leaves
  the game; if it survives (protected, or it ignored the signal), the bot respawns.
- A root process (when you play as root) needs a second frag within 30 seconds; psDooM asked
  y/n instead.
- Process bots play every map that has bot navigation (`.aas`), in any game type. They leave at
  a map change and join again on the next map.

## Tests

`tools/psdev test q3 --vm debian` runs the same real-signal test as the Doom engines
(`tests/vm/doom-pstest.sh`) against ioquake3's dedicated server in a test guest.
