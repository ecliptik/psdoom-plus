# The emulator/hardware boundary

DOSBox-X is faithful enough to be genuinely useful for most of a port's
life, and unfaithful in specific, knowable ways that matter a great deal
when they matter. Knowing which category a given question falls into is
the actual skill here -- not "trust the emulator" or "distrust the
emulator" as a blanket rule.

## What a clean DOSBox-X run actually proves

- The code compiles, links, and the binary runs to completion (or to
  whatever milestone you're checking) without crashing.
- Video mode negotiation, input event delivery, and filesystem access
  work along the code paths DOSBox-X exercises the same way a real BIOS/
  DOS/VESA stack does for those operations.
- Most gameplay logic -- anything not gated on exact hardware timing or a
  specific chip's quirks -- behaves the same as it will on real hardware.

## What it does not prove

- **Real performance.** See `local-benchmarking.md` -- a hard constraint,
  not a nuance.
- **Exact device timing/quirks.** An emulated device can accept, reject,
  or silently no-op an operation differently than the real chip it's
  modeling, especially around reset sequences, detection handshakes, and
  DMA timing. `dos-realhw-verification`'s smoke-gate-discipline material
  documents a concrete case: a probe passed 9/9 clean in DOSBox-X and
  froze real hardware on first run, because DOSBox-X's emulated SB16
  DSP-reset behaves differently on failure than the real chip, silently
  routing the probe around the one hardware I/O sequence it existed to
  test.
- **DPMI/LFN passthrough behavior.** Whether a real-mode TSR loaded under
  plain DOS actually receives a DPMI-reflected long-filename call is an
  empirical question about a *specific* DOS+TSR combination, not
  something DOSBox-X's own DPMI implementation can answer on a real
  target's behalf.

## A "declines / falls back" log line is not automatically benign emulator divergence

It's tempting to read a boot-time "device already open, declining,
falling back" line under DOSBox-X as "the emulator has no such peripheral
to probe, this is expected degradation, real hardware has the chip and
won't hit this" -- and sometimes that's correct. It was wrong at least
once: the actual cause was a real ship-gating logic bug, an init-order
guard that always declined the legitimate cold-init path, not just the
wedge case it was written to prevent. The tell that was ignored: the
decline *reason* was an init-order statement ("already open"), not a
hardware-absence statement ("not detected") -- those are different
failure classes, and only the hardware-absence one is safe to shrug off
as emulator divergence. Before waving off a decline/fallback line as
"DOSBox-X just doesn't have this," read the actual reason string and
classify it as one or the other explicitly.

The second half of this lesson generalizes past DOSBox-X entirely: a
defensive guard added mid-ship to fix one bad case can silently break the
good case it was never re-tested against. **Prove a new guard is FALSE
on the happy path, not just TRUE on the bad path it was written for** --
verifying only that the guard fires when it should leaves its
false-positive rate on legitimate input completely unchecked. The test
that actually caught this: a binary-vs-binary A/B in the identical
environment, only the binary differing -- which is also the reason it's
worth freezing the last known-good binary specifically, so that
comparison is always available on short notice rather than needing a
fresh "known good" rebuild under time pressure.

## Emulator-only escape hatches: track them, never let them leak

Sometimes the only way to get useful signal out of DOSBox-X for a given
subsystem is to explicitly work around a place where its emulation
diverges from real hardware -- `SDL_DOS_AUDIO_SB_SKIP_DETECTION` (see
`tooling.md`) is the concrete example already baked into this hub's own
runner: it skips detection and trusts the configured `BLASTER` line.
**Corrected 2026-09-22:** the "emulator limitation" it worked around was
our own runner's `-silent` flag switching the emulated Sound Blaster
off. DOSBox-X's SB answers detection fine without it (`tooling.md`).
That is the second lesson here: before building an escape hatch around
"the emulator can't do X", rule out your own invocation, then write
down which experiment showed the limitation.

**The rule this generalizes to**: any env var, conf setting, or code path
that exists specifically to work around an emulator limitation must be
(1) clearly named/commented as emulator-only where it's set, and (2)
never set when running on real hardware. Setting `SDL_DOS_AUDIO_SB_SKIP_DETECTION`
on real hardware wouldn't just be redundant -- it would actively mask a
real detection regression (a genuine Vibra16S/SB16-compatible-card
failure) by skipping the exact check that would have caught it. When you
add a new escape hatch to get a DOSBox-X smoke passing, ask explicitly:
where does this get unset (or refused) for a real-hardware run, and is
that enforced somewhere rather than just remembered?

## "Necessary but not sufficient": the DPMI-LFN probe pattern

`shared/tests/dpmi-lfn-smoke/` is a clean worked example of a probe
designed around this boundary honestly: its own README states outright
that the DOSBox-X run "is necessary but not sufficient... proves the
probe code is correct; it does NOT answer the real-HW [DPMI-LFN
passthrough] question." The DOSBox-X pass is still valuable -- it rules
out a whole class of "the probe itself is broken" explanations cheaply,
before spending any real-hardware time -- but the actual question the
probe exists to answer only gets answered by running it on the real
target under the real DOS+TSR combination in question.

Apply this pattern whenever a probe or smoke test is standing in for a
question that's fundamentally about real-hardware/real-DOS behavior:
state explicitly, in the probe's own doc, what a DOSBox-X pass rules out
and what it leaves genuinely open -- don't let "it passed in DOSBox-X"
quietly expand to cover the open question too.

## When to graduate off this skill

Reach for `dos-hardware-validation`, `dos-rig-operations`, and/or
`dos-realhw-verification` instead of (or in addition to) staying purely
local when:

- **Any actual performance/fps claim needs to be made** -- to a
  teammate, in a commit message, in a milestone report. See
  `local-benchmarking.md`.
- **The question is device-specific** -- audio chip detection/timing,
  VESA/Cirrus/S3-specific video paths, gameport joystick behavior, or
  anything else where the real chip's behavior is the actual subject.
- **The question is DPMI/LFN passthrough, or anything else DOSBox-X's
  own implementation can't stand in for a real target.**
- **Before claiming a milestone** ("playable," "release-ready," a
  shipped fix "confirmed") -- per `docs/testing.md` and
  `dos-realhw-verification`'s two-witness discipline, a clean DOSBox-X
  smoke is a gate, not proof.
- **A bug is suspiciously "correct" in DOSBox-X for something that's
  hardware-conditional** -- see `dos-realhw-verification`'s divergence-
  debugging material for how to reason about that gap rather than
  assuming the emulator result generalizes.

## Deterministic timing comparisons: run DOSBox-X at FIXED cycles

**Established 2026-09-23 (dosags lab-mmd, MMDCF cells).** At `cycles=max`
DOSBox-X runs as fast as the host lets it, so its timings follow HOST load.
That includes other workers' builds, which a DOSBox-X lock can't serialise.
The same binary and settings gave `render_backbuffer` 1.10-1.70 ms/call
(+-22%) and a script event 17.5% apart between two runs. At a fixed cycle
count (`cycles=fixed 40000`, normal core), two runs of the same binary and
settings produced **byte-identical** logs.

So, for any lab comparison of time between builds or arms (an A/B, a probe's
own cost, a before/after ranking), use fixed cycles. Use `cycles=max` only
for counts and functional checks, where speed doesn't change the answer.

**The caveat that keeps this honest:** fixed cycles charges roughly one cycle
per emulated instruction, so it ranks code by INSTRUCTION COUNT, not by a real
CPU's cycles per instruction. On the 486 those differ by kind of code. In the
same run at fixed 40000, a tight blit loop (`render_backbuffer`, 9.42 ms/call)
matched the real 486DX2-66 (9.43 ms). Branchy, large-footprint
script-interpreter code ran 2.2x FASTER than on the 486 (10.98 vs 24.26
ms/tick). So a fixed-cycles ranking is exact between two versions of the
SAME code. Across different kinds of code it needs a per-kind factor
measured on real hardware before it predicts 486 time. That's the same
"validate the lab metric before trusting it" rule
`benchmark/references/self-driving-loop.md` applies to any counter.
