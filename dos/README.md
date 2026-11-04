# psdoom-plus for MS-DOS

The og engine (id's linuxdoom-1.10 with psdoom-plus's SDL3 layer and libpsd),
built with DJGPP against the SDL3 DOS backend of the
[sdl-dos-ports](../.sdl-dos-ports/README.md) hub, running under CWSDPMI.

DOS has no processes, so the monsters come from elsewhere:

| Backend | Monsters | Kill |
|---|---|---|
| `mcb` (the default on DOS) | resident programs (TSRs) and CONFIG.SYS device drivers, from the DOS memory chain | frees the program's memory blocks; unlinks a driver from the device chain |
| `net` (`-psnet IP:PORT`) | processes on a Linux/BSD/Haiku host running `psdoomd` | the host's own kill; psdoomd's mode is the floor |

With the `mcb` backend:

- Programs are Shotgun guys and device drivers are Demons. The label is the
  name DOS 4+ wrote into the memory block.
- **Classic** (`-psclassic`) frees anything you kill. If the victim had
  hooked an interrupt, the PC hangs the next time that interrupt fires.
  That is the 2000 experience.
- **Modern** (the default) refuses:
  - psdoom itself and the COMMAND.COM chain above it;
  - CWSDPMI, its DPMI host;
  - any block an interrupt vector still points into;
  - any driver DOS can still call.

  These are shown as invulnerable monsters, since a DOS box has only a
  handful of residents. `-psprotect <glob>` adds names.
- *Wounding* has no DOS equivalent, because DOS has no priorities.

With the `net` backend, PSDOOM.EXE speaks UDP to `psdoomd` through its own
small stack (`libpsd/src/dos_net.c`: packet driver, ARP, IPv4, UDP). The
usual DJGPP TCP/IP library, Watt-32, can't be linked into a GPL program.

- **On the host:** `PSDOOMD_TOKEN=<secret> psdoomd -l <its IP>:6666`. The
  host's rules apply. A modern psdoomd refuses to kill its protected
  processes, even for a client playing classic.
- **On the DOS PC:**
  - load the card's packet driver;
  - set `PSDOOM_NET_TOKEN=<secret>`;
  - give the PC an address with `PSDOOM_IP`, plus `PSDOOM_GATEWAY` off the
    local subnet, or let it take one from mTCP's `MTCPCFG` file or
    `WATTCP.CFG`;
  - run `PSDOOM -psnet <host IP>:6666`.
- There's no DNS. The token goes over the network unencrypted, so use this
  only between test machines.

## Building

You need a DJGPP cross-compiler and CMake. nasm is only needed for the test
TSRs, and DOSBox-X only for the tests. The shared scripts link the
toolchain from the `~/emulators` hub.

```sh
cd dos
../.sdl-dos-ports/shared/scripts/setup-symlinks.sh   # tools/djgpp
make sources patches   # SDL3 at the hub's pin + the SDL3-DOS patch series
make                   # build/psdoom.exe, build/psdps.exe
make cwsdpmi tests     # CWSDPMI r7 (sha256-pinned), the test TSRs
make check             # tests/dosbox-pstest.sh in DOSBox-X
make pktdrv bundle     # Crynwr NE2000.COM (tests only) and build/bundle/
../tools/psdev test dos --vm debian   # PSDOOM.EXE in DOSBox-X in a test guest kills
                                      # that guest's sleeps through psdoomd
```

`make` fetches the og engine with `tools/psdev fetch og` when needed. DOS
builds use the same `engines/og/patches/` series as every other platform;
the DOS parts are `#ifdef __DJGPP__`.

## Running

Put these in one directory:

- `PSDOOM.EXE` and `CWSDPMI.EXE`;
- `PSDOOM2.WAD` or `PSDOOM1.WAD`, psDooM's own level;
- an IWAD: `DOOM2.WAD`, `DOOM.WAD`, or Freedoom as `FREEDOOM.WAD`.

Then run:

```
PSDOOM                      modern mode
PSDOOM -psclassic           classic: everything dies, including your PC
PSDPS                       what the level will contain (a ps for DOS)
```

Useful switches:

- `-mb <n>` sets the zone size in MB (default 8).
- `-nomouse` turns the mouse off.
- `-nosound` turns sound off.
- `PSDOOM_STDERR=ERR.TXT` captures error messages, which COMMAND.COM
  cannot redirect.
- The config (`PSDOOM.CFG`), savegames, the libpsd settings (`PSD.CFG`) and
  the audit log (`PSD.LOG`) live in the current directory.

There is no Doom network play (`-net`): only psdoomd uses the network.

## Testing

`make check` runs `tests/dosbox-pstest.sh`. It loads two plain TSRs and one
that hooks INT F1h, lets `PSDOOM -pstest` shoot them in classic and then
modern mode, and checks the memory chain after each run with `PSDPS`. It
also checks that the audit log records the kills.

`tools/psdev test dos --vm <os>` runs `tests/vm/dos-net-pstest.sh` in a test
guest. DOSBox-X's NE2000, on slirp with the Crynwr driver, carries
PSDOOM.EXE's UDP to psdoomd on the guest's loopback. It checks three cases:

- modern psdoomd, modern client: the guest's `sleep`s get SIGTERM;
- modern psdoomd, classic client: psdoomd refuses an `Xorg` decoy;
- classic psdoomd, classic client: the decoy gets SIGKILL.

DOSBox-X results are correctness checks only, never performance numbers.
Real hardware runs follow the hub's `/sdldos:*` skills.

## Layout

| Path | What |
|---|---|
| `Makefile` | the hub's `shared/build/sdl3-dos.mk`, then og's CMake build with the DJGPP toolchain |
| `vendor/sources.manifest` | the SDL3 pin (the og engine is pinned in `engines/og/UPSTREAM`) |
| `patches/SDL/` | the SDL3-DOS patch series, seeded from the hub, owned here |
| `tools/psdps.c` | `PSDPS.EXE` |
| `tests/` | test TSRs and the DOSBox-X test |
| `LICENSE-REVIEW.md` | what a DOS release may contain |
