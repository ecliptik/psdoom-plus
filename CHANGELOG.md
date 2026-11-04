# Changelog

## 0.1.1 (2026-11-04)

Fixes for Freedoom players, found while taking the README's screenshots.

- **og, Chocolate Doom:** these engines no longer crash past MAP01 with Freedoom 2. psDooM's
  `psdoom2.wad` adds one texture by shipping a whole Doom II texture list, and these vanilla-style
  engines replaced the IWAD's list with it. Freedoom 2's later maps need textures Doom II lacks,
  so MAP02 crashed (`LITE4`/`LITE96 not found`, or a segfault in og). Both engines now merge every
  TEXTURE1/TEXTURE2 lump with its own WAD's PNAMES, as Crispy Doom does. The DOS build gets the
  fix too.
- **DSDA-Doom:** Freedoom 2 now counts as Doom II (DSDA-Doom calls its mission `tc_freedoom`), so
  psDooM's MAP01 loads with it. Before, the level got no process monsters.
- **`-pstest`:** on a level that gets no process monsters, it now fails at once with the reason,
  instead of waiting forever.

## 0.1.0 (2026-11-04)

The first release of psdoom-plus, a remake of psDooM (2000): every process is a Doom
monster. Shoot one and it gets a lower priority; kill it and the process dies.

### Engines

| Engine | Upstream | Tested (real signals, in disposable VMs) |
|---|---|---|
| og: id's linuxdoom-1.10 on SDL3 | id-Software/DOOM `a77dfb9` | Linux, FreeBSD, OpenBSD, NetBSD, Haiku; MS-DOS |
| Chocolate Doom | 3.1.1 | Linux, FreeBSD, OpenBSD, NetBSD, Haiku |
| Crispy Doom | 7.2 | Linux, FreeBSD, OpenBSD, NetBSD, Haiku |
| GZDoom | 4.14.2 | Linux, FreeBSD, Haiku |
| UZDoom | 5.0.3 | Linux, FreeBSD, Haiku |
| Woof! | 16.0.0 | Linux, FreeBSD, Haiku |
| DSDA-Doom | 0.30.1 | Linux, FreeBSD, Haiku |

Each engine is a pinned upstream plus a patch series (`engines/<engine>/`), built by
`tools/psdev build <engine>`. psDooM's own levels (`psdoom1.wad`, `psdoom2.wad`) load
automatically.

### Kill modes

- **classic**, as in 2000:
  - the first hit renices +5, and a kill sends SIGKILL;
  - nothing is protected: your shell, your desktop and the game itself are fair game.
- **modern**, the default:
  - priority drops step by step as the monster loses health;
  - a kill sends SIGTERM, then SIGKILL after a grace period;
  - never killed: the game itself and its parent chain, init and service managers,
    sshd, display servers and compositors, the session, helpers, plus anything given with
    `-psprotect`;
  - it asks before killing a process owned by root;
  - at most 64 monsters, the busiest first.
- Every action goes to an audit log (`$XDG_STATE_HOME/psdoom/actions.log`).
- `-nopsact` is a dry run.

### Backends (libpsd)

- **Native process tables:** Linux `/proc`, FreeBSD, OpenBSD, NetBSD and Haiku.
- **psdoom-ng hooks (`PSDOOM_NG_*_CMD`):** run asynchronously, so a slow helper never
  stalls the game. Example helpers for Docker containers and Kubernetes pods are in
  `contrib/helpers/`.
- **net:** another machine's processes through **psdoomd**, over TCP or UDP, with a shared
  token and an explicit listen address. psdoomd's own mode is the floor: a client playing
  classic cannot kill what a modern psdoomd protects.
- **MS-DOS (mcb):** resident programs and CONFIG.SYS drivers, from the DOS memory chain.

### MS-DOS

- The og engine for MS-DOS (DJGPP, SDL3-DOS, CWSDPMI), as `psdoom-plus-0.1.0-dos.zip`.
- **Monsters:** TSRs are Shotgun guys and device drivers are Demons. A kill frees the
  program's memory.
- **Modern protection:** psdoom's own process chain, CWSDPMI, anything on an interrupt chain,
  and any driver DOS can still call.
- **Remote processes:** psdoomd over UDP, through the PSDOOM.EXE's own small IP stack on a
  packet driver.
- **Tested:** DOSBox-X, including real `sleep`s killed in a Linux VM through psdoomd. Also a
  Gateway 2000 (486DX2-66, Mach64, MS-DOS 6.22) on real hardware.

### Known limitations

- Doom network play (`-net`) is not available in the DOS build.
- The DOS remote-process test has not yet run on real hardware.
- On DOS, the fix that finds HIMEM/EMM386 in the system-data block was checked against a real
  memory dump, not run live.
- Woof! and DSDA-Doom keep pid monsters in saves and rewind key frames. Their re-adoption
  after a restore is not covered by the automated test. With a slow external helper,
  restored monsters may vanish and reappear a moment later.
- In DSDA-Doom, process labels show in the software renderer only.
- GZDoom, UZDoom, Woof! and DSDA-Doom have not been tried on OpenBSD or NetBSD.
- Quake and Quake 3 (the stretch goals) are not started.
