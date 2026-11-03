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
| Extra targets | **Containers (Docker/Podman)** and **Kubernetes pods**, alongside local processes |
| GZDoom family | Target **both GZDoom (4.14.x) and UZDoom** with one patch series and two `UPSTREAM` pins (added 2026-11-03) |
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

**Prior art to borrow from (all GPL):** psdoom-ng / psdoom-ng1 (Chocolate-based; added the
`PSDOOMPSCMD`/`PSDOOMRENICECMD`/`PSDOOMKILLCMD` external-command hooks, now stale on SDL1) and
kubedoom/dockerdoom (pods/containers through those hooks). We keep env-var compatibility with
psdoom-ng so existing kubedoom-style setups work.

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
            |   cmd (psdoom-ng env vars)  docker/podman  k8s  mock    |
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
- **Containers / k8s** (backend selected with `-psbackend docker|podman|k8s`): a container or pod
  maps to a monster. Wound → `docker update --cpu-shares` down (k8s: annotate only). Kill →
  `docker kill` / `kubectl delete pod`. These run through the CLI with argv exec, no shell.
  Monster type: privileged or system namespace → Demon, otherwise Shotgun guy.
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
  children (the `docker`/`kubectl` CLI it spawns, the privileged helper)
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
- **v1** Filters: `-psfilter` regex on name/cmdline, `-psmincpu`, top-N, per-container/cgroup.
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
tools/                  psd (top-level CLI: build/test/rebase), patch-rebase helpers
orig/                   untouched historical sources (reference only)
docs/
```

Licence: psDooM, XDoom, linuxdoom, Chocolate, Crispy, ioquake3 and Quake are GPL-2.0(+). GZDoom/UZDoom
are GPL-3.0. libpsd is **GPL-2.0-or-later** so it links into all of them.

## Engines

| Tier | Engine | Upstream | Notes |
|---|---|---|---|
| 1 | **og**: linuxdoom-1.10 | doomgeneric (SDL2 backend) | Reference port. The XDoom hunks map nearly 1:1, so this is the first one done |
| 1 | **Chocolate Doom** 3.1.x | chocolate-doom/chocolate-doom | Use the psdoom-ng1 hook sites as a map |
| 1 | **Crispy Doom** 7.x | fabiangreffrath/crispy-doom | Shares most of Chocolate's code, so it is mostly the same series |
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

1. **libpsd unit tests** run on every guest. They fork sacrificial children under a dedicated
   `psdtest` user, then assert that the children are listed, that renice gives nice=+5, that kill
   gives the child reaped, that filters work, and that the diff logic is correct. The mock backend
   covers the logic on any host.
2. **Engine integration**: a `-pstest N` mode in each engine glue spawns pid monsters for the
   `psdtest` user's `sleep` processes, damages and kills N of them through the normal damage path,
   writes a screenshot and exits. The harness checks the `sleep`s are gone and the screenshot exists.
   This runs once per kill mode: `classic` must kill a decoy that pretends to be sshd, and `modern`
   must spawn a protected decoy without being able to kill it, log a TERM→KILL escalation, and
   leave its own parent shell alive.
3. **Container/k8s**: Docker + k3s inside `tv-debian` (after a revert), using throwaway `busybox`
   containers/pods.
4. **Manual play**: VNC into the guest and shoot things.

CI (later): GitHub Actions Linux runs the unit tests plus mock-backend engine builds. BSD runs use
vmactions, as a second check on top of the jezebel guests.

## Phases

0. **Bootstrap**: git repo → `ecliptik/psdoom-plus`; import `orig/` as reference (original tarballs
   plus the reconstructed 2000.05.03.b tree); README, LICENSE, `tools/psd` skeleton; vault project card.
1. **libpsd core**: API, diff/resurrect logic ported from `pr_process.c`, Linux + mock + cmd
   (psdoom-ng env vars) backends, unit tests. *Exit:* unit tests green on tv-debian.
2. **og (linuxdoom via doomgeneric)**: port the ~60 hunks onto libpsd, SDL2, PSDBOXES, psdoom1/2.wad,
   `-pstest`. *Exit:* on tv-debian, shooting a `sleep` monster kills the process (screenshot taken).
3. **BSD + Haiku backends**: freebsd/openbsd/netbsd/haiku. *Exit:* unit tests + og `-pstest` green on all
   five guests.
4. **Chocolate + Crispy**: shared patch series. *Exit:* `-pstest` green on all five guests.
5. **GZDoom + UZDoom**: native bindings + `psdoom.pk3`, one series rebased onto both pins. *Exit:* `-pstest` green on Debian/FreeBSD/Haiku
   at least (GZDoom on OpenBSD/NetBSD depends on GL availability in the guest).
6. **Containers + k8s backends**. *Exit:* kill a busybox container and a k3s pod from og and Chocolate.
7. **Tier 2 engines**: DSDA-Doom, Woof!.
8. **Stretch: Quake**: patch the Ironwail/QuakeSpasm/vkQuake family (one shared series) with libpsd
   builtins, plus a QuakeC mod (`psquake`) that spawns grunts/enforcers for processes and shamblers
   for daemons. Test assets: the shareware pak0 or LibreQuake.
9. **Stretch: Quake 3**: no engine patch. A native `qagame` mod for ioquake3/Quake3e (`vm_game 0`)
   links libpsd, and processes become bots named `pid:comm` (Q3 has no monsters). First hit renices,
   a frag kills. Test assets: OpenArena (free), on a custom arena with AAS.
10. **Release**: tagged source releases; packaging (AUR, FreeBSD port, HaikuPorts recipe) as
    follow-up.

## Open items

- `gh`'s stored token on the claude VM is invalid. We use `pass:hosts/claude/github-release-token`
  instead (verified: it can see psdoom-plus with push rights). Whether the fine-grained token
  includes this repo is only proven by the first push.
- IWADs: Freedoom for tests. Should commercial IWADs (for the stock-E1M1 courtyard path) be tested
  manually on the owner's own machine?
- The claude VM and jezebel reboot tonight (2026-11-03), and any running guest will stop then.
