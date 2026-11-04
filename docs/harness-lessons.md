# Harness lessons: incidents behind the invariants

The checklist in
`shared/skills/dos-hardware-validation/references/harness-invariants.md`
is one line per rule; this file is the incident each line was earned
from, condensed to what happened and what it taught. Sections mirror
the checklist's headings, dates are when the lesson was recorded in the
skill (the incident is from the same round), and none of this is
normative. Entries dated 2026-08-26 came in with the standard from
doskutsu; the rest are from one port's CF-card write-stall and
frame-pacing campaigns on a 486-class target, driven over vcctrl.

## Checks, witnesses and gates

**The REM that redirected** (2026-08-26). An audit for unescaped `>`
found real hits, but the worst was a `REM` whose quoted example of the
bug still redirected, so documenting the fix recreated the file it was
meant to stop creating. Ask what a line would do, not what it matches.

**The stall detector that could not see a dead capture** (2026-08-26).
A frame-diff detector compared each signature with the previous one; a
dead capture yields null every time, null never equals null, so a wedged
screen scored as "activity" for minutes. The runner stdout that recorded
it was then deleted in a "clean up before the next run" pass.

**534 failures, 22 real** (2026-08-26). A lint gate reported 534
failures, 512 from two bugs in its own parser, and went unused until
fixed (25) while its existence kept the bug class looking covered.

**The precondition that fired fourteen minutes late** (2026-08-26). A
correct VBE-provider assertion was applied to returned logs after a
14-minute run whose provider had silently declined to load. A post-hoc
check passes review because its defect is only visible on a calendar,
and a grep over a log is cheaper to write than a live check.

**Two noise bands** (2026-08-26). Within-session repeatability was
0.0-0.1 fps; the same nominal config a day apart differed by up to 0.85
fps. Comparing across the bands manufactures results, and an apparatus
change validated against an absolute banked figure cannot fail.

**114.9 fps on the slowest cell** (2026-08-26). `flips / (duration -
overhead)` read 114.9 fps on the wall-clock-slowest cell because 82% of
its time was one-time load stall, which the formula treats as free.

**The win that did not reproduce** (2026-08-26). An improvement shipped
default-on from a solid-looking result and did not reproduce; the
binary carried more than one new mechanism, so nothing could be
attributed afterwards.

**0.81x to 286x** (2026-08-26). Emulated versus real timing of the same
path in the same binary ranged from 0.81x to 286x by code path. An
emulated sound chip also "succeeded" a reset real hardware fails, so
"9/9 smoke cells passed" meant 9/9 safe fallback branches.

**The hole the gate counter could not hear** (2026-09-30). A patch that
wrote a new sound's first mix chunk early passed its registered tests
and a full ratchet, then left a ~46 ms zero-filled hole ~186 ms into
every cold-started sound: the next top-up mixed one chunk past the data
that had arrived. The SB played the zeros as ordinary ring data, so the
gate's silent-IRQ counter stayed at 0; the mixer's own `underruns`
counter equalled the prime count in every run and nobody gated it. A
capture check on that binary passed too, because its gap test started
at 0.12 s. It was caught by a lab session reading a counter that moved
between binaries. Gate every counter a patch can move, not only the one
the patch was written to improve; the ratchet now requires underruns 0.

**A test that could not fail** (2026-09-30). The pre-registered test
for the fix above ran the known-buggy build as a control arm, with a
rule set before data: if the buggy arm scored the same as the others,
the result was "PASS without power", not a pass. Twice the control
matched (the stall never reached the bug path), and the third, forced
arm reproduced the bug before the fix was believed. Run the known-bad
build under the same test, and decide in advance what it means when it
passes.

**A semitone nobody measured** (2026-09-30). The shared OPL3 sink
played every note ~89 cents sharp for weeks (an AdLib F-number table
that starts at C#, indexed from C), through gates that scored timing
and counters, never pitch. A capture scorer comparing the recording's
spectrum with the song's render read +90 cents on OPL3 and -2 on a
WaveBlaster capture of the same scene. Uniform transposition keeps music
recognisable, so no listener flags it; measure against a render, with a
second synth as the control.

## Before designing a cell

**The bisect that checked out the wrong patch** (2026-09-18). A commit-
matching regex also matched an unrelated `dos: ` subject and silently
checked out the wrong patch level; only an unrelated patch-count
assertion caught it. Dirt folded unconditionally into fingerprints then
made clean historical builds unreproducible.

**A `noinline` on a function already out of line** (2026-09-19). Tested
first on a function the compiler had already placed out-of-line (a no-op
build read as a null result), then on a fixture where the function never
runs, a gate already in the campaign's own notes. Symbol count, an
execution counter (`presents_skipped=0`) and a grep of your records.

**The answer was on disk four times** (2026-09-19). In one day: a gating
constraint in prior ABBA notes, bandwidth figures refuting an exotic
theory, repaint counts settling a hypothesis, and 61% of a run's wall
time inside 19 ticks in a log collected an hour earlier. Separately, a
25-point gap was blamed on startup when the metric's clock began after
load and the pre-loop was 0.9% of the run.

**The four-day-stale SDL library** (2026-09-20). A stamp-gated `make`
rebuilt the engine against a four-day-old SDL with a green log; the new
counters would have returned exactly the zeros the hypothesis predicted.
`nm` was then found to return zero matches on DJGPP for symbols that
demonstrably fired; the real check both times was a counter printing
non-zero.

**417 frames of rects, 60 rows too high** (2026-09-19). A dirty-rect
change refreshed the wrong strip (game-coordinate box tested against
window coordinates across a letterbox margin) while every backbuffer
hash passed. The judged capture path then refused a correct static room
as "every frame was a duplicate"; the raw burst path is the tool.

**Camera for invalidation, hash for compositing** (2026-09-20). Stage 1
of the fix altered invalidation and never touched the back buffer, so
only a camera was evidence. Stage 2 altered compositing, and the buffer
hash caught a rendering defect the camera would have passed and no
timing number would have shown.

**The gate that could not see it, a third time** (2026-09-21). Both
entries above were already written down when a campaign's standing gates
(every fixture arm at both depths, and the demo harness's three captures,
all compared by hash) turned out to read the engine's own RAM frame:
`SaveScreenShot` copies the virtual screen, never what the present pushes
to the display. Every arm passed. The operator, watching the monitor
during ordinary hardware cells, saw chains of whole stale sprite copies
and leftover speech text on two different binaries; a capture-card loop
during a later cell reproduced it while the engine's own screenshots from
the same cells were clean. Proven: what the gate reads (from source) and
that the trails exist on the physical screen. Not known yet: the cause,
and which earlier figures came from a renderer that leaves trails (skipped
repaint work is exactly what raises a frame rate). The rule was in the
checklist and in these case studies; it was not in the standing gate, so
nothing enforced it. It only bites once a physical-screen shot at a
pre-registered tick is a step of every moving-content cell.

**`frames_skipped=0` on a path that was off** (2026-09-19). Read as "the
emulator never falls behind"; the fixed-timestep code was disabled
there because the hardware runner set an env var by default and the
emulator runner did not. Every emulator validation behind it had been
vacuous.

**Ticks 603 and 1807 became 803 and 2407** (2026-09-20). A parked
deterministic stall was revisited five patch series later at different
indices; a probe armed at the old ones, which both sessions had planned,
would have read as the phenomenon vanishing. The two pairs satisfied
the same exact relation, which pointed at a mechanism. Four rounds went
this way.

**The RAM disk that bypassed four layers** (2026-09-20). Branches were
written as "no spikes = the CF media; spikes = DOS or the BIOS above
it"; a RAM disk bypasses the driver, INT 13h, the controller and the
media, and the BIOS sits below DOS. The same design verified "a
different volume" when the hypothesis was about an erase block.

**Four guards for one absence** (2026-09-20). A different-medium cell
needed arms matched on elapsed time (the RAM-disk arm covered 0.06 of
one period), the original medium as a positive control, a not-scorable
duration floor, and bytes-written asserted against bytes-attempted
(`fwrite` to a full DOS volume returns short). Four is the count of
ways that run could be quiet, and every one was live.

**Five cells before reading CONFIG.SYS** (2026-09-20). Five mechanism
families were killed before anyone read the machine's `CONFIG.SYS`,
which listed a resident disk cache hooking INT 13h that matched every
observation; `BUFFERS=20` (10 KB) had already argued against a DOS-layer
flush. The cache reconfigures while resident, so no reboot was needed.

**The warm-up that moved the stall out of the window** (2026-09-26). An
exact cache warm-up removed a first-play sound stall from a click tick
in the lab, and its 37 ms landed in the room load. The fixture started
directly in the room, so that load ran in the pre-loop, outside the
whole-run window the gate reads: the counted entry tick did not move
(577.9 vs 577.8 ms) and the gate figure would have risen with no cost
shown. The lab reported it itself because the pre-registration had been
told to say where the moved cost lands. A gain that works by moving
cost is reported beside the cost, and a gate is read on a fixture where
the cost stays inside the counted window.

**Wall-clock clicks on a timeline the arm had moved** (2026-09-28). A
killswitch pair for a faster screen fade cut the entry fade from 2274
to 981 ms and raised the whole-run figure by 0.40. But the fixture's
walk clicks were timed by wall clock, and the shorter fade put the ON
cell's game 1.3 s ahead, so the same clicks landed on different game
states: the cells played different sounds, and only the OFF cell loaded
the largest clip. The fade itself came before the first click and
stayed comparable; the whole-run figure did not. Any arm that changes
timing can change what wall-clock input does, so a whole-run or
silent-IRQ comparison between such arms needs input keyed to game
ticks, or a metric taken before the first input.

## Instrument validity

**`presents_palette_only`** (2026-09-18). A counter on "frames equal AND
palette changed" was reasoned from by two people to opposite
conclusions about the palette flag alone; a direct probe (`palchg
true=2 of 1000`) settled it.

**Three patches in dead code** (2026-09-20). A guard counter read zero;
a `skipped` counter said not-reached; a second site was instrumented;
the function was never called (direct-framebuffer fast path). Call
sites were miscounted three times, and an "unconditional" counter placed
950 lines in, past an always-taken early return, read as "never called".

**17,847,809 us inside a 636 ms tick** (2026-09-19). Phase totals
accumulated across nested loops while self-time excluded them; the fix
printed the parent's accumulator as the children figure; both were
caught only for being impossible. A third bug passed the sum check: a
line of well-formed zeros from a snapshot reported before anything ran.
A legitimate zero total (outer call in flight) then needed a `NESTED=`
label, and a per-frame-cleared `reason` field on per-run counters
printed `-` for 2,124 frames.

**Sub-type 1 was the expensive one** (2026-09-19). A bare enum value,
a `switch` in the opposite order, and a script action pointing at the
cheap sub-type: matching the number against source order would have
reported the wrong cause of the largest remaining cost, at 48,000:1.

**A ramp of 17,681,718 writes from 1,430 calls** (2026-09-21). A
per-cylinder histogram in unzeroed DOS memory rose 513, 1027, 1541
across 704 cylinders and looked like a distribution. Each write is one
`INT 13h` call by construction, so a total above the call counter now
prints `HISTOGRAM VOID`; the probe printing its own total caught it.

**A field inserted mid-header** (2026-09-21). A stub/installer shared
header gained a field in the middle; the installer read the busy flag
as the handler offset and installed an interrupt vector into the data
area. It was visible only because the installer printed the offsets it
resolved.

**Geometry fetched three times and thrown away** (2026-09-21). A gate
called `INT 13h AH=08h` every run and printed the registers only on a
mismatch; three cells collected CHS addresses that could not be
converted to LBA. Three other saves that campaign were passive lines
nobody asked for: MSCDEX's drive banner, resolved offsets, `events=N
bytes=B`.

**The `-1` that could only time out** (2026-09-20). A power-draw
watchdog printed `-1` on any parse failure, and `-1` never exceeds
8,000 mW, so "did not come up within 300 s" could only mean a parse
failure. Three independent readings showed the machine up throughout.

**The 168 ms event under a 250 ms threshold** (2026-09-20). A detector
at 250 ms found a clean ~240 ms mechanism, confirmed three times. A 168
ms event in another phase surfaced only as skipped frames rising from
12 to 17, and the clean mechanism would have absorbed it.

**The fsync inside the flush** (2026-09-19). A ~250 ms present-path
stall survived six hypotheses; it was the port's own diagnostic doing an
`fsync` per hundred presents, 21-36 ms each on CF and one at 227 ms,
over-represented in the present because that is where the writes were.
A bracket-set diff cannot see cost carried by both arms.

## Reading a result

**Three denominators, then a fourth by the rule's author**
(2026-09-19). Nested brackets subtracted from a budget made a "17 ms
unattributed"; 13% versus 24.3% compared different fixtures and inverted
in absolute terms; two tick populations were compared as one. Then the
rule's author quoted 47% from a fixture at four times the pixel count;
on the fixture under test it was 4.9%, the difference between "external"
and "18.7x over-represented".

**94% of an engine event, retracted in one measurement** (2026-09-19).
A scope opened before an `#ifdef` and closed by the enclosing block
swallowed an engine call after the `#endif`, implying 0.78 s per log
line. Its author wrote "absurd on its face" and shipped it; the reviewer
independently derived the contradiction and resolved it with a hedge; it
reached the operator as a retraction of a major finding. Real figure:
1.9 ms, off by 20,000x. It was the fifth "our own redundant work" find
that day and the only one relayed before a bracket had killed or
confirmed it, at a pace the coordinator's instant relay had set.

**The 4.4 ms that was 60% elsewhere** (2026-09-19). A measured sub-term
subtracted from its parent, the remainder attributed to the part whose
name matched, and a hypothesis sent after a term that did not exist;
the named part measured 10%. A coordinator did the same to the frame
budget the same day.

**1,201 ms, of which 900 was removable** (2026-09-20). File I/O traced
to eight log re-opens was projected as a 1,201 ms saving; two opens
were irreducible. That decided whether a gate cleared by 1.0x or 2.8x
its noise, with four cells about to be spent on the wrong one.

**Four monsters and 37 ordinary frames** (2026-09-19). 41 over-budget
frames averaged 9.1% render; split, 37 at 79 ms were 85.8% render and 4
at 8,790 ms were 98.9% events, so the aggregate implied the opposite of
the truth. A dirty-check cost was bimodal the same way (0.60 vs 0.15
ms), and 107 ticks/s was computed on a 40 Hz pacer because overlapping
excluded intervals removed 80% of the wall clock.

**`fps_p50` 36.60, 39.00, 38.12** (2026-09-20). Spread 2.40 on the
percentile; `tick_rate_steady x (1 - skipped/ticks)` on the same runs
gave 38.03, 37.97, 38.00. Nineteen multi-second blocking operations
skewed the distribution, which had been characterised and then judged
against another fixture's floor. A coordinator had already relayed a
mechanism for the "anomaly".

**The stall at the same second in four runs** (2026-09-20). A ~250 ms
stall recurred at the same wall-clock time across four runs (3 ms
spread at 121 s) with the tick index varying, so both sessions looked
outside the program. It was the log file reaching a byte offset (16,103
to 16,861 on two binaries). The FAT-cluster mechanism proposed for it
was later falsified (32 KB clusters, logs never reached 32,768).

**7, 10 and 14 events with nothing varied** (2026-09-20). "Position is
not reproducible" (six of seven matched; the refuting comparison varied
three levers) and "the rate is not stable" both read a missed detection
as a missed event. A lattice fit over the union put every event on one
35.0-record period with `n` 0..6 unbroken, 0 of 2,000 random draws
matching; the third run came from a voided cell mined for what stood.

**28,000 to 19,778 bytes** (2026-09-20). After four families died on
arguable pattern failures, halving the record size moved the period 29%
on the one coordinate a byte counter cannot vary. The per-record cost
was measured in-run (5,244.9 us against 6,419 at 800 bytes, neither
half nor equal), not carried across the change.

**19.4 cycles per pixel, not 2.3** (2026-09-19). "0.70 ms per sprite, a
fast masked copy" confirmed a prior and would have closed the question;
the sprite dimension was invented and the true figure was 8x worse.
Inverting the arithmetic (0.70 ms at measured bandwidth implies 77x77,
not 100x200) needed no instrument.

**The bracket comment that was wrong** (2026-09-19). A months-old
comment said a path did not touch the back buffer; it was the frame's
second-largest term, with the engine author's own 50%-of-CPU warning
above the call. A round later the interesting of two surviving
candidates was written up as the cause; one split counter showed it was
the dull one, an unclipped rectangle worth 417 frames going to 3,253.

**6, 7, 8, 15 in stage order** (2026-09-20). Counts excluded from every
verdict as too noisy were also excluded from every eye; a strictly
increasing sequence surfaced only from a reader of unscored numbers. It
did not survive (comparable runs gave 7, 10, 14, 11, 6), but the
examination is the lesson.

**Four collection failures and a 1.20 fps win** (2026-09-19). Four
consecutive cells returned only a send witness, and the one thing that
had changed looked like the cause. A known-good binary run between them
reproduced its prior figure exactly; the "broken" binary later ran
clean; the transient was never explained. Without the interleave the
conclusion would have been "binaries built this way don't run".

## Shell and DOS traps

**`FSPROBE_SEQ` truncated at the first stage** (2026-09-20). A
`|`-separated stage list arrived truncated and the probe ran one stage,
exiting zero with a plausible single-arm result. "COMMAND.COM reads `|`
as a pipe" was written as mechanism; with `#` the truncation recurred
and the diagnosis was withdrawn on an untested inference. An echo-back
probe then showed `|` and `>` truncate, `%` is stripped silently and
`#` survives: the diagnosis was right and the withdrawal wrong. The
operative cause was neither: a nested `strtok` in the probe's own fresh
source ended the outer loop after stage one with the value intact, and
the one-variable-per-stage fix removed it as a side effect. Old shared
machinery was argued over while the fresh probe went unsuspected, and
the runner already printed "all files verified byte-for-byte".

**`RATE = one event per 56,228 bytes`** (2026-09-20). Three runs of one
configuration printed 56,228, 39,360 and 28,114, because the event
count came from an intermittently sensitive detector. `events=7
bytes=393,600` invites "is seven all of them?"; the ratio does not, and
it carried the instrument's credibility into killing a hypothesis
family.

**The runner edited mid-run** (2026-09-20). A runner script edited
while four cells were invoking it was reverted within a minute; the
running cell died anyway with `rc=127`, resumed inside a Python heredoc
a thousand lines away, after "still running" had been checked. The data
survived only because the daemon's pulled store still held it.

**CONFIG.SYS at 1,307 bytes with zero carriage returns** (2026-09-21).
A fetch that doubled newlines was normalised to diff against a
reference, diffed clean, and the normalised copy was staged for push-
back with 42 CRLF pairs gone. Caught only because a boot file is the one
place line endings are not cosmetic. Re-staged at 1,349 bytes.

**The waiter that waited on itself** (2026-09-26). A background waiter
meant to start a profile run once another cell finished tested
`! pgrep -f "<cell script> <tag>"`. The pattern also matched the
waiter's own command line, so it slept forever after the cell ended and
the run never happened; nobody noticed for hours because a waiter is
silent by design. Wait on the exact PID (`kill -0 $pid`) or on a result
file the job writes last, never on a pattern (the same class of bug as
a pattern kill).

## Boot config, drives and hardware

**`VOL D:` against an empty optical drive** (2026-09-21). A "read-only
verification" cell ran `VOL C:;VOL D:;VOL E:;VOL F:`; `VOL` on an empty
CD-ROM raises `CDR101: Not ready reading drive D` / `Abort, Retry,
Fail?`, keystrokes stopped landing, and recovery was a mains cycle.
MSCDEX had already printed `Drive D: = Driver OPTICAL unit 0` on a
screen the capture tool reads. `verify_input` returned "no LED change",
the post-FTP hang signature, before any screenshot; the documented
order (captures before verify-input) turned "known fault recurred" into
"my command wedged it".

**A starting cluster from an undocumented field** (2026-09-21). A
will-this-write-land-on-free-space check was about to rest on an
undocumented DOS 6.22 FindFirst field, a class of assumption rejected
twice that night under an analysis; under a safety check the cost was
the boot drive of a machine that cannot be re-imaged. The replacement
wrote a pattern through the documented API, computed where it should
live, and read that absolute sector back.

**No `RAMDRIVE.SYS` on this machine** (2026-09-20). `DIR C:\DOS\*.SYS`
showed one file, the cell was declared blocked and a licensing
escalation was pending. It was in `C:\WINDOWS`, beside the
`SMARTDRV.EXE` that a fetched `AUTOEXEC.BAT` loaded; the volume held
eighteen `.SYS` files in eight directories. Command, output and reading
were all correct; the claim's scope was wider than its evidence.

**Six status tables, not four** (2026-09-20). A gate required exactly
four status tables; a correct run emits six because each toggle prints
one, derivable from the toggle's documentation with the results sealed.
Relaxed and declared up front. Hours later a fit missed its threshold
and dropping one point, chosen after seeing which spoiled it, passed by
0.0045. Refused: the justification did not survive with the outcome
column covered.

## Harness and collection

**The bracket nobody fetched** (2026-09-20). A four-phase bracket
written by another port years earlier for this exact symptom was always
on, writing `LOGS\<TAG>SDL.LOG`, and the collection step never fetched
it: recorded on every cell of a multi-day investigation, read on none.
The same `HARDWARE.md` row had named it; the borrowing session had
earlier instrumented the row's guard from a branch this port never
executes and reported the mechanism falsified.

**The config the engine never read** (2026-09-26). Two lab sessions
staged an engine config as `C1X-ACSETUP.CFG` and similar, keeping the
source basename; the engine reads only a file named exactly
`ACSETUP.CFG`, so every cell ran on defaults. One lab chased a
"sound bug" before the staging was found. The runners now
refuse any config whose destination name is not the one the engine
reads (and the reverse, the right source staged under another name)
unless an explicit override flag is given. Stage configs by destination
name, and make the runner refuse the wrong one rather than trusting the
caller.

**Committed before the cell reported** (2026-09-20). While a
discriminating cell was on the hardware, a re-read of four earlier arms
gave a reading materially more favourable to the hypothesis under test
than the one published an hour before. It was committed, with its
reasoning, before the cell reported. That is the difference between an
argument and a rationalisation, and it binds only while a relevant
measurement is already running.
