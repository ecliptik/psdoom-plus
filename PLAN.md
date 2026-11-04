# psdoom-plus — plan

A remake of [psDooM](https://psdoom.sourceforge.net) (David Koppenhofer, 2000; based on Dennis Chao's
"Doom as an interface for process management", 1999) for current Linux, BSD and Haiku, on many
Doom source ports, with Quake and Quake 3 as stretch goals.

Repo: https://github.com/ecliptik/psdoom-plus (public, empty as of 2026-11-03).

## Decisions (owner, 2026-11-03)

| Topic | Decision |
|---|---|
| "og" target | **Vanilla linuxdoom-1.10** (via doomgeneric + SDL2) with the psDooM hooks applied |
| Multi-engine approach | **libpsd + patch sets**: one portable C library, a small per-engine patch series against a pinned upstream tag |
| Kill policy | **Two modes** (see "Kill modes"): `classic`, which is faithful to 2000 (`renice +5`, `SIGKILL`, nothing protected, can kill itself), and `modern`, a friendlier mode with protected processes and graceful signals. Audio daemons are *not* protected: killing PipeWire/PulseAudio is fair game |
| Extra targets | **Local processes only.** No built-in Docker/Podman/Kubernetes backends (dropped 2026-11-03; kubedoom already does pods well). Containers, pods and anything else plug in via the psdoom-ng-compatible `cmd` hooks |
| GZDoom family | Target **both GZDoom (4.14.x) and UZDoom** with one patch series and two `UPSTREAM` pins (added 2026-11-03) |
| DOS | **og only**, built with the sdl-dos-ports hub's SDL3-DOS + DJGPP layer, kept **inside psdoom-plus** (`dos/`, hub `shared/` via git subtree, a hub `ports.yaml` entry pointing here). Monsters are **remote host processes** (via `psdoomd` over TCP) **and local TSRs/drivers** (MCB chain) (added 2026-11-03) |
| Repo access | `pass:hosts/claude/github-release-token` (fine-grained, ecliptik/*, Contents + Workflows RW). Pushes use a one-off credential helper piping from `hpass`, so the token is never written to disk |

## What psDooM actually is (from `orig/`)

- `orig/psdoom-devpatch/` is only the `.a → .b` delta. The real code is in the 2000.05.03 source,
  now fetched to `orig/src-2000.05.03/` (and the `.a` patch to `orig/dl/`).
- Core: `xdoomsrc/xdoom/pr_process.c` (745 lines). Every N tics `pr_check()` runs
  `popen("ps h a x u OT")`, parses user/pid/tty/comm, and keeps a linked list of pid mobjs.
  New pids are spawned, vanished pids are removed (no corpse), and pids still running after their
  monster "died" are resurrected archvile-style.
- Monster type: daemon (no tty) → Demon (`MT_SERGEANT`), otherwise → Shotgun guy (`MT_SHOTGUY`).
  The label is the pid plus the last 7 characters of the command, drawn in `r_things.c`.
- Damage hook (`p_inter.c`): only the player can hurt pid monsters (`-nopssafety` lifts this).
  First damage → `system("renice +5 <pid>")`; death → `system("kill -9 <pid>")`.
- Spawning: three 1024×1024 boxes with their lower-left corners at (0,0), (2048,0) and (4096,0) in
  `psdoom1.wad`/`psdoom2.wad`. Otherwise a hard-coded courtyard on stock E1M1/MAP01. Monitoring is
  only on the first map, never during demos.
- Engine hook sites: about 60 `*** PID BEGIN ***` hunks in d_main, g_game, p_saveg, p_tick,
  p_setup, p_inter, p_mobj, r_things, plus `p_mobj.h` (`m_pid`, list links, draw/delete flags).
  XDoom is close to linuxdoom, so these port almost 1:1 to the og target.
- Flags to keep: `-nopsmon -nopsact -nopssafety -psallusers -psuser -psnotuser -nopslev
  -nomonsters. -respawnitems`; user from `PSDOOMUSER/LOGNAME/USER/USERNAME`.

**Prior art to borrow from (all GPL):**
- **psdoom-ng → psdoom-ng1** (Orson Teodoro, `orsonteodoro/psdoom-ng1`, GPL-2.0, last push
  2025-06). psDooM moved onto Chocolate Doom; now SDL2, Autotools/CMake, Linux + macOS, with a
  `psd` wrapper and a man page. Still a single engine, still shells out to `ps`/`kill`
  (procps, util-linux), classic kill semantics, and the external calls are synchronous (it
  hitches when they are slow). Hector Rivas Gandara (keymon) added the external-command hooks
  `PSDOOM_NG_PS_CMD` (prints `<user> <pid> <name> <is_daemon>` per line),
  `PSDOOM_NG_RENICE_CMD` and `PSDOOM_NG_KILL_CMD` (each gets the pid as an argument).
- **dockerdoom** (2015, dead) forked psdoom-ng for Docker. **kubedoom** (`storax/kubedoom`, last
  push 2024-08) vendors dockerdoom plus a Go helper that answers the hooks with `kubectl`.
- libpsd's `cmd` backend implements those exact variables and line format, so psdoom-ng1
  scripts and kubedoom-style helpers drive any psdoom-plus engine. It runs the calls
  asynchronously so a slow helper doesn't stall the game.

## Architecture

```
            +------------------- engine (patched) -------------------+
            |  thin glue: tick → psd_poll(), spawn/remove mobjs,      |
            |  damage → psd_wound(), death → psd_kill(), label draw   |
            +-----------------------------+---------------------------+
                                          | C ABI
            +-----------------------------v---------------------------+
            | libpsd (C99, GPL-2.0-or-later)                          |
            |  diffing (new / gone / still-alive-after-kill)          |
            |  user filters (-psuser/-psnotuser/-psallusers)          |
            |  spawnbox table + PSDBOXES lump parser                  |
            |  backends:                                              |
            |   linux (/proc)  freebsd/dragonfly (sysctl KERN_PROC)   |
            |   openbsd (sysctl KERN_PROC)  netbsd (KERN_PROC2)       |
            |   haiku (get_next_team_info, kill, set_thread_priority) |
            |   cmd (psdoom-ng PSDOOM_NG_*_CMD hooks, async)  mock    |
            |   net (remote psdoomd over TCP)  mcb (DOS TSRs/drivers) |
            +---------------------------------------------------------+
```

- **Native calls, not `popen`/`system`**: `kill(2)` and `setpriority(2)` directly. This avoids shell
  injection from odd process names and works where `ps` output differs (BSD, Haiku).
  The faithful semantics stay the same: +5 nice and SIGKILL.
- **Engine glue stays small**: each engine patch only maps libpsd events to its own spawn, damage
  and render code. All the policy lives in the library, so a rebase onto a new upstream tag touches
  only the glue.
- **Spawnboxes as data**: a text `PSDBOXES` lump (the format sketched in the original TODO), so any
  map can be a process room. Built-in fallbacks cover psdoom1/2.wad and stock E1M1/MAP01.
- **Freedoom**: its E1M1/MAP01 layouts differ from id's, so the hard-coded courtyards don't apply.
  With Freedoom we load psdoom1/2.wad, or ship a PSDBOXES lump for Freedoom maps. CI and the VMs use
  Freedoom so no commercial IWAD ever has to be copied around.
- **External sources via hooks only**: containers, pods or cloud apps come from a helper that
  speaks the psdoom-ng hook protocol (`PSDOOM_NG_PS_CMD` etc.), for example kubedoom's helper or a
  ten-line `docker ps` script. libpsd ships no container code of its own.
- **Off by default, as in the original**: in netgames (desync risk) and demo record/playback.
  Save games drop pid mobjs on save and resync them on load.

## Kill modes

Selected with `-psmode classic|modern`, or `mode =` in the config file. **Default: `modern`**, so a
fresh build run on a real desktop doesn't take the session down; `classic` is one flag away.

| | `classic` (2000) | `modern` |
|---|---|---|
| First hit | `renice +5` | nice rises with damage taken (0 → 19 as health → 0) |
| Death | `SIGKILL` | `SIGTERM`, then `SIGKILL` after a grace period (default 3 s) if the process is still alive; the monster plays its death animation in between |
| Protected processes | none: it can kill itself, its terminal, X/Wayland, sshd, init | not spawned as killable (see below) |
| PID-reuse race | ignored | Linux `pidfd_open`/`pidfd_send_signal`; elsewhere start time is re-checked before signalling |
| Process count | every process | capped (default 64), highest CPU first; the rest are queued |
| Root-owned target | killed if the game runs as root | Doom-style "are you sure? (y/n)" prompt |

**Protected in `modern`** (built-in list, extendable in config):
- psDooM itself, its parent chain (shell, terminal emulator, tmux/screen) and its own helper
  children (the external hook commands it spawns, the privileged helper)
- pid 0/1 and kernel threads (Linux `kthreadd` descendants; BSD `[idle]`, `[pagedaemon]` and
  similar; Haiku `kernel_team`)
- `sshd` and every ancestor of an ssh session (so a remote VNC/ssh session survives)
- the display stack: Xorg, Xwayland, Wayland compositors (sway, labwc, kwin_wayland,
  gnome-shell, …), the display manager, Haiku `app_server`/`input_server`/`registrar`
- the session: `systemd`, `systemd --user`, `dbus-daemon`, `login`, `getty`, `polkitd`, `seatd`

Protected processes can be shown as invulnerable "decor" monsters (`show_protected = true`) so the
room reflects the whole system, or hidden (the default).

## Modern niceties

Each item is marked **v1** (built in the main phases) or **later**.

**Gameplay ↔ system mapping**
- **v1** Monster type and health scale with load: idle → Zombieman, busy → Imp/Shotgun guy, top CPU
  → Baron, a runaway (>90% CPU) → Cyberdemon. Health is based on RSS. Daemons keep the Demon/Spectre
  look.
- **v1** Crosshair inspect: aiming at a pid monster shows pid, ppid, user, full cmdline, CPU%, RSS
  and uptime on the HUD. The floating label stays pid + name.
- **v1** "Play any map" (`-psanymap`): pid monsters replace the map's own monster spawn points, so
  any WAD or megawad becomes a process room. No PSDBOXES lump is needed.
- **v1** Filters: `-psfilter` regex on name/cmdline, `-psmincpu`, top-N, per-cgroup.
- **later** Weapon → signal (from the original TODO): fist/berserk = `SIGSTOP` (the monster
  freezes; `SIGCONT` when it "wakes"), pistol/chaingun = `SIGTERM`, shotgun = `SIGHUP`, plasma =
  `SIGKILL`, BFG = `SIGKILL` to the whole process group. The rocket launcher's splash damage hits
  neighbours, which is accepted, in keeping with the original.
- **later** Process trees: children spawn next to their parent; killing a parent in modern mode asks
  whether to take the tree.
- **later** Intermission stats: "processes killed", "CPU freed", "RAM freed".

**Safety and operations**
- **v1** Audit log of every action (time, pid, comm, user, signal, result) in
  `$XDG_STATE_HOME/psdoom/actions.log`.
- **v1** Dry run that shows what *would* be signalled (`-nopsact` plus HUD messages).
- **v1** XDG config at `$XDG_CONFIG_HOME/psdoom/psdoom.conf` (mode, protected/allow lists, filters,
  cap), shared by every engine. Each engine keeps its own config/save dir.
- **later** Privilege separation: the game runs as the user, and a tiny `psdoom-helper` (polkit on
  Linux, a `doas` rule on BSD) performs root-owned kills against the same protected list, instead of
  running the whole game as root.
- **later** systemd/rc awareness: show the unit/service name; optionally `systemctl stop` /
  `service stop` the owning unit instead of signalling the pid (otherwise the service manager
  respawns the process and the monster comes back).

**Packaging**
- **v1** `.desktop` file, man page, `psdoom` launcher script that picks an installed engine and
  IWAD (Freedoom if nothing else is found).
- **later** Distro packages (AUR, FreeBSD port, OpenBSD port, pkgsrc, HaikuPorts recipe).

## Repo layout

```
libpsd/                 C library + backends + unit tests (CMake)
engines/<name>/
    UPSTREAM            git URL + pinned tag/commit
    patches/NNNN-*.patch
    build.sh            fetch → apply → build (engine's own build system)
data/                   psdoom1.wad psdoom2.wad (from orig), PSDBOXES lumps, psdoom.pk3 (GZDoom)
quake/  quake3/         stretch targets
tests/  unit/ integration/ vm/ (per-OS bootstrap + harness)
tools/                  psdev (developer CLI: vm/fetch/build/test/rebase), patch-rebase helpers
orig/                   untouched historical sources (reference only)
docs/
```

Licence: psDooM, XDoom, linuxdoom, Chocolate, Crispy, ioquake3 and Quake are GPL-2.0(+). GZDoom/UZDoom
are GPL-3.0. libpsd is **GPL-2.0-or-later** so it links into all of them.

## Engines

| Tier | Engine | Upstream | Notes |
|---|---|---|---|
| 1 | **og**: linuxdoom-1.10 | `id-Software/DOOM` @ `a77dfb9` + own SDL3 layer (`engines/og/platform/`) | Reference port. Not doomgeneric: that is Chocolate-derived (corrected 2026-11-03). The XDoom hunks map nearly 1:1. One SDL3 layer for every platform, so DOS is not a special case |
| 1 | **Chocolate Doom** 3.1.x | chocolate-doom/chocolate-doom @ `chocolate-doom-3.1.1` | One patch (the hooks); shared glue in `engines/common/psdoom` |
| 1 | **Crispy Doom** 7.x | fabiangreffrath/crispy-doom @ `crispy-doom-7.2` | Same hooks (`tools/apply-doom-hooks.py`) plus a no-SDL2_mixer build fix; hi-res label placement |
| 1 | **GZDoom** 4.14.x and **UZDoom** | ZDoom/gzdoom, UZDoom/UZDoom | Both targeted: one patch series, two pins. C++ native bindings (list/wound/kill/inspect) + ZScript `PidMonster` actors, an EventHandler poll and HUD inspect in a shared `psdoom.pk3` |
| 2 | DSDA-Doom, Woof! | | Boom lineage; one shared approach |
| 3 | Eternity, Odamex | | Only on request |

## Platforms

| OS | Process backend | Test guest |
|---|---|---|
| Debian 13 (and Linux in general) | `/proc` | `tv-debian` 192.168.150.11 |
| FreeBSD 15.1 (DragonFly shares the code) | `sysctl KERN_PROC_PROC` / `kinfo_proc` | `tv-freebsd` .13 |
| OpenBSD 7.9 | `sysctl KERN_PROC` / `kinfo_proc` (pledge-friendly) | `tv-openbsd` .12 |
| NetBSD 11.0 | `sysctl KERN_PROC2` / `kinfo_proc2` | `tv-netbsd` .14 |
| Haiku R1/beta6 | `get_next_team_info`; `kill()`; nice → `set_thread_priority` per thread (verify) | `tv-haiku` .15 |
| macOS (bonus, untested) | `libproc` | none |
| MS-DOS 6.22 / FreeDOS (og only) | `net` (remote psdoomd) and `mcb` (local TSRs/drivers) | DOSBox-X first, then the hub's vcctrl real-hardware rigs (486/Pentium) |

All Tier-1 engines already build on these platforms through SDL2; Haiku has HaikuPorts recipes for
Chocolate, Crispy and GZDoom.

## Test environment (ops answer, 2026-11-03)

The existing clean-install **testvm** guests on jezebel are used. No new VMs need to be created.

- Control from the claude VM: `ssh -i ~/.ssh/testvm-ctl micheal@jezebel.hale-gopher.ts.net
  status|start|stop|revert tv-<os>` (one guest per call, logged). No approval or silence is needed.
- Guests: 2 vCPU / 4 GiB each, **at most two up at once**. They sit on the isolated `testnet`
  (192.168.150.0/24, NAT egress only, no LAN or tailnet). Login is `claude` (Haiku: `user`) with
  `~/.ssh/testvm_ed25519`, NOPASSWD sudo/doas.
- `revert` resets to the pristine install, so all installed deps are lost. Each OS therefore gets an
  idempotent `tests/vm/bootstrap-<os>.sh` (toolchain, SDL2, Freedoom, Xvfb/x11vnc). If
  bootstrapping gets slow, ask the owner for a "base + deps" image; that is an owner decision.
- The guests are headless, with no host VNC. Automated tests use `SDL_VIDEODRIVER=dummy`/offscreen.
  For human-visible checks, run Xvfb + x11vnc inside the guest and connect over the testnet; Haiku
  has app_server natively. Screenshots use the engine's own screenshot function.
- **Rule: armed builds (real kill/renice) run only in tv-\* guests.** On the claude VM, use the
  `mock` backend or `-nopsact` only.

### Test layers

1. **libpsd unit tests** (`tools/psdev test unit [--vm <os>]`) run on every guest. The native tests
   fork their own `sleep` children and only ever signal those, so they are safe anywhere: children
   are listed, renice gives nice=5, kill reaps them by SIGKILL, modern TERM→KILL escalates, and a
   recycled-pid identity is refused. The mock backend covers the core logic (filters, protection,
   resurrection, caps, dry run) and the cmd tests use a fake psdoom-ng helper.
2. **Engine integration**: a `-pstest N` mode in each engine glue spawns pid monsters for the
   `psdtest` user's `sleep` processes, damages and kills N of them through the normal damage path,
   writes a screenshot and exits. The harness checks the `sleep`s are gone and the screenshot exists.
   This runs once per kill mode: `classic` must kill a decoy that pretends to be sshd, and `modern`
   must spawn a protected decoy without being able to kill it, log a TERM→KILL escalation, and
   leave its own parent shell alive.
3. **Hook interop**: a fake helper script speaking the psdoom-ng protocol (including a deliberately
   slow one, to prove the game doesn't stall).
4. **Manual play**: VNC into the guest and shoot things.

CI (later): GitHub Actions Linux runs the unit tests plus mock-backend engine builds. BSD runs use
vmactions, as a second check on top of the jezebel guests.

## DOS

SDL2 is a dead end on DOS (`sdl2-compat` needs a dynamic loader; DJGPP's `dlopen` returns NULL,
see the hub's `shared/patches/sdl2-compat-notes/`). The hub's proven path is **SDL3-DOS + DJGPP +
CWSDPMI**, which is why og gets its own SDL3 backend. Chocolate/Crispy (a full SDL2→SDL3
migration including mixer/net), GZDoom/UZDoom and the Quake engines (GL, memory) are out of scope
on DOS.

**Where it lives:** `dos/` in this repo, with the hub's `shared/` layer pulled in by git subtree at
`.sdl-dos-ports/`, the hub's conventions (patch naming, DJGPP constraints, emulator → real-hardware
tiering), and a `ports.yaml` entry in the hub pointing here. That claim is made with `/sdldos:port`
when the phase starts, not before.

DOS has no processes, so the DOS build has two monster sources:

- **`net`: remote processes.** A small `psdoomd` runs on a Linux/BSD/Haiku host (built from libpsd,
  so it gets the host's native backend and kill modes) and serves a line protocol over TCP:
  `LIST` returns `<user> <pid> <name> <is_daemon>` lines (psdoom-ng's hook format) ending in
  `.`, and `RENICE <pid>`/`KILL <pid>` act on them. The DOS side uses a packet driver + Watt-32. A
  real 486 can then shoot processes on a server. `psdoomd` requires a shared token, binds only to
  an address given explicitly, and is tested only between test guests. Every other engine gets the
  `net` backend for free.
- **`mcb`: local TSRs and drivers.** Walk the DOS memory chain (INT 21h AH=52h "List of Lists" →
  first MCB, plus the UMB chain and DOS 4+ device-driver sub-blocks) through DPMI. Device drivers
  (HIMEM, EMM386, …) are Demons, resident programs are Shotgun guys, and the label is the MCB owner
  name. *Wound* has no DOS equivalent and only shows a message. *Kill* frees the block:
  - `classic`: free it regardless. If it hooked interrupts, the PC hangs on the next interrupt.
    That is the authentic experience.
  - `modern`: protect our own PSP, the parent `COMMAND.COM` and CWSDPMI (our DPMI host). Blocks
    that interrupt vectors still point into are shown as invulnerable; only blocks nothing points
    into can be freed.

**Testing:** DOSBox-X first (its NE2000 + slirp networking covers `net` against `psdoomd` in a
tv-\* guest), then the hub's vcctrl rigs on real 486/Pentium hardware, following the hub's
`/sdldos:*` skills (emulator numbers are never performance results).

## Phases

0. ✅ **Bootstrap**: git repo → `ecliptik/psdoom-plus`; import `orig/` as reference (original tarballs
   plus the reconstructed 2000.05.03.b tree); README, LICENSE, `tools/psdev` skeleton; vault project card.
1. ✅ **libpsd core** (2026-11-03): API, diff/resurrect logic ported from `pr_process.c`, Linux + mock + cmd
   (psdoom-ng env vars) backends, unit tests. *Exit:* unit tests green on tv-debian.
2. ✅ **og (id's linuxdoom-1.10)** (2026-11-03): 64-bit fixes, SDL3 video/audio, `-iwad` + Freedoom, XDG paths,
   psDooM's hooks on libpsd (`engines/og/patches/`, glue in `engines/og/psdoom/`), psdoom1/2.wad,
   `-pstest`/`-pstestview`. *Exit met:* `tools/psdev test og --vm debian`: 8/8 with real signals
   (classic SIGKILL, modern TERM→KILL, protected decoy untouched, audit log), screenshots taken.
   PSDBOXES lumps moved to the "any map" work.
3. ✅ **BSD + Haiku backends** (2026-11-03): FreeBSD (`KERN_PROC_PROC`), OpenBSD/NetBSD (`KERN_PROC` /
   `KERN_PROC2`, one file), Haiku (teams; `setpriority` is a no-op there, so renice lowers thread
   priorities; everything runs as uid 0, so no root confirmation). og patch 0006 for BSD/Haiku libc
   differences. *Exit met:* libpsd 5/5 and og 8/8 (real signals) on Debian 13, FreeBSD 15.1,
   OpenBSD 7.9, NetBSD 11.0 and Haiku R1/beta6. Guest notes: OpenBSD needs the X11 base set for
   SDL3; NetBSD has no pkgin in a minimal install (bootstrap uses `pkg_add`).
4. ✅ **Chocolate + Crispy** (2026-11-03): shared glue (`engines/common/psdoom`) with a per-engine adapter;
   hooks generated by `tools/apply-doom-hooks.py`. *Exit met:* 8/8 (real signals) for both engines on
   all five guests. Built without SDL2_mixer/SDL2_net (sound effects yes, music and netgames no).
5. **GZDoom + UZDoom**: native bindings + `psdoom.pk3`, one series rebased onto both pins. *Exit:* `-pstest` green on Debian/FreeBSD/Haiku
   at least (GZDoom on OpenBSD/NetBSD depends on GL availability in the guest).
6. **Hook interop**: the `cmd` backend proven against psdoom-ng1's documented protocol, plus example
   helpers in `contrib/` (`docker ps`, `kubectl get pods`) as samples, not supported backends.
   *Exit:* og and Chocolate drive a fake helper and a slow helper without frame hitches.
7. **DOS (og)**: the SDL3 backend under DJGPP via the hub's `shared/` layer, the `mcb` backend, then
   `psdoomd` + the `net` backend. *Exit:* in DOSBox-X, shooting a TSR frees it and shooting a remote
   monster kills a `sleep` in a tv-\* guest; then the same on a real-hardware rig.
8. **Tier 2 engines**: DSDA-Doom, Woof!.
9. **Stretch: Quake**: patch the Ironwail/QuakeSpasm/vkQuake family (one shared series) with libpsd
   builtins, plus a QuakeC mod (`psquake`) that spawns grunts/enforcers for processes and shamblers
   for daemons. Test assets: the shareware pak0 or LibreQuake.
10. **Stretch: Quake 3**: no engine patch. A native `qagame` mod for ioquake3/Quake3e (`vm_game 0`)
   links libpsd, and processes become bots named `pid:comm` (Q3 has no monsters). First hit renices,
   a frag kills. Test assets: OpenArena (free), on a custom arena with AAS.
11. **Release**: tagged source releases; optionally a kubedoom-compatible image built on
    psdoom-plus (offered upstream to kubedoom rather than as a rival); packaging (AUR, FreeBSD port, HaikuPorts recipe) as
    follow-up.

## Open items

- `gh`'s stored token on the claude VM is invalid. We use `pass:hosts/claude/github-release-token`
  instead (verified: it can see psdoom-plus with push rights). Whether the fine-grained token
  includes this repo is only proven by the first push.
- IWADs: Freedoom for tests. Should commercial IWADs (for the stock-E1M1 courtyard path) be tested
  manually on the owner's own machine?
- The claude VM and jezebel reboot tonight (2026-11-03), and any running guest will stop then.
