# Optimization

Always optimize based on profiling — instrument first (see `timing.md`),
optimize second. Do not prematurely rewrite major systems in assembly;
determine which subsystem is actually expensive before touching it.

## Measure work-vs-budget ratio before optimizing anything

Before reaching for any bottleneck below, measure real per-frame work
against the frame budget the KPI implies (e.g. 66.67ms at 15fps) and
determine which regime you're in:

- **Compute-bound** (work is close to or over budget): a genuine
  performance problem. The bottleneck list and strategies below apply.
- **Slack** (real work is comfortably under budget, e.g. dossage/Passage's
  ~42.5ms of work against a 66.67ms budget): **a pacing problem, not a
  performance problem.** No amount of further render/audio optimization
  moves the number — see `docs/timing.md`'s pacer-pattern and
  `SDL_Delay()`-overshoot material instead.

Getting this wrong costs real time: an earlier campaign on the same port
was legitimately render-bound, and that framing persisted into a later
investigation where the game had already become slack-bound — hours were
spent reasoning about render cost against a problem that had already
changed character. Re-measure the ratio at the start of *every* new
optimization pass, don't assume last time's regime still holds.

**A loop-only reading can say "slack" while nearly half the frame is
being eaten by work the loop never sees.** The spans a main loop
instruments itself -- render, blit, present, tail -- exclude whatever
this backend's cooperatively scheduled audio pump does *inside the
pacer's own `SDL_Delay` sleep*. That cost stays invisible right up until
it grows large enough to consume the sleep entirely, at which point it
spills into frame time all at once instead of showing up gradually in
the loop's own numbers. dossage/Passage hit exactly this on the 486DX2-50
tier (`docs/BENCHMARK-PLAN.md` there, "Methodology lesson from this
gate's failure mode", 2026-09-04): audio conversion was taking ~43% of
every frame while the loop-only spans still read healthy, and after that
was fixed a much smaller version of the same blind spot (the
silence-detect throttle, see `docs/audio.md`) still cost 0.08-0.18 fps
against the KPI line. So the compute-bound gate above has to be re-run
per CPU tier and per audio configuration with the cooperative background
work counted: either a diagnostic build that instruments the sleep/yield
span directly, or an A/B pair across whatever toggle changes that
background work's volume. A clean "slack" reading from loop spans alone
is necessary, not sufficient.

## Likely DOS bottlenecks, roughly in order

1. Full-screen pixel copies.
2. Pixel-format conversion.
3. Alpha blending.
4. Software scaling.
5. Compressed audio decode.
6. Audio mixing.
7. Excessive heap allocations.
8. Floating point.
9. C++ container churn.
10. Filesystem calls.
11. Sprite/collision loops.
12. Cache-unfriendly object layouts.

## High-value strategies

- Native framebuffer resolution (see `video.md`).
- 8/16-bit rendering instead of 32-bit when practical.
- Dirty rectangles, especially for mostly-static-screen games.
- Preconverted graphics and preconverted audio.
- Hardware MIDI instead of software synthesis or decode (see `audio.md`).
- Static lookup tables, fixed-point math.
- Object pooling; avoid per-frame allocation.
- Reduce intermediate SDL surfaces; cache decoded assets.
- Compile-time removal of unused subsystems.

## Confirmed real-hardware findings

**An optimization validated on one fixture can be a real regression on
another — build a fixture per workload class before trusting any gate.**
A dosags patch reordered a present-path dirty check to win a palette-only
present, measurably helping an *idle* 8-bit benchmark and becoming one of
five patches behind a claimed performance gate. On 8-bit *moving* content
the same patch cost **+0.60 ms per render pass** (`present_dirty_check`
4.86 → 5.46, fps 40.12 → 38.91), which was the entire margin between
passing and failing the next gate. Nothing was wrong with the original
measurement; the fixture simply didn't contain the case the patch hurt.
Two things make this worth generalizing:
- **It was invisible until a moving-content fixture existed.** The plan
  of record had already named this exposure, recording that workload as
  "inferred, not measured" — a correctly-priced risk that then came due.
  Write that phrase down when you accept such a gap, so the eventual
  finding reads as a known cost rather than a surprise.
- **The fix is to preserve both paths, not to revert.** A patch that helps
  one workload and hurts another is a signal that the cheap path and the
  expensive path both need to exist, selected at runtime. Reverting just
  moves the regression to the other fixture.

**Don't accept a plausible mechanism just because its magnitude matches.**
In the same investigation the obvious explanation — a cheap short-circuit
test moved after an expensive comparison — predicted almost exactly the
0.60 ms observed, and was still wrong: a counter in the same logs
(`presents_palette_only=0`) proved the short-circuit could never have
fired, so both code arms did identical work. A matching magnitude is weak
evidence, because on a fixed-size buffer at a fixed bandwidth *many*
candidate mechanisms predict the same number. Find a counter that
discriminates between the candidates and read it before writing the
mechanism down — the bisect result stands on its own; the story attached
to it needs separate evidence.

**In a pacing-limited loop, only work that makes the loop LATE converts
into clock.** Removing work that fits inside the existing slack changes
nothing measurable in the rate — it just widens the slack. This is the
single most common reason a saving measures far below its projection on
this class of target, and it caught one campaign three times in a row on
the same estimate.

That campaign found its diagnostic logging was costing ~1.2 s of a 127 s
run and predicted the removal would be worth 0.10-0.25 in tick rate. It
then revised the arithmetic upward to 0.37 after finding more writes.
**Measured: +0.05.** The loop's steady-state tick rate was 40.01-40.06 in
*both* arms — pacing-limited, not work-limited, for most of its length.
Removing 24 batches of ~26 ms grew the slack inside each tick and showed
up as **skipped frames falling from 12 to 4**, not as time. The only part
that converted was the work that had actually made the loop late: a
single 270 ms stall, 0.2% of the run, worth 0.08 — against +0.05
measured.

As the author put it: both revisions answered the wrong question. Not
*what does the instrument cost*, but **what fraction of its cost is being
paid out of slack.** Ask that before quoting any saving on a paced loop —
and note the corollary, that a saving invisible in the rate can still be
real and valuable where it lands, here as a two-thirds reduction in
dropped frames.

**And the inverse is a measurement technique worth reaching for: an
UNPACED region reports every millisecond it is given, so it is a
higher-resolution instrument than a paced one.** The same campaign
noticed this from the other end — the reason moving work into startup
*looks* free is that startup has no pacer, so the cost that the loop was
partly absorbing becomes fully visible there. That is a liability when
choosing where work should live, and an asset when trying to price it:
**to measure something a paced loop's slack is hiding, measure it
somewhere unpaced.** It is also why that campaign's one clearly
reproducible small signal — a 200 ms difference between arms — was
visible in its startup phase and nowhere else.

**The recurring shape of a real win: general-case code doing per-unit
bookkeeping for a per-unit quantity that is usually zero.** Three of one
campaign's four largest wins had this form, and none of them were slow
code — all were work that did not need doing at all:
- A full-frame buffer-to-buffer copy whose output nothing consumed
  (+9.64 fps).
- A full-screen present pushing 230,400 pixels when 18,986 had changed
  (+12.98 fps).
- A black-region fill running a 36-byte `memcmp` per scanline to merge
  identical rows — 360 calls a frame — for a region averaging 1,177
  pixels, whose actual filling cost 0.06 microseconds. Testing the span
  count before the comparison took that term down 79%, with provably
  identical output.

The pattern is worth carrying between ports because it predicts where to
look: an engine written for the general case pays per-scanline or
per-element setup unconditionally, and a port that runs one specific
content class at one resolution often makes that quantity zero almost
every frame. **Measure the quantity the bookkeeping is bookkeeping for**
— if it is usually zero, the guard is free and the win is the whole
scan.

**"We have exhausted the identified work" is a statement about the
search, not about the territory.** A campaign reported that its target
frame rate was "not reachable by the render work we have identified" —
literally true, and badly misleading, because everything identified was
*efficiency* work while the actual cost turned out to be *volume*: a
single 460x260 GUI element, covering half the screen, being recomposited
on frames where nothing about it had changed. Nobody had looked for it,
and the sentence implied they had.

The phrasing matters because the two readings drive opposite decisions —
one says stop, the other says look somewhere new. **State it as "we have
exhausted the work we identified, and nothing has examined X"**, and name
X. In this case X was "why the slow frames are slow", and asking it
turned a wall into a bounded object with a measured fingerprint inside
two rounds.

The related diagnostic habit: when several cost terms are *all* elevated
but by different factors, that is not "the frame is uniformly slow" — it
is one upstream cause propagating. Here compositing 6.3x the normal
sprite area blew the dirty rectangle out to 80% of the screen, which
stopped the present narrowing and enlarged the background restore. Three
elevated terms, one object. **Look for the single cause that explains the
*pattern* of elevations before treating them as separate problems.**

**Look for redundancy in unexamined code before buying efficiency in code
you understand.** In one dosags campaign day the two kinds of win were
measured side by side on the same fixture: finding a *redundant*
operation in a never-examined path (a full-frame buffer-to-buffer copy
whose output nothing consumed) was worth **+9.64 fps**, while the best
micro-optimization of an already-understood path (un-inlining a hot
function to shrink its cache footprint) was worth **+1.20**. An 8x
difference, and not a coincidence: **unexamined code can contain
redundancy, whereas a path you already understand can only be made more
efficient.** When a frame breakdown offers you a term nobody has looked
at and a term you already know how to shave, take the unexamined one
first -- and look there for work that is *unnecessary*, not merely slow.
Ask what it does, how many times, over how many bytes, and whether any of
it is repeated. The same day produced the counter-case too: a term that
*was* understood yielded only percentages, exactly as this rule predicts.

**Before building an optimization mechanism, check whether both halves
already exist and are simply unconnected.** A dosags session scoped
engine-level dirty-rect tracking as a substantial change to the AGS
render path. It was not. AGS's dirty-rect system was **already compiled
and live in the tree** — the port had never touched it, and it was
already repainting room backgrounds over dirty spans only. The receiving
half was also already built: the SDL3-DOS backend has honoured partial
rect lists since `shared/patches/sdl3-dos/0033`, and the engine's own
header said so outright — the backend *"never receives a real partial
rect list because AGS never provides one."* Two finished halves, on
opposite sides of the port boundary, neither aware of the other.

This is a structural hazard of the porting model rather than an accident:
a port layer and a mature engine will often implement the same capability
independently, and the port's patch series is the only place anyone looks.
**Grep the engine for the capability before designing it**, and grep the
backend for a code path that is present but never exercised. The cheapest
optimization available is one that is already written.

**At three instances in a single campaign this stopped being bad luck.**
The same day also found a letterbox fill already done once and cached
rather than repeated per frame, and an eager rect coalescer already
merging touching and overlapping rectangles at add time — the latter
discovered only after modelling a greedy merge pass whose best case was
0.19 ms, because the cheap merges had all been taken already. **Extend
the rule past "does this exist" to "what does the existing mechanism
already do":** before pricing an improvement to a component, read that
component. An optimization proposed on top of one you have not read is
being priced against an imaginary baseline.

**Rect-limit every stage of the pipeline, not just one.** dossage/Passage
found `SDL_BlitSurface(..., NULL, ..., NULL)` and `SDL_UpdateWindowSurface()`
(which builds an implicit full-window rect internally — confirmed by
reading `SDL_video.c`, not assumed) both running full-surface every frame
regardless of how little of a small, centered sub-image actually changed.
These are two separate calls, each needing its own rect argument — dirty-
rectangle work on one does nothing for the other. Limiting *both* to the
same sub-rect (falling back to full-surface only on a frame where
something structural changes, e.g. a resize) cut real-hardware frame time
roughly in half combined — the two largest single wins in that
investigation. The present-side fix needed no shared-backend change: it
relies on `DOSVESA_UpdateWindowFramebuffer`'s existing per-rect VRAM-copy
loop (`SDL_dosframebuffer.c`), reached via `SDL_UpdateWindowSurfaceRects`
instead of `SDL_UpdateWindowSurface` — calling the right existing API, not
new plumbing. Before trusting a "dirty rectangles" optimization is
actually working, verify every stage between your draw calls and the
screen is rect-limited, not just the one you remembered to change first.
**Caveat found later (dosags, 2026-09-17): that per-rect loop exists only
on the backend's banked path and its `SDL_HINT_DOS_ALLOW_DIRECT_FRAMEBUFFER=1`
path. The normal LFB path ignores the rect list and copies the whole
surface** -- check which path your mode-set log says you are on
(`use_lfb`, `banked`, `hint_direct_fb`) before spending effort on dirty
rectangles. See `video.md`, "No low-res mode on the card".

**Float/double-to-int conversion costs a real rounding-mode dance on
486-class CPUs, not just the arithmetic — and this is invisible under
DOSBox-X.** dossage/Passage found a hot per-frame function doing three
`(unsigned char)(double)` truncations per call, 2400 calls/frame,
*unconditionally* — even on a cache hit reusing an already-computed
value. Moving the truncation to happen once at cache-population time
instead of once per reuse, and switching the hot per-call path to
fixed-point integer math (scaled-by-256 multiply+shift), cut render-loop
cost ~29% on real 486DX2-50 hardware. **This cost showed zero
DOSBox-X-visible magnitude difference beforehand** — it was invisible in
emulator testing and only surfaced as the dominant remaining cost once
earlier, unrelated fixes cleared everything ahead of it in the profile.
Another concrete instance of "DOSBox-X is a correctness instrument, not a
performance proxy" (`docs/testing.md`). Prefer converting a value to
integer once and caching the integer, not re-truncating a cached
double/float on every reuse — the x87 `fldcw`/`fistp`/`fldcw`
rounding-mode-change dance behind a naive C truncation is real,
measurable cost on this CPU class independent of the arithmetic itself.

**486DX2-66 + S3 ViRGE/Cirrus result, campaign closed.** dossage/Passage's
real-hardware fps campaign closed at **14.936/14.932fps** (measured twice
independently), up from 13.992fps at the campaign's start -- reported
honestly short of the 15.000fps target rather than tuned to cross it. Run
`/sdldos:benchmark` for the campaign checklist this result came from (delay(1) granularity,
`DOS_Yield()` cost, clock-rate mismatch, and audio-refill were each tested
and eliminated in turn; the real mechanism was the frame limiter's own
deadline-vs-actual error, fixed to land within microseconds; the residual
gap to 15.000 is audio-buffer-refill-driven stalls, root cause not yet
identified as of this writing).

**You cannot compensate your way past stalls by tightening the pacer's
target -- stalls are the binding constraint, and squeezing the budget
manufactures more of them.** The last experiment in that campaign
shortened the pacer's deadline by the measured mean overshoot (0.293ms/
frame), on the sound argument that targeting exactly `1/lockedFrameRate`
makes 15.000 unreachable by construction (a stalled frame is a permanent,
unrecoverable loss). **The mechanism worked exactly as predicted** --
steady-state rose from 14.9989fps to **15.089fps**, above target, pacer
still landing within 0.03ms of its (tightened) deadline. **The run
average moved 14.936 -> 14.942fps. Nothing.** Tightening the deadline
also converts more ordinary frames into missed/stalled frames, and every
stall is a pure, unrecoverable loss -- measured stall cost more than
doubled (0.279 -> 0.654ms/frame), consuming exactly what the tightening
bought. Reverted; the reverted binary is byte-identical to the
already-validated build, and the knob is documented in-code so it isn't
re-derived. **The general lesson: don't fight a stall-dominated residual
by squeezing the steady-state target tighter** -- find and fix the stalls,
or accept the average; a tighter target only trades steady-state
performance for more frequent losses of the same total size.

**A change whose mechanism verifiably works can still be worthless --
measure the thing you actually care about, not the thing the change was
designed to move.** The tightened-deadline experiment above is a clean
case study: the metric the change directly targeted (steady-state fps)
moved beautifully and exactly as predicted; the metric that actually
mattered (run-average fps, the KPI) didn't move at all. Reporting
"steady-state now exceeds 15fps" alone would have been true, verifiable,
and measured on real hardware -- and would have been a materially
misleading way to close out a campaign whose actual goal was a 15fps
run average. Before crediting an optimization, confirm it moved the KPI
itself, not just the intermediate quantity it was designed to influence.

**Skip the present entirely when the frame is unchanged -- a cheap
stand-in for dirty rectangles on a mostly-static game.** dosags (AGS,
Trilby's Notes, HW-486-66) cached the last-presented frame and skipped
the blit + `SDL_UpdateWindowSurface()` when the new one was
byte-identical (palette, offset and flip state checked too). On an idle
adventure-game screen **95.9% of presents were skipped**; pre-registered
ABBA: fps_p50 8.57 -> 21.05. It is not a substitute for real dirty
rectangles -- the moment anything moves, every frame pays the full
present again -- but it is a few dozen lines and needs no engine
knowledge. (dosags patches 0091/0092.)

**DJGPP libc's `memcmp()` and tiny `memcpy()` calls are slow enough to
decide a frame budget.** Disassembling the toolchain's `libc.a` showed
`memcmp` is a byte-at-a-time loop (no `REP CMPSB`, no word widening).
Replacing a 64 KB frame compare with a 32-bit word compare (exact, not a
hash -- a `Uint32` is equal iff its four bytes are) took fps_p50 22.01 ->
34.62 in a pre-registered ABBA, 4x the predicted effect, because on a
fixed-timestep engine saved time also removes catch-up ticks. The same
port's first "fast blit" showed ZERO real-hardware gain until a
per-replicated-byte `memcpy()` (~153,600 one-byte calls per frame) was
replaced with direct stores. Verify a hot inner loop compiled to what you
think it did before crediting or blaming the algorithm.

**Label every fps number with how many presents were real.** After the
skip-unchanged change, dosags's headline `fps_p50=34.62` counted render
passes on a static screen: 23 of 881 frames actually reached the card,
and a real present still cost ~76 ms (~13 fps if everything changed).
Both statements are true; only one describes gameplay. Emit
`presents_real`/`presents_skipped` beside fps, and never quote a
static-screen figure as a gameplay figure. The same applies to a
whole-run tick rate that silently averages in one-time stalls: report a
steady-state figure AND list the stalls.

**A build fingerprint derived from the COMMITTED tree cannot see an
uncommitted working-tree change -- so an A/B pair built that way can
stamp identical identities and silently pass its own invalidity check.**
dosags derives `binary_sha12` from `git rev-parse HEAD^{tree}` over its
vendored trees plus the build flags (its `Makefile`). A worker built the
A arm by reverse-applying a patch to the working tree: it compiled the
older code but stamped the NEWER commit's identity, and both ABBA arms
came back with the same fingerprint. "binary_sha12 must differ between
arms" is one of this hub's standard invalidity conditions, and it would
have passed. **Build each arm from a genuine checkout of its own commit,
not by patching the working tree**, and treat two arms sharing a
fingerprint as a build error rather than as the check being satisfied.
The same gap applies to any fingerprint built from committed state --
git describe, a tag, a submodule SHA.

**The fix, built and proven after this trap bit a third time** (dosags
patch 0115) -- four parts, each of which earned its place:

1. **Fold the working tree's state into the fingerprint, using a
   CONTENT-sensitive input.** `git diff HEAD | sha256sum`, not
   `git status --porcelain`: porcelain lists changed *paths*, so two
   different edits to the same file produce an identical string and
   collide with each other -- the same silent failure one level down.
   Demonstrated on the real tree: two different edits to one file now
   stamp `6e8a3e324187` and `c4e779a6d7a9`, and reverting returns the
   stamp to its clean value exactly, so it is a function of content
   rather than of history.
2. **Hash untracked files' CONTENTS if the build can reach them.** Check
   how your build lists sources: dosags names its engine sources
   explicitly, so an untracked `.cpp` could not compile -- but an
   untracked `.h` can be included by one that does.
3. **Emit dirtiness as its own field, not a suffix.** `binary_sha12` was
   fixed-width in the manifest struct, so a `-dirty` suffix would have
   truncated. A changed hash alone also tells a later reader only that
   two builds differ, never *why*.
4. **Absent the flag, report UNKNOWN, never CLEAN.** dosags reports `-1`
   when the compile flag is missing, so a future build stage that drops
   it cannot be read as a clean build. **A missing signal must not
   default to the reassuring value** -- that is how the original failure
   mode gets rebuilt in a new place.

And the part that actually prevents recurrence rather than diagnosing
it: **a pre-build check** (`make build-dirty`) that reports the stamp
and dirty state *before* an arm is built. All three original
occurrences were caught from a cell's manifest afterwards, which is too
late to save the cell. **Prefer a check that refuses over a check that
records.**

**Stage every config input explicitly; ambient state left on the target
is a silent confound.** The same campaign voided a cell because the
machine still carried an `ACSETUP.CFG` from an earlier zero-patch
experiment, so the A arm ran with the treatment already applied -- two
samples of the same state, the failure mode two entries below. It was
caught only because the A arm's number resembled the B arm's prediction.
A benchmark cell should write the config it depends on rather than
inheriting whatever the previous run left behind.

**Instrument at the choke point the code must pass through, not at the
call sites your own analysis enumerated.** dosags needed to know whether
a 1351 ms precomputed table was ever actually used. It had read every
caller and concluded almost none could reach it at 8-bit -- and then put
its counters at the two functions every consumer must pass through
rather than at the callers it had enumerated, on the grounds that
"trusting my own caller reading would make the instrument only as good
as the analysis it exists to check." That is the right instinct
generally: an instrument placed where your analysis says the action is
can only confirm your analysis, while one placed at a genuine choke
point can refute it.

**Don't pre-register an absolute when the quantity is content-dependent
-- pre-register the mechanism or the ratio.** The same table cost
1351 ms on one game and **4699 ms on another**, same code and the same
65,280 iterations: the inner search exits early on exact palette
matches, so the cost is a property of the game's palette, not of the
engine. A threshold written as "expect ~1351 ms" would have recorded a
large miss on the second fixture for reasons having nothing to do with
the change under test. Where a quantity varies with content, state the
prediction as the mechanism ("call 1 builds, call 2 reuses, saving is
whatever call 1 cost") and let the absolute fall out of the measurement.

**A once-per-run summary metric cannot show that a per-event cost
recurs -- and will make a recurring cost look like a one-off.** dosags
bracketed a 1351 ms room-load phase, but the bracket was reported in a
startup summary emitted once, at first-room-ready. A run that paid the
cost twice (a save-restore re-enters the same room-load path) reported
exactly the same single figure as a run that paid it once, so the
bracket built to find that cost would have hidden its most important
property. Fixed by emitting one line per occurrence, with a sequence
number and a cheap identity (here a palette hash) so repeats are
visible and attributable. **If a cost can happen more than once per
run, count the occurrences -- a total or a first-occurrence figure is
not enough**, and check where your report is emitted from before
trusting what it does not show.

**Instrumentation bugs that hid real signal in one campaign, worth
checking in any per-phase timer:** (1) truncating each sample to whole
milliseconds *before* summing -- nine phases reported exactly 0 ms
across 10,000+ calls; sum in the clock's native units and convert once.
(2) a whole-function bracket on a function with call sites outside the
path being measured summed to MORE than its parent -- an impossible
subset is a free tell; bracket the call site, not the callee. (3) totals
for a phase that can re-enter itself (a blocking script wait that runs
nested game ticks) double-count; measure self-time, or the arithmetic
built on those totals is wrong. (4) the measured cost of the timing
mechanism itself was 11 us per bracket -- cheap enough, but measure it
once rather than assume. (5) changing a recorder's unit without updating
every call site: the fix for (1) moved the recorder to microseconds, two
hand-rolled call sites kept passing milliseconds, and for a day the
script VM read 1000x too cheap -- which sent the investigation hunting
"zero-work wrapper overhead" that was really script time. The tell was
in the data the whole time: a parent phase with max 2605 ms wrapping a
child that claimed max 2 ms. Put the unit in the parameter AND the local
variable names, and sanity-check child-vs-parent maxima after any change
to the timing code itself.

**Pick the iteration vehicle for speed, the validation vehicle for
truth.** dosags spent most of a day on 20-70 minute real-hardware cells
because its only 32-bit content was a 72 MB game. Forcing a small,
fully scripted game onto the same code path with an existing
depth-override lever reproduced the same relative behaviour in 4-8
minute cells. Develop against the fastest fixture that exercises the
code path; return to the heavy one only to confirm.

See also `video.md` ("No low-res mode on the card") for the largest
structural lever found so far -- presenting unscaled instead of
scale-to-fill -- and `timing.md` for the 1.3 ms clock read.

## Measuring on real hardware and in the lab: three rules from dosags (2026-09-24)

**An unscored warm-up cell after every power-on.** The first cell after
the target machine powers on reads off: it doesn't match the cells that
follow on the same build. This is an operator rule for dosags: every
round that powers the target on runs one warm-up cell first. It uses the
same fixture and build, is recorded as "warm-up", and is never scored.
Scored cells start with the second run. The same holds for any port's
campaign, and a pre-registration should list the warm-up cell explicitly
so it can't later be counted or dropped by choice.

**Fixed-cycles DOSBox-X is deterministic, but blind to 486 CPI and code
layout.** At fixed cycles the emulator executes a fixed number of
instructions per emulated millisecond. It sees how many instructions a
change removes, not what each one costs on a 486. Cache and
prefetch-queue behaviour, alignment, and where hot code lands relative
to other code are all invisible to it. HYPOTHESIS, pending an ABBA
confirmation on hardware: a code-layout change alone (no change in
instructions executed) moved a 486 result by about 0.5 ms per tick.
Until that is confirmed or refuted:
- rank candidate changes in the lab at fixed cycles;
- decide on hardware;
- don't credit or blame a change for a sub-millisecond hardware delta
  that an unrelated layout shift could also explain.

**Real-mode calls are invisible to fixed-cycles DOSBox-X.** A DPMI
program's call into real mode (INT 33h mouse, INT 16h keyboard BIOS,
INT 21h DOS) costs a protected-to-real-mode round trip plus the real-mode
handler itself. dosags measured it (LAB-MMD 26/27, lab/mmd 9a853b01 and
89c0a10b):
- **Per tick:** AGS's steady tick runs 3 SDL event pumps. Each pump makes
  3 INT 33h calls in dosags' build: fn 3, plus fn 5 twice (from dosags'
  own SDL 0220). That is 9 real-mode calls per tick with no input at all.
  The hub's reference `DOSVESA_PumpEvents` makes 1 per pump (fn 3, plus
  fn 0Bh in relative mouse mode).
- **On the 486:** the input phases run 5-6x slower than at fixed cycles,
  against 2-3x for the rest of the logic.
- **In DOSBox-X:** every real-mode call costs about the same ~15 us,
  mostly the DPMI round trip, because the emulator answers INT 33h
  internally. So fixed-cycles timing makes these calls look cheap and
  uniform.
- **The 486 cost is an INFERENCE:** about 0.2 ms per call, derived from
  the phase timings. A microbenchmark on the target is pending.

The rule: on any hot path, count the real-mode calls per frame or tick
(INT 33h, INT 16h, INT 21h), and don't trust fixed-cycles timing for
them. Measure them on hardware, or remove them.

A candidate design direction, not a result: event-driven mouse input.
INT 33h fn 0Ch installs a user event handler, reached through a DPMI
real-mode callback, so the driver reports changes instead of being polled
every pump.

**Worked example: confirm the KPI, not the intermediate metric (dosags
"D2").**
- **The finding.** SDL's audio silence throttle -- the `SDL_Delay(10)`
  in the SB driver's silence gate -- was running inside the app's own
  `SDL_DOSAudioPump()` call on the main thread. (The hub's reference
  series still has that throttle in `PlayDevice`.)
- **The fix.** dosags' SDL 0213 v2 stopped it sleeping there.
- **The intermediate metric moved a lot.** On the 486 at 16 bpp, the
  pump's time fell from 8.1 ms to 0.4 ms.
- **The KPI barely moved.** The tick rate went from 39.000 to 39.135:
  +0.135, against 0.02 of N-N noise. That is real, but the
  pre-registered target was FALSIFIED.
- **Why.** The sleep had mostly fallen in time the loop would have spent
  idle anyway. The remaining limiter is load stalls.
- **The lesson.** An 8 ms saving in the metric the change aimed at
  bought 0.135 ticks per second, which is exactly the gap
  pre-registration against the KPI is there to catch.
- **Source.** dosags lab/metric ace672be, LAB-S32 19.12.

## Setting a performance KPI

Write a KPI down *before* chasing it, in a form that can't be satisfied by
a metric that's technically true but practically misleading — a real
campaign on this hub nearly shipped exactly that failure mode more than
once (see `docs/timing.md`'s self-lying-metric entry). A well-formed DOS
port performance KPI:

- **Is real-hardware only.** DOSBox-X/86Box are correctness instruments,
  never a performance proxy — see `docs/testing.md`.
- **Is percentile-based, not a single average.** Require both a central
  figure (`fps_p50`) and a worst-case figure (`fps_p95` or an explicit
  "no single window below X" floor) via `shared/include/runmanifest.h`'s
  fields. An average alone can pass while hiding a real excursion —
  exactly what happened when a debt-repayment fix improved the average
  while momentarily running the game visibly too fast right after a
  stall. "Solid," not just "clears the average," is the actual bar a
  release needs to meet.
- **Is falsifiable, decided in advance.** Write down what result means
  fail, not just what result means pass, before the first real-hardware
  run — same discipline `dos-hardware-validation`'s ABBA methodology
  already requires for an A/B comparison; a KPI is a comparison against a
  fixed target, not a different kind of claim.
- **Is attributed to a specific validated build.** Cite `build_sha12` (or
  equivalent), not "the code we currently have" — a KPI result that can't
  be reproduced against a named commit isn't a result, it's a claim.
- **Has an independent witness available**, not just the engine's own
  self-report — see `docs/timing.md`'s self-lying-metric entry for why a
  metric computed from a clock the change under test can perturb will
  tend to fail in the flattering direction, not an obviously wrong one.
- **Is reachable by the fixture you will measure it on.** Check the
  *content's* own ceiling against the bar before writing the bar down. A
  dosags gate required 40 fps on a hand-authored benchmark room whose
  sprites animate at AGS delay 1 — one new frame every 2 ticks, so
  **exactly 20 distinct frames per second, by authorship**. No amount of
  optimization could ever have displayed a 21st. The bar was unreachable
  by construction and nobody noticed for the length of a campaign,
  because the metric being scored (`fps_p50`) counts render passes, which
  *can* reach 40 while half of them display nothing new. Worse, 20 fps
  animation is the *ordinary* authoring choice in AGS, so the bar was
  also testing something no real game in the genre does. Two rules fall
  out of this, and the second is the one that generalizes furthest:
  - **Pair any fps bar with a completeness ratio** — displayed frames
    over frames the content actually generated. A render-pass count
    cannot distinguish "fast with headroom" from "dropping content," and
    on this campaign that ambiguity hid a 98%-versus-74% difference
    between two presentation paths that the fps column made look like
    pure headroom.
  - **Count content frames upstream of the dirty check, not from it.** If
    the completeness numerator and denominator both come from the same
    skip-unchanged test, the ratio reads 100% by construction and
    measures nothing. The denominator has to be incremented where the
    content changes, before any coalescing.

## Rule

Do not make a performance claim without a measurement, and prefer a
real-hardware measurement over an emulator one for anything you intend to
publish in `gallery/` or `COMPATIBILITY.md` — see `docs/hardware-testing.md`.
