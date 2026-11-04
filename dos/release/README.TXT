psdoom-plus @VERSION@ for MS-DOS
==============================

psDooM (2000) made every Unix process a Doom monster: shoot one and it
gets a lower priority, kill it and the process dies. psdoom-plus brings
that back, and on DOS, where there are no processes, the monsters are
what is resident in memory: TSR programs and CONFIG.SYS device drivers.
Or, through the network, the processes of a Linux, BSD or Haiku machine.

    WARNING: killing a monster really frees that program's memory. In
    classic mode nothing is protected, and freeing a program that hooked
    an interrupt hangs the PC the next time that interrupt fires.


What you need
-------------
- A 486 or better with VGA and DOS 4 or later (program names in memory
  come from DOS 4+). Tested on MS-DOS 6.22 on a 486DX2-66 with 48 MB
  (Mach64, Sound Blaster) and in DOSBox-X. The game reserves 8 MB for
  its zone; -mb N asks for less on smaller machines.
  Sound Blaster compatible sound is optional (BLASTER variable).
- An IWAD: DOOM2.WAD (Doom II) or DOOM.WAD, or Freedoom (copy
  freedoom2.wad as FREEDOOM.WAD; free from https://freedoom.github.io).
  None is included.

Put the IWAD next to PSDOOM.EXE and run:

    PSDOOM                      modern mode (the safe one, see below)
    PSDOOM -psclassic           psDooM 2000: everything dies, your PC too
    PSDOOM -iwad FREEDOOM.WAD   pick the IWAD
    PSDPS                       what the level will contain


The monsters
------------
- Resident programs (TSRs) are Shotgun guys, CONFIG.SYS device drivers
  are Demons. The floating label is the name DOS gave the memory block.
- Hitting a monster would lower the program's priority; DOS has none, so
  nothing happens. Killing it frees the program's memory (classic: any;
  a driver is unlinked from the DOS device chain instead).
- Modern mode (the default) never kills, and shows as invulnerable:
  psdoom itself and the COMMAND.COM above it, CWSDPMI, anything an
  interrupt vector points into, any program further down an interrupt
  chain, and any driver DOS can still call. Plain TSRs remain fair game.
  -psprotect NAME protects more names.
- PSDOOM2.WAD (Doom II) and PSDOOM1.WAD (Doom) are psDooM's own levels,
  loaded automatically; -nopslev plays the normal first map instead.


Remote processes
----------------
A Linux, BSD or Haiku machine running psdoomd (from the source, below)
serves its processes; its own rules decide what may be killed:

    on that machine:  PSDOOMD_TOKEN=some-secret psdoomd -l 192.168.1.10:6666
    on the DOS PC:    load the network card's packet driver, then
                      SET PSDOOM_NET_TOKEN=some-secret
                      SET PSDOOM_IP=192.168.1.20   (or use mTCP's MTCPCFG)
                      PSDOOM -psnet 192.168.1.10:6666

There is no DNS, and the token crosses the network in clear text: use
this between test machines only. Doom's own network play (-net) is not
available in the DOS build.


Options and files
-----------------
-mb N            zone memory in MB (default 8)
-640             640x480 instead of 320x200 (some capture devices need it)
-nosound         no sound
-nomouse         no mouse
-nopsmon         plain Doom, no monsters from memory
-nopsact         dry run: show and log, free nothing
PSDOOM_STDERR=F  write error messages to file F

PSDOOM.CFG (settings), DOOMSAV?.DSG (saves), PSD.CFG (psdoom options,
e.g. "mode = classic") and PSD.LOG (every kill, with time and result)
are kept in the current directory.


Licences and source
-------------------
psdoom-plus is free software under the GNU General Public License,
version 2 or (at your option) any later version: see COPYING.TXT. It is
built from id Software's Doom source (GPL since 1999) and SDL 3 (zlib
licence, SDL.TXT). You can get the complete source of this release from

    https://github.com/ecliptik/psdoom-plus/tree/v@VERSION@

CWSDPMI.EXE is Charles W Sandmann's DPMI host (CWSDPMI.DOC has its
terms). You have the right to receive its source code and binary
updates: https://sandmann.dotster.com/cwsdpmi/

psDooM: David Koppenhofer (2000), after Dennis Chao's "Doom as an
Interface for Process Management" (1999).
