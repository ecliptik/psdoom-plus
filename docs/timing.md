# Timing

Never assume display FPS and simulation/game timing must be identical.
doskutsu demonstrated why this matters: on slow real hardware, render rate
varies a lot (see `HARDWARE.md`'s baseline table), but game logic must
continue at its intended rate regardless.

## Determine, per port

```
simulation tick
animation tick
audio tick
render tick
input polling rate
```

Where possible:

```
simulation = fixed timestep
rendering  = as fast as practical
```

This prevents slow rendering from slowing gameplay — a candidate does not
need to hit 50/60 FPS to be considered playable if its simulation timing is
cleanly separated from its render rate.

## Your clock can lie

DJGPP's `uclock()` — the only timebase behind `SDL_GetTicks`/`SDL_Delay`/
`SDL_GetPerformanceCounter` on this backend — reads PIT channel 0. Any
code that reprograms PIT ch0 for its own purposes (a PIT-driven audio pump
is the confirmed real case — see the "PIT-driven audio pump" hazard in
`docs/audio.md`'s architecture-wide section) can freeze or corrupt this
timebase while active, silently
invalidating any performance measurement taken during that window — this
produced a false ~42% fps-cost finding for a feature that, measured
correctly via flip-counts against wall-clock `time()` instead, cost
nothing. Before trusting a timing measurement that looks surprising,
check whether anything active during that window reprograms a PIT channel
DJGPP's clock depends on.

A related but distinct `uclock()` hazard: it is **not monotonic** on this
platform — real-hardware logs have shown the unsigned-subtraction wraparound
that produces when two `uclock()` reads are subtracted in the wrong order
(a duration coming out near `2^32`, i.e. ~1.8×10^19 in a wider integer
type, instead of the small positive number expected). Clamp any
`uclock()`-derived duration to a sane range before trusting it, and treat
a single wildly-anomalous timing sample as a suspect measurement to
discard, not as a finding to explain.

**`gettimeofday()` has no sub-second resolution of its own, so a period
measured with it collapses onto a few discrete values.** DJGPP's
`gettimeofday()` fills `tv_usec` from DOS's own clock (INT 21h AH=2Ch,
hour:minute:second:hundredths -- the `AH=0x2C` call is right there in
`gettimeo.o` inside the toolchain's `libc.a`), and DJGPP's libc reference
says so outright: "precise to less than 1/20 of a second only ... the
underlying DOS function has 1/20 second granularity, as it is calculated
from the 55 ms timer tick count". DOS advances its hundredths by an uneven
mix of steps per 18.2065 Hz tick (100 does not divide by 18.2065), so a
genuinely steady period measured this way comes out as whichever whole
number of ticks elapsed during that frame's phase against the tick
boundary, rounded to a hundredth. dossage/Passage saw exactly this
(`docs/PACER-TIMING-INVESTIGATION.md` there, resolved 2026-09-03): a
deadline pacer converging on a true ~66.67 ms period against
`SDL_GetTicksNS()`, measured per frame with `gettimeofday()`, produced
`fps_p50`/`fps_p95` of exactly 16.67/9.09 on every CPU and card, and every
one of 256 raw samples checked was exactly 50, 60, 100, 110 or 330 ms --
reproduced in complete isolation by a standalone probe with no SDL,
audio, or rendering, so it is hardware-independent by construction.
Never use `gettimeofday()`, or anything built on it such as an engine's
own portable "current time" wrapper, for sub-second frame-timing
instrumentation on this platform; use `SDL_GetTicksNS()` / `uclock()`,
the same clock family the pacer runs on. This is a second, independently
confirmed root cause behind the "single clock family end-to-end" rule in
the pacer-patterns section below.

**A busy-wait fix can starve the BIOS timer interrupt, and the fps report
will lie about it in the flattering direction.** dossage/Passage's
frame-limiter investigation (see `docs/optimization.md`) tried replacing a
`delay(1)` call with a tight busy-wait poll. On real hardware this
starved the BIOS timer interrupt chain -- `time(NULL)` lost 54% of its
ticks during the run. Because Passage's own fps report divides frame
count by `time(NULL)`, **the reported number went UP (14.3) as real
performance collapsed (~6.5 actual, confirmed via vcctrl's independent
external wall-clock bracket)**. The general rule this is a concrete case
study for: **a metric computed from a clock the change under test can
perturb will tend to fail in the flattering direction, not an obviously-
wrong one** -- the busy-wait didn't just make the number noisy, it made a
badly broken run *look healthy*. Only an external witness (a measurement
that doesn't share the compromised clock) can catch this; nothing
DOS-side could have, since every DOS-side clock was corrupted by the same
busy-wait. This is precisely why this hub mandates independent/
real-hardware verification over trusting an engine's own self-report --
see `docs/video.md`'s triage-order note for the same discipline applied
to a different failure class.

**A corollary that cost real rig time before it was caught: an
external-witness check has to bracket the same interval the engine is
actually measuring, or it manufactures false failures.** The validity
check built for the above ("wall-clock and the engine's own game-time
should agree within a few seconds") was itself mis-specified -- it
silently assumed zero startup time, but a 486 spends ~14-16s on DOS init,
CWSDPMI, PicoGUS, VESA mode-set, and asset loading before the engine's
own timer even starts. That gap made the check fire on a genuinely
healthy run. The harness applied the check correctly; the check's own
interval definition was wrong. Before trusting an external-witness check
as evidence of a regression, confirm its two endpoints bracket the exact
same span the in-engine measurement covers -- an unaccounted-for
startup/teardown window on one side of the comparison but not the other
produces a confident, wrong verdict in either direction.

## PIT channel 0 on DJGPP: what `uclock()` does to it, and a latch race

Source: dosags-worker's disassembly of DJGPP libc's `uclock.o`
(2026-09-24), re-checked in the hub against the same toolchain's
`i586-pc-msdosdjgpp/lib/libc.a`.

- **The first call reprograms ch0 to MODE 2.** On its first call,
  `uclock()` writes `0x34` to port 0x43, then `0xFF` twice to port 0x40:
  channel 0, lo/hi access, MODE 2 (rate generator), count 0xFFFF. It then
  tries to wait for the BIOS tick at 0x46C to change, to align with it --
  but only while `__dpmi_yield()` succeeds (see "The first call and IF=0"
  below). From then on, ch0 counts DOWN by 1 per 1.193182 MHz clock (838
  ns per count). The BIOS's MODE 3 counts by 2. IRQ 0 still fires at
  18.2 Hz. Code that reads ch0 and assumes MODE 3 misreads elapsed time
  by a factor of two.
  The hub's SB audio IRQ timer (SDL/0039, PIT mode) converts with 838 ns
  per count, which is right for MODE 2; its comment still says "mode 3".
- **Every read is unprotected.** Each `uclock()` call reads the BIOS
  tick, sends latch command `0x00` to port 0x43, reads port 0x40 twice
  (lo, then hi) and re-reads the BIOS tick. It retries only if the tick
  changed. There is no `cli` around the three port operations.
- **The race.** The 8254 has one latch and one lo/hi flip-flop per
  channel, shared by everything that reads it. An ISR that also latches
  and reads ch0 can fire between `uclock()`'s latch and its two reads.
  - A latch command is ignored while a latch is still pending, so the ISR
    can read `uclock()`'s latched value.
  - `uclock()` can then read the live counter, with the flip-flop out of
    step.
  - Rare, wild values on both sides. The BIOS-tick check does not catch
    it, because the tick has not changed.
  In the hub that other reader is the SB audio IRQ timer in PIT mode,
  which only 486-class CPUs use: without RDTSC they fall back to the
  PIT. Pentium-class runs time that ISR with RDTSC and don't touch ch0
  there.
  **Status (dosags lab/journeys be452219, the H35-TSC test): FINDING.**
  `isr_max` outliers under the PIT fallback are a PIT-read artefact, not
  real ISR time.
  - Under RDTSC (n=2 cells), the maximum ISR time equals the derived peak
    drain (6903 us measured vs 7003 us derived, -1.4%), and no reading
    reached 10 ms.
  - On the PIT, 43 of 46 cells had readings >= 10 ms (22-54 ms).
  The test did NOT separate the two possible mechanisms:
  - a single-delta wrap or reload misread (see the next bullet);
  - this latch race, which remains a candidate mechanism.
  **Practical rule:** on 486-class builds, which time the ISR with the
  PIT, never judge a single `isr_max` reading. Derive the bound instead,
  as burst x per-write cost (the most entries one IRQ handled, times the
  cost of one write).
- **A single PIT delta wraps at 54.9 ms** (65536 counts). Time a wait
  longer than that by summing steps that are each shorter than 54.9 ms,
  or use a clock that folds in the BIOS tick (`uclock()` itself does).

**Recommended pattern** (no shared patch implements it yet):
- One IRQ-safe PIT read routine, used by every piece of port or shared
  code that reads ch0: interrupts off around the three port operations
  (`pushf; cli; out 0x43,0; in 0x40; in 0x40; popf`), and around nothing
  more. While IF=0, IRQ 0 ticks are held back, so the window stays at
  those three or four port operations, never a longer section. The
  routine is needed in mainline code, and inside an ISR that has
  re-enabled interrupts: dosags' SDL 0226 v2 does `sti` during its MIDI
  drain, and any such handler must use it too. Only an ISR that has NOT
  re-enabled interrupts is already safe.
- `uclock()` itself is libc and cannot be patched. Where an ISR also
  latches ch0, the robust choice is to keep ch0 reads out of that ISR
  (RDTSC where present, or count IRQs).
- If you must wrap `uclock()` in `pushf; cli; ... popf` instead, make
  one warm-up `uclock()` call with interrupts ON before any cli-wrapped
  call. Keep the wrap to that single call, because each read already
  spans several port operations and holds IRQ 0 back while it runs.
- **The first call and IF=0: the hang needs a narrow host.** The first
  call's tick wait (libc.a `uclock.o`, re-checked in the hub 2026-09-25)
  is `do { errno = 0; __dpmi_yield(); } while (errno == 0 && tick
  unchanged)`. `__dpmi_yield()` (`2f_1680.o`) simulates real-mode INT 2Fh
  AX=1680h and sets `errno` to ENOSYS when AL comes back 80h, i.e. when
  nothing answered. So the first call only waits on the tick while the
  yield is answered, and it only hangs with IF=0 on a host that answers
  1680h AND does not let IRQ 0 advance 0040:006C during the yield.
  Measured in DOSBox-X 2025.02.01 with CWSDPMI (hub probe YLD2,
  2026-09-25; three confs: the hub's parity conf, the same with
  `dos idle api=false`, and a --sb-live SB16/OPL3 fixed-40k conf):
  - `dos idle api` at its default (all hub and dosags confs): 1680h is
    answered (AL=00), and with IF=0 in protected mode the tick still
    advanced during the FIRST yield call, so IRQ 0 is serviced somewhere
    inside the answered call and the loop ends on the tick. The
    real-mode flags the host hands back carry IF=0 (the image DJGPP
    passed in), so they don't show where IRQ 0 got in.
  - `dos idle api=false`: 1680h is not answered (AL=80), `errno` is set,
    and the loop ends on its first pass. With IF=0, 4000 unanswered
    yields did not advance the tick: an unanswered reflection does not
    service IRQ 0.
  - In all three confs a first `uclock()` called with IF=0 returned. In
    DOSBox-X the IF=0 first call is safe either way, for two different
    reasons. That is also why dosags' TPIT-H (cli-wrapped first call,
    DOSBox-X, default conf) exited 0.
  - Plain MS-DOS + CWSDPMI on the 486: expected to be the unanswered
    case (nothing there should answer 1680h), so the loop would end on
    `errno`. **Unverified on real hardware.**
  - The hang is real only on a host that answers 1680h without letting
    IRQ 0 in. Whether a Win9x DOS box or a multitasker behaves that way
    is untested; no hang has been observed anywhere.
  So a TPIT-style hang control (first `uclock()` under `cli`, expected
  to hang) has no power in DOSBox-X, and by the above likely none on
  plain DOS + CWSDPMI: it passes whether or not the code under test is
  safe. Keep the warm-up call with interrupts ON anyway; it costs
  nothing and covers the hosts where the hang is real. (The separate
  RDTSC path, taken only when `_os_trueversion` is 0x532, i.e. an NT DOS
  box, spins on the tick with no yield at all, and would hang with IF=0.)
- Never reprogram ch0 with a mode or count that `uclock()` doesn't
  expect (see "Your clock can lie" above).

## An engine's portable clock can cost 1.3 ms PER READ, and its sleep a whole 55 ms tick

The `gettimeofday()` finding above is about *resolution*. This one is
about *cost*, and it applies to any C++ engine that keeps time with
`std::chrono` -- on DJGPP, `std::chrono::steady_clock`/
`high_resolution_clock::now()` lands in `gettimeofday()`. Measured on
HW-486-66 by a standalone probe, 10,000 iterations each (dosags,
2026-09-17; `tests/harness/results/clock-probe/` and the "40/40 plan"
P1 item 1 in its `PLAN.md`):

| primitive | per call |
|---|---|
| `uclock()` (control) | 4.9 us |
| engine `Clock::now()` (std::chrono) | **1324.6 us** |
| `gettimeofday()` underneath it | 1318.8 us |
| `mktime()` inside that -- the largest part | 723.7 us |
| one `__dpmi_int(0x21)` (two per call) | 171.2 us |
| `__dpmi_int(0x33)` mouse poll, for scale | 81.9 us |
| `std::this_thread::sleep_for(0)` | 0.4 us |
| `sleep_for(1 ms / 10 ms / 25 ms)` | **~54.8 ms each** |

Consequences, all seen for real in AGS on the DX2-66:

- **The cost is mostly `mktime()`'s calendar arithmetic, not the DPMI
  real-mode switches.** "It is just a DOS call" undersells it by 4x.
- **A handful of innocent clock reads per tick is a large slice of a
  25 ms budget.** AGS read its clock ~5 times per logic tick on an idle
  screen (three in its end-of-tick bookkeeping, one each in the
  fixed-timestep driver and the frame wait): ~6.3 ms per tick of pure
  overhead, predicted from call counts and matching the measured phase
  costs to within 4-18%. Two of those reads were the
  port's own once-a-second samplers polling the clock every tick to see
  whether a second had passed -- count ticks instead.
- **The signature to look for:** a timed phase whose cost is suspiciously
  constant (min = max to the millisecond) across thousands of calls while
  doing no real work. Count `now()` call sites on the hot path and
  multiply before hunting anything more exotic.
- **The mouse poll was checked and ruled out** (0.25 ms per tick) -- a
  per-tick `INT 33h` is cheap next to one `gettimeofday()`.
- **Any positive sleep costs up to a full 55 ms BIOS tick.** A frame
  pacer built on `sleep_for()`/`nanosleep` cannot pace a 25 ms frame.
- **Swap the clock and the pacer in the same change.** With the coarse
  55 ms clock the pacer always believes it is behind and never sleeps,
  so it is accidentally harmless. Give it an accurate clock alone and it
  will start sleeping ~55 ms every frame, capping the game near 18 fps.
  Replace the wait with a `uclock()`-deadline wait (interrupts enabled,
  audio still pumped -- see the busy-wait hazard above) in the same
  slice, and validate with an external wall-clock bracket, because the
  change replaces the very clock the engine's self-report uses.

Fix shape: a DOS-only clock type backed by `uclock()` behind the engine's
own clock alias (guarded `#ifdef __DJGPP__`), leaving calendar-time users
on the system clock. **Find EVERY clock alias, not just the main one:**
dosags's first pass replaced the engine's `Clock` and missed a second
alias, `FastClock = std::chrono::system_clock`, which the script
interpreter reads on every script function call -- another ~2.5 ms per
tick. Grep for `::now()` and for every `chrono` clock type, then decide
per user whether it needs calendar time (keep) or a duration (swap). Respect `uclock()`'s own limits documented above
(not monotonic across a wrong-order subtract; read DJGPP's source for its
midnight behaviour before relying on it across a long session).

## With TZ unset, DJGPP's libc can land in a garbage timezone state: wrong dates and ~42 ms clock reads

Found in dosags, 2026-09-17 (its patch 0097 and PLAN.md "libc zone bug").
With `TZ` unset, DJGPP's `tzset()` looks for a default zone file
(`/dev/env/DJDIR/etc/localtime`, then zoneinfo fallbacks). On a target
with no DJGPP install every open fails (errno 22 on the g2k 486), and
what is left behind is not a clean UTC: probes read `tm_gmtoff` of
-1024 s in one process and -675288 s in the next; inside the AGS engine
it came out as +11896 days, so RUNMANIFEST's `started_utc` read 2059 to
2071. `localtime()` stayed correct, which is why nobody noticed.

Two consequences, both invisible unless you look for them:

- **Every `gettimeofday()` -- and so every `std::chrono` clock read --
  cost ~42 ms instead of 1.3 ms** in the bad state, because it runs
  `mktime()` through the broken zone data. CPU-bound phases stayed
  normal while every phase containing a clock read ran ~31x slow.
- **Whether a given process is hit varies with the binary and with the
  environment it runs under (which `SET` lines), so it CONFOUNDS A/B
  comparisons rather than adding noise.** About twenty dosags
  real-hardware cells over four days carried a wrong date, and they were
  the catastrophically slow ones. Some conclusions built on them had to
  be marked "re-measure", including a whole sound-on/sound-off campaign.
  DOSBox-X is not immune, only milder (records dated ~76 days off).

**What every DJGPP port should do:** call `setenv("TZ","UTC0",0);
tzset();` first thing in `main` (`UTC0` is what the fallback was meant to
produce, DOS local time taken as-is), and **treat a `started_utc` that is not today as a
cell invalidity** in any benchmark harness -- it is a free, already-logged
tell. A year check is not enough: the garbage offset can be minutes or
days. Status for the other ports: doskutsu's 128 recorded run dates and
dossage's are all in the right year, neither sets `TZ`, and neither has
been checked at finer grain -- unverified, not cleared. Setting `TZ` in
the target's own boot configuration would protect every program at once;
that is a boot-config change and needs the operator's say-so.

Correction (2026-09-27): an earlier version of this section said a TZ that
is set "is parsed, never loaded from a file". That was wrong. dosags' INT
21h call trace (`docs/lab/io.md` in that repo) shows `tzset()` with
`TZ=UTC0` still tries files first -- `./UTC0`, `./posixrules`, `./GMT`,
about 16 failed calls -- and only then parses the string. The
recommendation stands (the resulting zone state is the intended one, and
the failed probes happen at startup and exit, not in play), but it is a
failed-lookup-then-parse, not a parse-only path.

## SDL_Delay() overshoot on 486-class CPUs (resolved: delay(1) granularity, not clock rate)

dossage/Passage bisected a residual fps gap on 486DX2-66 + S3 ViRGE (see
`docs/optimization.md`) to `SDL_Delay()` timer-precision overshoot, with
real per-frame render work well under budget (~45.6ms against a 66.7ms
frame budget). The original hypothesis here -- a PIT-channel-0 clock-rate
divergence from nominal, possibly tied to no-`RDTSC` 486-class hardware --
was tested directly and **falsified**: `uclock()`, `time(NULL)`, and the
CMOS RTC (read directly via ports 0x70/0x71, independent of any BIOS/PIT
chain) all agreed to within 0.03%, nowhere near the ~1.7% a clock-rate
mismatch would require.

**Actual root cause**, found only once the frame-limiter's own decisions
were instrumented directly: `SDL_SYS_DelayNS`'s wait loop calls DJGPP's
`delay(1)`, which genuinely takes ~1.236ms per call (a fine-grained,
non-BIOS-tick-quantized measurement -- the *earlier* hypothesis that
`delay(1)` itself was coarsely quantized was correctly eliminated), but
the loop can only re-check its exit condition *between* `delay(1)` calls
-- so a sleep can land up to a full ~1.236ms quantum later than its
deadline. **"Resolved" here means the overshoot *mechanism* is settled, not that
any specific fps KPI has been hit** -- that's a separate, still-open
question tracked in `docs/optimization.md`, since the residual gap
between steady-state and a reported number can come from other sources
(e.g. audio-buffer-refill windows) independent of this delay-quantum
finding. This number was sitting in this project's very first real-
hardware probe of the night (measuring `delay(1)`'s own granularity) for
hours before anyone connected it to the pacer's actual behavior -- a
reminder that eliminating a hypothesis ("delay(1) isn't BIOS-tick
quantized") doesn't mean the measurement it produced is done being
useful for a *different* question ("does that per-call cost still matter
inside this specific loop structure").

## First framebuffer flush after SDL_Init can spike 50-70ms on 486-class hardware

**A one-time startup cost, not a per-frame one, but big enough to trip an
unrelated safety cap.** dossage/Passage's `DOS_Yield()` cost instrumentation
(`shared/patches/sdl3-dos/0136`, temporary) found steady-state `DOS_Yield()`
cost cheap (~0.24-0.34ms/call), but a sharp spike in the first ~200 frames
on real 486DX2-66 + Cirrus CL-GD5434 hardware -- up to 71ms and 52.6ms,
16 and 12 spikes over 10ms in those two windows only, nowhere else in a
119-second/1651-frame run. Cross-referenced against `SDLDBG.LOG` from the
same run, this lines up precisely with the very first framebuffer flush
(57.65ms by itself -- the first-ever DPMI mapping, first non-bank-0 write)
and three separate `SDL_DOSAudioPump()` "cap tripped (>32 calls in one
sequence)" warnings (`shared/patches/sdl3-dos/0067`'s runaway-caller safety
cap) firing in that same window, right after `OpenDevice`.

**Generalizable, not dossage-specific**: any port using this backend's
frame-limiter pattern (a `SDL_Delay`/`SDL_SYS_DelayNS` wait loop yielding to
the audio thread) plausibly pays a similar one-time cold-start cost around
its first framebuffer flush, and that cost can be large enough to trip
0067's runaway-caller cap and produce a `DOS_Yield()` burst -- worth
knowing before misreading a startup-only warning burst as a steady-state
performance problem. Plausibly concentrated in a title-screen/pre-frame-
counting phase rather than counted gameplay: dossage logged 1700 real-sleep
invocations against only 1651 counted frames, a gap consistent with extra
invocations during a title-wait loop.

**Not fixed, not chased further as of this note** -- flagged as real and
reproducible, not as something needing a shared-layer change yet. If a
future port's startup feels janky specifically in its first second or two
on 486-class hardware, this is a known, already-characterized cause to
check before assuming a new bug.

## RDTSC from real-mode code hangs the g2k Pentium OverDrive under EMM386 (DJGPP protected mode: not affected so far)

**Finding (2026-09-03, vcctrl rig).** With the Pentium OverDrive 83
installed, the first `RDTSC` executed by a *real-mode* DOS program running
as a V86 task under MS-DOS 6.22 `EMM386 NOEMS` never returns. Interrupts
keep being serviced (keyboard LEDs still respond) but there is no forward
progress; Enter, Ctrl-Break and Ctrl-Alt-Del do nothing, and only a power
cycle recovers. Reproduced twice in `dinspect` (the sibling 16-bit Open
Watcom hardware-inventory tool, via a step-by-step trace build that left
the line printed immediately before `RDTSC` as the last thing on screen)
and independently by HWiNFO for DOS the same day. `CPUID` under the same
EMM386 is fine. Every boot profile on that rig loads EMM386 in `[COMMON]`,
so there is no EMM386-free profile to fall back to. The mechanism has not
been identified (EMM386 reflecting an opcode it cannot emulate, or
something specific to the OverDrive); do not repeat a guess as if it were
known.

**Why every 486-class result was silent about this.** The OverDrive is the
only CPU in the rig's matrix with a time-stamp counter at all. Code that
gates `RDTSC` on `CPUID`'s TSC bit passes every 486DX2 / Am5x86 test by
never executing the instruction, then wedges on the first Pentium-class
tier.

**What it does and does not cover.**

- *Real-mode / V86 code* (a 16-bit Open Watcom or Turbo C utility, a setup
  tool, anything launched from a BAT that is not a DPMI program): treat
  `RDTSC` as unsafe under a V86 monitor. dinspect's fix is the right one
  there: read CR0 via `SMSW` (unprivileged, never traps); if PE is set
  while a "real-mode" program is running, a V86 monitor (EMM386, QEMM, a
  Windows DOS box) is underneath -- skip `RDTSC` and use a PIT-timed loop.
- *DJGPP protected-mode code* (every port built on this hub, every DJGPP
  probe): the same rig, CPU and EMM386 line has been executing `RDTSC`
  from a CWSDPMI DPMI client without incident. doskutsu's 2026-08-13
  round-2 QA matrix on the OverDrive (`qa-results/2026-08-13-r2-POD83/
  *SDL.LOG`; UNIVBE boot profile with `EMM386 NOEMS`; cpu-witness family 5
  model 3 stepping 2) logs `audio IRQ timer: RDTSC (Pentium-class
  detected, 83 cycles/us ~= 83 MHz)` on every cell: SDL/0039 ran a 10 ms
  `RDTSC` calibration spin at device open and then executed `RDTSC` inside
  every Sound Blaster IRQ for whole sessions. A ring-3 DPMI client under
  CWSDPMI (hosted via VCPI, with its own GDT/IDT) is a different execution
  context from a V86 task, and it is the context a port actually runs in.
  Treat this as "observed not to hang", not "proven safe".

**What a DJGPP program can and cannot detect.** The `SMSW`/PE test is
meaningless from a DPMI client: PE is always set in protected mode, so it
would disable `RDTSC` everywhere, DOSBox-X and EMM386-free machines
included. DPMI function 0400h's flags bit 1 ("host returns to real mode,
not V86, for reflected interrupts") is no better: CWSDPMI hardcodes the
flags word to 1 or 5 (`exphdlr.c`, `case 0x0400: tss_ebx =
dalloc_max_size() ? 5 : 1`), so it reports V86 whether or not a monitor is
loaded. The only real signal is VCPI presence (INT 67h AX=DE00h), which
the shared layer deliberately does not consult because the evidence above
says its context is fine.

**What the shared layer does.** `shared/patches/sdl3-dos/0039` keeps its
default (RDTSC when `CPUID` reports a TSC, 8253 PIT counter 0 otherwise).
`0138` adds a runtime killswitch, `SDL_HINT_DOS_AUDIO_TIMER_RDTSC=0`
(strict `0`), that selects the PIT path unconditionally and logs
`audio IRQ timer: 8253 PIT counter 0 (RDTSC disabled via
SDL_HINT_DOS_AUDIO_TIMER_RDTSC=0; ...)`. If a run on new Pentium-class
hardware, or under a different memory manager, stops at audio device open
with the symptoms above, set that env var before a rebuild is even
considered. The PIT path costs only telemetry resolution (~838 ns per
tick against ~12 ns) and is the path every 486-class result on this hub
was measured with.

**Real-hardware A/B of the killswitch: pending.** The OverDrive was
swapped out of the rig for a 486DX2-50 campaign on 2026-09-03 before the
`0138` build could be run on it, and a 486 predates `CPUID`, so nothing
RDTSC-related can be tested until a Pentium-class CPU is back in the
socket. The witness is `shared/tests/probes/audtimer.c` + `audtimer.bat`
(SDL3-linked; step 1 forces the killswitch and never executes `RDTSC`,
step 2 is the production default). Expected on the OverDrive: step 1
logs `8253 PIT counter 0 (RDTSC disabled via
SDL_HINT_DOS_AUDIO_TIMER_RDTSC=0; ...)`, step 2 logs `RDTSC
(Pentium-class detected, 83 cycles/us ~= 83 MHz)`. Whoever runs it next:
record the result here and in `HARDWARE.md`'s row.

**For port-side and probe code.** Prefer `uclock()` / the PIT for timing.
If a port or probe wants `RDTSC` from DJGPP, gate it on `CPUID` (TSC bit),
give it a killswitch, and put the first `RDTSC` somewhere a wedge would be
recognized immediately, not inside an IRQ handler. Never execute it from
any real-mode helper without the `SMSW` check.

## Open (dossage, 2026-09-05): a fixed-length run can land one second over an integer-second boundary on some CPU/card pairings and not others

Unconfirmed and not chased -- recorded so the next port doing precision
fps work recognizes the shape. Across dossage/Passage's full 3-card x
4-CPU matrix on one build, a natural ~298 s life measured as 299 s on
some pairings (Cirrus at both 486DX2 tiers, Mach64 on Am5x86, and Mach64
on 486DX2-50 on two of three repeat lives) and 298 s on the rest, with
S3 ViRGE never showing it at any tier. Neither banked-vs-LFB, framebuffer
bytes per frame, nor CPU tier explains the table on its own. The working
theory in `docs/benchmarks/mach64-215ct-486dx2-66-2026-09-05.md` there:
overall per-frame margin sets how close a pairing sits to the rounding
boundary, and ordinary run-to-run jitter (plausibly the same
DOS-tick-phase quantization as the `gettimeofday()` finding above)
decides which side an individual life lands on. Separating those two
needs repeat lives per pairing or per-frame-cost instrumentation;
nothing in that table threatened a KPI, so it stayed open.

## Reference pacer patterns from doskutsu (deadline-based, not delta-based)

dossage/Passage's frame-rate investigation (see `docs/optimization.md`)
found real per-frame render work well under budget with no single fixable
bottleneck -- the residual gap traced to the *pacer's own shape*, not to
render cost. doskutsu's `nxengine-evo/src/main.cpp` has two working
pacers worth citing directly rather than reinventing, both confirmed by
reading the actual vendored source, not from memory:

**The render/game-loop pacer** is deadline-based, not delta-based: it
holds an absolute `nexttick` target and compares it against a fresh
`SDL_GetTicks()` read every iteration, rather than computing a sleep
duration once and trusting it. When time remains, it sleeps *half* the
estimated remainder, then loops back and re-measures real elapsed time
against the deadline again -- converging rather than committing to a
single sleep call. `nexttick` resets from the actual current time when a
tick fires (not additively from the previous deadline), so this path
doesn't accumulate unlimited backlog on its own.

**The gameplay simulation pacer**, `run_tick_fixed()`, is a true
fixed-timestep accumulator: an absolute nanosecond timeline
(`SDL_GetTicksNS()`), a fixed tick interval (`TICK_NS = 1e9/GAME_FPS`),
and a catch-up loop that drains accumulated backlog as discrete ticks,
capped at a hard `MAX_CATCHUP` of 5.

**Correction to an earlier version of this note**: MAX_CATCHUP=5 is a
real, validated precedent for a debt ceiling's *size* -- but not because
letting a stall inflate the catch-up backlog is a general gameplay
hazard. It isn't, in doskutsu: outside TAS replay, the accumulator delta
is completely **unclamped** ("bit-for-bit the pre-0281 path" per the
source's own comment) -- a stall can and does inflate `accum_ns`, and the
catch-up loop drains it via up to 5 discrete ticks, unguarded, as normal
shipped gameplay behavior. The stall-discount/clamp machinery (patches
0281/0286/0288) that *does* exist is gated entirely behind
`TAS::active()`/`TAS::replaying()` -- it protects **TAS recorded-input
replay fidelity** specifically (each catch-up tick consumes one recorded
input; during a genuine stall those extra ticks apply the wrong, held
input and drift a recorded route off its path), not general simulation
correctness. Patch 0284's own commit message goes further: 0281's
original premise -- that tick-count inflation itself was what corrupted
replays -- was directly tested and found **wrong** (0283 found an
unrelated input-hold bug was the real cause; a controlled A/B with that
fixed showed clamped and unclamped replays produce identical traces). So
the lesson isn't "a catch-up accumulator needs a stall guard or a loading
screen desyncs the game" -- doskutsu's own gameplay doesn't need one. The
lesson is narrower: **only a port with a discrete-tick-consumes-one-
recorded-input replay/TAS system inherits this specific hazard.** A port
without one (e.g. a continuous-integration pacer where "catching up"
means not sleeping rather than running multiple discrete world-step
calls) has no analogous mechanism to desync in the first place. Take
MAX_CATCHUP's *number* as precedent if useful; don't take the incident
history as evidence that an unclamped catch-up accumulator is dangerous
for gameplay in general -- it demonstrably isn't, in the one shipped
engine this hub has real data on.

**The precedent that matters most across both mechanisms**: doskutsu
paces and measures using SDL's own timer API throughout
(`SDL_GetTicks()`/`SDL_GetTicksNS()`) -- never an engine-private clock
(e.g. a separate `gettimeofday()` call -- which on DJGPP has no
sub-second resolution of its own anyway, see "Your clock can lie")
running in parallel with whatever clock the delay primitive itself uses. This is the same "single clock
family end-to-end" principle a same-clock instrumentation pass can prove
out before trusting a cross-clock measurement (see the `DOS_Yield()`/
frame-cycle-time diagnostics above) -- and it applies to the *pacing*
decision itself, not just to measuring it: an engine whose own frame-time
measurement and whose delay mechanism read from different clocks has a
second, independent source of per-frame error regardless of which pacer
shape it uses.

## Add debug output for

```
simulation Hz
render FPS
frame time
audio underruns
dropped renders
```

This instrumentation should exist before any optimization work starts —
see `docs/optimization.md`: never optimize without numbers.

**Use `shared/include/runmanifest.h` for the baseline fps/duration/exit
numbers rather than hand-rolling them.** It's a header-only, drop-in
library: include it and call `runmanifest_emit()` once at clean shutdown
for a standardized, greppable `[RUNMANIFEST-BEGIN]`/`[RUNMANIFEST-END]`
block (`fps_p50`, `fps_p95`, `duration_s`, `exit_code`, `environment`, a
build fingerprint, and port-specific extra fields as needed). See
`shared/skills/dos-hardware-validation/references/runmanifest-log.md`.
Wire it in early (STARTS/TITLE_SCREEN milestone), not deferred to
OPTIMIZING — dossage/Passage hand-rolled its own engine-side fps
computation from the start, and a later real-hardware investigation spent
hours chasing an fps gap before finding the actual clock bug lived inside
that same bespoke fps math: the number being measured and the mechanism
under suspicion were the same unreviewed code (see `docs/optimization.md`
for the full investigation). Using the shared, already-reviewed header
doesn't guarantee no bug, but it means the baseline measurement itself
isn't the thing you'd later have to distrust.
