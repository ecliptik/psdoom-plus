# ABBA methodology with pre-registered thresholds

A method for turning a real-hardware A/B performance claim into something
someone can act on, instead of "some numbers that happened."

## Why ABBA, not A-then-B

A straight "run A, then run B" comparison confounds the lever under test
with anything that drifts over time -- thermal state, a card warming up,
a background process, session-to-session rig variance. ABBA (A/B/B/A,
repeated as a block) cancels a monotonic drift across the block: if
performance is trending up or down for reasons unrelated to the lever,
that trend shows up symmetrically in both A's and both B's rather than
biasing the comparison toward whichever arm happened to run first.

## Steps, in order

1. **Pick the lever(s) and write the pre-registered thresholds** -- what
   delta magnitude counts as a real win, what counts as noise, what counts
   as a regression -- *before* running the first cell. If the thresholds
   get adjusted after seeing the data, the result is no longer a test, it
   is a story fit to the data.
2. **Write the invalidity conditions** -- see below. Also before running.
3. **Run the block**: A, B, B, A (one full cell each, full
   set/forbid/expect_log discipline per `cell-protocol.md`). Repeat the
   block (x2 or more) if the campaign's budget allows -- a single block
   is the minimum, not the target.
4. **Check invalidity conditions before looking at the delta.** If the
   block is invalid, don't compute a verdict from it -- re-run instead.
5. **Compute the verdict** from the pre-registered threshold table, not
   from eyeballing the raw numbers.

## Invalidity conditions (check these before trusting any delta)

- **A-to-A spread too wide.** The two A cells in a block should agree with
  each other within a stated band (a same-config, same-binary re-run
  should reproduce closely). If they don't, the rig was not sitting
  stable during this block -- something changed between the two A runs
  that had nothing to do with the lever, and the B cells sandwiched
  between them are not trustworthy either. Discard the block, don't
  average through the instability.
- **Baseline does not reproduce a known prior figure within a stated
  band.** If this port/config has a previously-measured baseline number,
  today's A cells should land near it. If they don't, something changed
  outside the lever under test (a different binary than intended, a rig
  config drift, a different card/CPU than expected) -- resolve that before
  trusting any comparison run on top of it.

Both checks exist because a comparison run on an unstable baseline can
produce a confident-looking delta that is actually measuring instability,
not the lever.

**Compute your invalidity conditions against the expected magnitudes
before you run — a threshold written as a phrase may sit exactly where
the result will land.** One campaign pre-registered an experiment whose
validity required the intervention to change free disk space by "more
than a few percent". Sizing the work beforehand showed it would change it
by **under 4%** — right on the threshold, so the run would have produced
a number with no principled way to decide whether to believe it.

**And the fix was not a better number, it was a better quantity.** Total
free space was the wrong thing to measure: what the experiment needed was
whether the *allocator* behaved differently, and deleting hundreds of
small files punches holes throughout the table while barely moving the
total. The replacement condition is empirical — re-run the probe
afterwards and see whether its own spike positions move. **That promotes
the predecessor measurement into the experiment's control**, which is
where it should have been from the start.

Two habits follow. **Put real numbers into every invalidity condition at
pre-registration time**, not at scoring time. And when one turns out to
be marginal, ask whether the quantity is wrong before reaching for a
different threshold on the same quantity.

## Calibrate a fit tolerance against a null before adopting it

"Within 10% of an integer multiple" sounds like a criterion. It is only a
criterion if random data **fails** it — and whether it does depends on
the tolerance, the number of points and the search range, none of which
are obvious by inspection.

A campaign testing whether stall intervals fit a common period ran 20,000
draws of six uniform random gaps through its own fitting procedure before
fixing the threshold:

| tolerance | null pass rate |
|---|---|
| 0.15 | 2.50% |
| **0.10** | **0.38%** |
| 0.05 | 0.00% |

That turns a plausible-sounding number into a known false-positive rate,
and it showed why the middle value was right: 0.15 would pass one random
dataset in forty, while 0.05 risked rejecting a true signal over jitter
in a 220 ms event measured on a 486. **Run your own procedure against
noise and see how often it says yes.** It costs a loop and it converts a
guess into a calibrated test.

Pair it with closing the degrees of freedom the fit would otherwise have:
that search constrained the period to `[max(gap)/4, max(gap)]`, which
forces every multiplier into {1,2,3,4} so the period **cannot shrink
until it divides everything**. A free parameter with an unbounded range
fits any data, which is the same hole as a pre-registered range with an
undefined middle.

## A null must carry the observed data's own structural constraints

The section above says calibrate a tolerance against a null. This one is
about getting the null right, and it cost a published figure to learn.

A lattice fit to eight event positions scored a worst residual of 0.057.
Calibrated against 2,000 draws of eight **uniform random** points from
the same range: **0 pass**. That was written into `HARDWARE.md` as
evidence the lattice was real, with the caveat that a
spacing-constrained null was still running and "will not move 0% to
anything you would worry about."

It moved. Two people then produced **six different numbers for the same
eight points**, spanning 0.00% to 6.12%, before anyone noticed they were
all answering different questions.

The immediate cause is that **uniform draws clump** — two points three
records apart are common — and a clumped set cannot lie on any lattice,
so the uniform null was dominated by draws that could never have passed
whatever the data looked like. It was a strawman. Imposing the events'
observed minimum spacing of ~30 records makes the null a real competitor.

But the repair exposed the actual problem. Sweeping the assumed minimum
spacing, with a correct uniform-over-feasible-configurations sampler:

| assumed minimum gap | free slack over 7 gaps | null pass rate |
|---|---|---|
| 10 records | 182 | **0.00%** |
| 20 | 112 | 0.18% |
| 25 | 77 | 0.53% |
| 30 (observed minimum was 31) | **42** | **6.12%** |

**The entire result is the minimum-spacing assumption.** Look at the
slack column: eight events across 245 records have a mean gap of 35, so
once a 30-record floor is imposed there are only 42 records of freedom
left across seven gaps. **Near-even spacing is then close to forced**,
and a lattice fit is measuring the constraint rather than the data.

The cleanest way to see it needs no sampling at all. **Bound the
statistic analytically against the constraint before simulating
anything:** with seven gaps, a 245-record span and a floor of `f`, the
largest possible gap is `245 - 6f`, so the max/min ratio is capped at
`(245 - 6f)/f` — **2.17 at a floor of 30**, against 18.5 at a floor of
10. A ratio that cannot exceed 2.17 is already nearly even, whatever the
mechanism. Had anyone written that line first, neither the 0% nor the
6.12% would have been published.

And the floor was **taken from the observation itself**. Nothing
independent establishes that these events have a refractory period. So
the null either conditions on part of the finding (min gap 30, and the
lattice says little) or ignores a real physical constraint (min gap 10,
and the lattice looks decisive) — **and the data cannot settle which.**

Three rules, in increasing order of how much they would have saved:

**Build the null from the constraint set the observation lives in** —
every constraint the real data satisfies belongs in the null too, or the
p-value is an artifact of the comparison.

**Report the null's construction alongside its number.** "0 of 2,000" is
not a fact about the lattice; it is a fact about the lattice *and* a
sampling choice, inseparable to a reader who sees only the first.

**When a statistic's value is dominated by a modelling choice the data
cannot justify, say that instead of picking a value.** Stop trading
numbers. "This is undeterminable without an independent model of the
event process" is a real finding and an honest one; a number chosen from
a range spanning two orders of magnitude is neither.

### The tell, and the cheap practice that defuses it

Across this exchange two people reproduced the same calculation four
times. **Every single reproduction came out favouring the reproducer's
own prior** — the one who had fitted the lattice kept getting figures
kind to it; the one auditing it kept getting figures unkind to it, in
both directions, including after fixing a bug that moved the answer
against the fixer. Nobody was arguing. Nobody noticed it in their own
work. **Each caught it only in the other's.**

That is not a reason to distrust calculation; it is a reason to expect
this specific failure whenever someone computes a statistic whose value
they have a stake in. It generalises the rule elsewhere in this hub that
a number confirming your prior gets less scrutiny — here the bias
operated one level earlier, in the *construction* of the calculation
rather than in the reading of its output, where no amount of checking
the output would have found it.

**Trigger the check on inconsistency, not on incentive.** The same
campaign produced a hand-written table and a script that disagreed by
~2x; the author flagged it, and the correction happened to make their own
proposal look *cheaper* — the flattering direction, declared anyway. Asked
about it afterwards, they noted they had not spotted which way the error
leaned: **the habit fired on the two artifacts disagreeing, not on
recognising an incentive.**

That is the more reliable trigger of the two, and it is worth preferring
deliberately. "Would this flatter me?" requires correctly identifying
your own bias, which is the thing the bias is best at preventing. "Do my
two sources of this number agree?" requires only arithmetic. **Build
checks that fire on mechanical inconsistency** — a document against its
script, a summary against its log, a constant against the run that
measured it — and they will catch motivated errors as a side effect,
without anyone having to be honest about their motivation first.

**The practice: when your own calculation favours your prior, hand it
over instead of leading with it.** Say what you computed, what you
chose, and that it came out your way — and let the other party's
reproduction be the one that gets quoted. This exchange converged
instead of hardening for exactly that reason: each side withdrew a
number that flattered it, twice, rather than defending it. The cost is
one message. The alternative is two defensible numbers and no way to
choose between them.

## Do not compare dispersion across a shift

`max/min` is **not scale-invariant under translation**. Add a constant to
every value in a set and the ratio of extremes shrinks — always, by
construction, regardless of the data.

A campaign nearly concluded that stall intervals were "more even in time
than in bytes" from a 2.8x spread against 7.3x. The time figures were
byte gaps converted by a per-record cost **plus each event's own ~220 ms**
— a near-constant added to every gap. The comparison measured the
addition. Sweeping the unmeasured per-record constant made the same seven
events produce anywhere from 1.51x to 4.84x.

**Any "more even in X than in Y" claim across a change of variable that
shifts values is measuring the shift.** Use a scale-free statistic — a
coefficient of variation, or a structural fit like the integer-multiple
test above — and measure the constant rather than assuming it, since an
assumed constant that drives the conclusion makes the conclusion a
property of the assumption.

## Redesign so the denominator is known, rather than measuring it

When a test needs a quantity you would have to model, look first for a
configuration in which that quantity is **exactly known** instead.

Testing whether an event fires at a fixed rate per byte written, one
campaign planned to instrument bytes-to-device per arm of a four-arm
probe. The better move was to **delete the arms**: with them gone, the
sweep's 393,216 bytes were the only device bytes in the run, and the
denominator needed no modelling at all.

The instrumentation would not have worked anyway, for a reason worth
separating: **only an explicit sync guarantees a commit**, so for the
buffered arms the true device-byte count was **unknown, not merely
unmeasured** — no instrument placed in the program could have recovered
it. **Check whether a quantity is measurable at all before planning to
measure it**; where it is not, the experiment has to be reshaped rather
than better instrumented.

## A rate hypothesis needs its distribution predicted, not just its count

"About N events per M bytes" can be satisfied by a process that is
nothing like a rate — several events bunched together and a long empty
stretch produce the right total for entirely the wrong reason.

So pre-register the **spacing** alongside the count. The campaign above
wrote three conditions rather than one: the count (8-12 events in the
known byte total), the **magnitude** (unchanged from earlier runs, since
a rate hypothesis gives no reason for the operation to cost differently),
and the **evenness** (gaps within roughly 2x of each other). Only the
third can distinguish a rate from a burst, and it is the one a
count-only registration silently omits.

Note also what a confirmed rate does and does not buy, which that
registration stated in advance: it **names the family without settling
the mechanism** — it does not say whether the counter lives in the
device, the OS or the firmware, and the experiment that separates those
is a different one.

**A noise floor belongs to a fixture, not to a machine.** Measured on one
real target in one campaign: 0.12 fps spread on an idle fixture and 0.24
on a moving-content one, same machine, same session. Carrying the smaller
one across would have halved the bar a delta had to clear. Measure the
floor for the fixture you are about to draw a conclusion on.

## The second noise floor: build layout, which A-to-A cannot see

An A-to-A check re-runs **the same binary**, so it measures run-to-run
variance and nothing else. It is structurally incapable of detecting
**build-to-build layout variance** -- the performance difference between
two binaries that do the same work but whose code the compiler placed,
aligned, or allocated registers for differently. On a 486-class target
with a small cache and no branch prediction, that is not a rounding
error: a real campaign measured a build with *strictly more work in it*
running **0.93 fps faster** than the same revision without the added
instrumentation, four times that fixture's run-to-run spread.

This matters because **every single-build A/B comparison is exposed to
it.** An A/B of patch N versus patch N+1 compares two different binaries,
so its delta contains the lever's effect *plus* whatever layout variance
those two builds happen to differ by. If the layout floor is comparable to
the delta, the measurement cannot attribute the difference to the patch at
all -- and no amount of diffing the source will ever find the mechanism,
because there isn't one. Repeated failure to find a mechanism for a
reproducible delta is a signal worth taking seriously here.

**Measure it with a layout-perturbation ensemble.** Build three or four
**semantically identical** variants of the *same* revision -- vary
`-falign-functions`/`-falign-loops`, insert padding ahead of the hot
function, reorder two independent functions -- and run a cell on each. The
spread across that ensemble is the layout floor, and any single-build A/B
smaller than it is unattributable. Two practical notes: an added probe or
banner is *not* a semantically neutral perturbation if it executes on the
hot path, and the floor is per-translation-unit as much as per-fixture, so
measure it on the TU the lever actually touches.

The strategic reading matters as much as the number. When a target sits
right at a frame-budget boundary, a layout lottery of this size decides
gate outcomes. **The durable fix is to take enough real work out to move
off the margin**, not to keep chasing deltas the size of the floor.

**Measure a floor with a deliberate ensemble; never infer one from
incidental observations.** A real campaign watched two code-adding
changes land at +0.93 and -1.04 fps, concluded from those two points that
growing the hot path carried a +/-1 scatter, and wrote down a ~2 fps
floor. Measured properly with dead-but-emitted blocks at three sizes, the
actual size effect was **0.19 / 0.23 / 0.27 across a 5.6x size range** --
smooth, monotone, no cliff, and indistinguishable from that target's
0.19 alignment floor and 0.24 run-to-run floor. The real floor was ~0.25,
not 2. The cost of the error ran both ways: a correctly-attributed 1.21
fps regression was retracted as noise, and a wrong floor reached this
file before the ensemble that refuted it had been run.

So the useful rule is narrower than "calibrate by perturbation class,"
though that still holds: **two observations are an anecdote, and the
ensemble is cheap.** If a floor is load-bearing for an attribution, run
the variants.

**The diagnostic signature and the test that settles it.** When two
unrelated patches each add small, provably cheap logic to the same hot
path and each cost about the same, with no mechanism surviving scrutiny,
the natural hypothesis is that growth itself is the cost -- an I-cache
boundary or an inlining flip. Test it with **dead-but-emitted code**: a
block of comparable size behind a condition the compiler cannot fold away
(a `volatile` read, an env var), so it is emitted but never executed.
Confirm emission by diffing instruction counts, or a folded-away block
gives a null result that means nothing.

In the case above the test came back **negative** -- inert code the same
size as the real change cost 0.23 where the real change cost 1.05, a 4.6x
difference at identical growth. That is a strong result, not a wasted
round: it proves the cost is *execution*, eliminates size, layout and
inlining in one cell, and sends the investigation back to finding out why
calls that look like nanoseconds are not. A cheap test that kills a
plausible hypothesis is worth more than the argument it replaces.

## A control must be invariant under the change you are testing

A control variable is only a control if the change cannot move it. Pick
one the change *does* move and you have registered a threshold that will
fail for the right reason and read as a defect.

A real campaign registered "sprite pixels composited must not change" as
the primary check on a change to *invalidation*, reasoning that the
composite path was untouched. It held perfectly under the emulator. **It
could not hold on hardware by construction** — the change alters which
frames get skipped, so both arms stop rendering the same frames, and
every per-frame quantity shifts its denominator. The same run's
`render_backbuffer` figure rose and breached its regression bound while
the code was **19% faster per pixel**, compositing 43% more pixels
because fewer frames were skipped.

**Two rules come out of it.**

**Ask what the change alters about the population, not just about the
code.** Anything that changes which frames execute, which branch is
taken, or how often a path runs invalidates every per-unit mean as a
control. Express thresholds in a unit the change cannot move —
per-pixel, per-byte, per-call — and keep per-frame means for description
only.

**And an emulator can satisfy a control for a reason that does not exist
on the target.** Here the emulator never falls behind, so both arms
rendered identical frames and the control held for a reason the hardware
does not share. The verification passed; its *validity* did not transfer.
When a pre-cell check depends on the emulator behaving like the target,
say which behaviour it depends on and confirm the target has it.

## Prefer a within-build A/B: put the lever behind a runtime switch

Everything above -- layout variance, code-size scatter, inlining flips,
I-cache placement -- exists only because the two arms are **two different
binaries**. Compile the lever behind a **runtime hint or env var and A/B
within one build**, and all of it vanishes by construction: identical
code, identical layout, identical cache behaviour, toggled at startup.
The floor collapses back to the run-to-run figure, which is typically an
order of magnitude smaller.

**Any effect smaller than the code-adding floor must be measured
within-build or not claimed.** Large deltas are safe either way -- a
comparison of presentation modes differing by 13+ fps was never at risk
from a 2 fps floor -- so this is a rule about small effects, which is
exactly where optimization work lives once the obvious wins are gone.

This usually costs nothing to adopt, because a lever worth measuring is
often a lever worth shipping as a tunable anyway. Build the switch for
the feature and the measurement method comes free with it. Two cautions:
the switch itself must not be read on the hot path (hoist it to a
startup-time bool), and both arms still carry the code, so a within-build
A/B tells you the lever's value but *not* what the added code cost to
include -- that question needs the dead-but-emitted test above.

## The same argument one layer out: prefer a within-boot A/B

The section above is about not rebuilding between arms. The identical
argument applies to not **rebooting** between arms, and it is easy to
miss because a machine-configuration lever does not look like a code
lever.

An investigation into a periodic stall identified a resident disk cache
as its leading candidate and reached for the obvious test: change
`CONFIG.SYS`, reboot, re-run. That test carries a reboot, a config file
to restore and verify, a documented keyboard-hang hazard on the return
boot, a mains cycle on thirty-year-old parts, and a boot-config change
that sat outside the campaign's standing authorization. **The same cache
reconfigures from an ordinary command line while resident** -- both arms
from one boot, nothing to restore, and every boot-to-boot difference
removed by construction rather than by hope.

**Before designing a cell around a reboot, ask whether the component has
a runtime interface.** Resident caches, mouse drivers, sound
initialisers, VBE providers and TSRs frequently do. When one does, the
within-boot A/B is strictly better on every axis: fewer confounds, less
hardware risk, a smaller authorization footprint, and a faster turn.

Two conditions, both the same ones the within-build rule carries. **The
toggle needs its own `expect_log`** -- a status readout proving the arm
actually took, since a reconfiguration that silently did nothing reads
exactly like "not the cause". And **verify the invocation against the
tool's own help output on the machine**, not against anyone's
recollection of the manual; the running instance's status table is a
measurement, an argument about argument grammar is not.

## Verdict table shape

Key the verdict to the delta magnitude against the pre-registered
thresholds, not to statistical significance alone (a real hardware sample
size is usually too small for significance testing to be the deciding
factor) -- e.g.:

| Delta vs. baseline | Verdict |
|---|---|
| Within the noise band (± threshold) | No measurable effect -- do not claim a win or a loss |
| Beyond the noise band, below the "large" threshold | Small effect -- note it, don't lead with it |
| Beyond the "large" threshold | Large effect -- actionable conclusion |
| Negative beyond the noise band | Regression -- do not ship without understanding why |

Set the actual threshold numbers per port/metric based on that port's own
measured noise band (see `docs/optimization.md` and `docs/timing.md` in
the sdl-dos-ports hub for why real-hardware FPS noise has been measured at
several times the within-session band -- a threshold copied from a
different port's noise characteristics is not safe to reuse blind).

## What a finished campaign write-up needs

- The full verdict table and which row the result landed in.
- The actual per-cell numbers (raw data, not just the computed delta --
  see the main skill's "hand off raw data" rule).
- Which invalidity checks were run and that they passed.
- Identity fields for every cell (see `cell-protocol.md`'s Collect
  section) -- a campaign result is only as trustworthy as the weakest
  cell's identity confirmation.
