# Architecture

The stack every port targets, top to bottom:

```
game data
      |
game engine (per-port, e.g. NXEngine-evo for doskutsu)
      |
SDL3 / SDL3_mixer / SDL3_image
      |
SDL3 DOS backend  <- patches/SDL/ (port's own vendored copy, seeded from shared/patches/sdl3-dos/)
      |
DJGPP + CWSDPMI
      |
MS-DOS 6.22
```

## The cooperative-scheduler model

There is no preemptive multitasking underneath any of this -- DOS has no
threads in the modern sense, and the SDL3 DOS backend's audio, timing, and
I/O all interleave cooperatively: a subsystem callback that doesn't yield
back promptly can monopolize the scheduler and starve everything else
sharing it, not just its own subsystem. Confirmed directly in upstream's
own backend documentation
([README-dos](https://wiki.libsdl.org/SDL3/README-dos)): the mechanism is
a `setjmp`/`longjmp` mini-scheduler that switches only at explicit yield
points (`SDL_Delay`, event pump calls) and never preempts mid-instruction
-- `SDL_Delay(0)` is the documented way to yield without actually
sleeping, worth knowing as a tool independent of the starvation hazard
below. This has a real, load-bearing
consequence for validation: **whether a cooperative-scheduler starvation
bug actually manifests is gated by CPU speed, not by which physical
machine it is.** A callback that reliably yields in time on a fast CPU can
fail to on a slow one running the identical code, on the identical
motherboard/chipset, with only the CPU swapped -- see `audio.md`'s
architecture-wide hazards for the concrete confirmed case (a starvation
bug that reproduced 100% on a 486DX2-66 and never on a faster Pentium
OverDrive on the same board). **Validate any cooperative-scheduler-
sensitive code on the slowest target tier a port claims to support, not
just the fastest/reference machine** -- a fast box can fail to exhibit a
real starvation bug at all, which reads as "fixed" rather than
"untested at the tier where it matters."

`shared/` in this repo owns everything from the SDL3 DOS backend down, plus
the build/test/QA tooling that operates at that layer. A port repo owns
everything from the game engine up, plus the DOS-specific patches that are
genuinely specific to that engine (renderer flip paths, format decoders,
scripting VMs — not video-mode or audio-hardware plumbing).

## What we own vs. what we inherit

| Layer | Source | Patched here? |
|---|---|---|
| Game data | port repo, user-supplied, never committed | no |
| Game engine | port repo, vendored + pinned SHA | port repo's own `patches/<engine>/` |
| SDL3 / SDL3_mixer / SDL3_image | upstream `libsdl-org`, pinned SHA | port's own vendored `patches/SDL/`, `patches/SDL_mixer/` (seeded once from `shared/patches/sdl3-dos/`, `shared/patches/sdl3-mixer/` at scaffold time -- see `docs/patch-conventions.md`) |
| DJGPP / CWSDPMI | toolchain, not vendored | not patched |
| MS-DOS | target OS | not patched |

## Interrupt handlers and DPMI locking

Anything an interrupt handler touches has to be locked in memory, because
CWSDPMI can page. When physical memory runs out it swaps to
`c:\cwsdpmi.swp`, and its own doc lists a page fault in a hardware
interrupt as fatal ("lock all pages!"). **Found 2026-09-23 while porting
dosags' MIDI fix:** the series' per-function locks,
`DOS_LockCode(func, func_End)`, never covered the Sound Blaster handler.
Under DJGPP gcc 12 -O2 the `_End` markers are laid out *before* their
functions (handler -64 bytes, ring-copy helper -3504), and CWSDPMI still
returned success for the wrapped lengths. Correct ranges wouldn't have
been enough anyway: the handler calls `memcpy`, `__udivdi3`, a sibling
mixer and a port callback. `shared/patches/sdl3-dos/0145` fixes it by
locking the whole code image, `[start, etext)` from the linker, once
before any handler is installed. It also logs any per-function range
of length zero or less, as a permanent witness. The cost is that about
1.5 MB of code is never swappable. The bug has never been seen failing;
it only matters when CWSDPMI actually pages, as on a low-RAM machine or
a big data set. **Rules for new ISR code in any port:**
- don't trust a `func` / `func_End` range; function order is the
  compiler's choice;
- lock the data the handler reads (including flags tested on the hot
  path) at install time, not first use;
- keep the handler to integer work and port I/O: no malloc, printf,
  FPU, `uclock()` or SDL calls. This is `0144`'s contract, and doskutsu's
  `0072` works within it too.


### An ISR that re-enables interrupts must mask its own IRQ first

The DJGPP libc wrappers behind `DOS_HookInterrupt` and
`DOS_HookInterruptReplace` do not let a handler re-enter itself.
- `_go32_dpmi_chain_protected_mode_interrupt_vector` (the chain variant,
  used by the SB IRQ) copies a code template, `_wrapper_intcommon` in
  libc's `gopint.o`, into a locked buffer.
- `_go32_dpmi_allocate_iret_wrapper` (the replace variant, used by the
  keyboard and OPL-PIT handlers) copies the same template.

Read from the disassembly of the toolchain's `libc.a`, the template:
1. increments a counter;
2. checks a per-wrapper BUSY flag;
3. if the flag is clear, sets it, switches to the wrapper's own stack,
   calls the handler, then clears the flag;
4. if the flag is already set -- this wrapper is still running -- does
   NOT call the handler at all.

It then ends either way:
- the chain variant far-jumps to the PREVIOUS vector (`_wrapper_intchain`);
- the replace variant just `iret`s (`_wrapper_intiret`).

So if a handler executes `sti` part-way through, and its own IRQ fires
again before it returns, that nested entry never reaches the handler:
- with the chain variant it goes to the previous handler (often the
  BIOS or DOS default);
- with the replace variant it is dropped, with no EOI from us.

Either way the interrupt is lost to the port.

**Rule:** an ISR that re-enables interrupts mid-handler must first mask
its OWN IRQ line at the PIC, and unmask it again with interrupts off
before returning. Other IRQs can then nest safely, and its own line
cannot re-enter a wrapper that would send it elsewhere. No hub ISR
re-enables interrupts today: the SB, OPL-PIT and keyboard handlers all
run with IF=0 throughout. The rule is for any port or shared handler that
adds an `sti`. Source: dosags' review of its SDL 0226 v1 -> v2, which
does this. Its v2 lab witness is still running, so this records the rule,
not a measured result.

## Seams that matter

Each seam is a place ports have historically hit friction — see the
per-topic docs for detail:

- **Video** (`video.md`): resolution/color-depth assumptions, scaling,
  framebuffer presentation path.
- **Audio** (`audio.md`): PCM vs. MIDI, hardware backend selection, decode
  cost vs. render cost trade-offs.
- **Input** (`input.md`): keyboard/mouse/gameport joystick, no modern
  controller abstraction needed.
- **Filesystem** (`filesystem.md`): 8.3 names, drive letters, no `$HOME`/
  `%APPDATA%`/XDG.
- **DOS scripting** (`dos-scripting.md`): `COMMAND.COM` is not `cmd.exe`
  -- no escape character, redirection parsed inside comments, a 256-byte
  environment block, and other landmines for any port's install/launcher/
  test BATs.
- **Timing** (`timing.md`): decoupling simulation rate from render rate.
- **Optimization** (`optimization.md`): what's actually expensive on
  486/Pentium-class hardware, and in what order to chase it.
- **Testing** (`testing.md`): DOSBox-X/86Box automation vs. real hardware.

Do not design a new port's architecture from scratch — start from this
stack and this seam list, and only diverge where the specific game's engine
genuinely requires it.
