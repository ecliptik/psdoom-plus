# Harness invariants: why a check is a check

Distilled from this hub's own `/HARNESS-STANDARD.md` (adopted from doskutsu, where it originated); read that for the
normative I1-I3 invariants and the L1-L3 conformance ladder. This file is the practice layer for this hub's own
vcctrl-driven rig, as a checklist. Mechanics live in `cell-protocol.md` and `abba-methodology.md`; the DOS/DJGPP
platform profile lives in `docs/dos-scripting.md`.

Case studies: `docs/harness-lessons.md` (the dated incident behind every line below).

## Checks, witnesses and gates
- I1 Attest, do not recall: every fact about a run lives in an artifact the run produced and the collection step actually shipped.
- I2 Witness the state, not the artifact: a file arriving, a result file existing or a banner printing proves only itself, not idle, finished or engaged.
- I3 Audit for the effect, not the syntax: ask what a line would do, not whether it matches a pattern (a `REM` quoting `>` still redirects).
- Before relying on a check, state what its failure would look like; if that matches success it is not a check, and one that reads a status of unverified provenance (does this program set an exit code on that path?) can fabricate, which is worse than none.
- Verify routinely, not only doubtful-looking claims, and not less right after naming a hazard; the procedure is the defense, not the understanding.
- A watch is a check: say what it emits if its event happened now, never let a null/absent reading enter a "different means healthy" compare, induce its event before trusting it; never infer success from silence, assert the positive consequence.
- A verdict names its subject and what it does not cover; "can the apparatus drive the target" gets read as "is the target configured".
- A retained reading (LED, ready flag, cache) can predate a reboot; across a discontinuity wait for it to go false, then trust the next true.
- Prove the control path at the far end with four states (answered / did not answer / could not look / tool failed), never collapsing could-not-look into did-not-answer; preflight is one command with one exit code (any FAULT, else any UNKNOWN, else PASS), names the deciding check, runs non-invasive checks first, and a held rig lock is UNKNOWN.
- Check preconditions (no stray TSR, the right driver or VBE provider loaded) before the run as an executable check, not prose; a check applied to returned logs is a receipt, worth keeping as a second gate but never the gate.
- Keep declared and detected fields separate: declared is parameterized never hardcoded, detected is recorded verbatim naming the detector (a shim can mask it); a complete result envelope does not mean the cells succeeded, only a captured completion witness does.
- A platform profile is a lint gate whose false-positive rate is a functional requirement; a noisy gate stops being run while still promising coverage.
- A harness must be able to say "I cannot determine this" and escalate to a physical check; a run of plausible causes with no evidence is itself a symptom.

## Before designing a cell
- Grep the last cell for the quantity you are about to measure, and read what a metric's clock is anchored to before doing arithmetic on it.
- Prove the code under test executes on this fixture: symbol count for "distinct function", an entry counter for "reached", your own prior notes for known gates.
- Verify the artifact contains the change with a functional check (an output line only the new code emits): a clean build log is a claim about `make`, `strings | grep` proves compilation not execution, `nm` finds nothing on DJGPP; test the check against a binary that lacks the change.
- Match the gate to the layer the change touches: a present-path or invalidation change needs a capture of the actual screen (VGA capture or camera, before and after), a compositing change needs the buffer hash, both needs both; a liveness-judged capture refuses a correct static screen, so use the raw burst path, where two identical frames are the desired result. Make the screen capture a runner step on every moving-content cell (frames through the timed run, a contact sheet reviewed before the rate is recorded), not a check remembered per patch: a standing gate that hashes the engine's own frame passed while the monitor showed trails.
- Diff every default between the emulator runner and the hardware runner, keep the diff in the repo; code on a timing-gated path needs a forced-execution switch or the emulator's silence reads as coverage.
- Re-confirm a parked observation on the current tree before designing around it; when an index shifts between builds, the shift is data.
- Name the layers a negative result clears before running (write the stack out, mark the rows a substitution replaces), and assert the property the hypothesis is about, not a proxy (a different volume is not a different erase block).
- A cell whose useful answer is "it did not happen" needs one control per way of going quiet, and each guard must void, not score.
- Read the target's own `CONFIG.SYS`/`AUTOEXEC.BAT` at campaign open, not a sibling repo's copy (residents, caches and TSRs are the candidate list; record `BUFFERS=`/`FILES=`), and before a cell around a reboot check for a runtime interface (see `abba-methodology.md` on within-boot A/B).
- One new mechanism per measured build; ablation cells around it are fine, instrumentation and banners riding along do not count.
- Any step that selects an artifact needs an unambiguous key (the SHA in the patch's own `From` line) plus a cheap invariant (patch count, expected `build_sha12`); fold working-tree dirt into a build fingerprint only when the tree is dirty.

- A gain that works by moving cost (a warm-up, a preload) is reported beside where the cost went; read the gate on a fixture where the moved cost stays inside the counted window, not one whose setup runs before it.
## Instrument validity
- Pair every conditional counter with a reached-but-not-taken counter, and decompose zero at every level (branch not taken, site not reached, function not called, path not taken); a zero says nothing about the levels above it.
- Instrument the callee, not the callers: one unconditional counter on the function's FIRST line answers "reached" for every call site, present and future.
- A conjunction counter (`A && B`) says nothing about `A` or `B` alone; instrument the condition the hypothesis turns on.
- A decomposition instrument asserts its parts sum to its whole and reports a violation count, paired with a plausibility floor (a 0 us frame is not a frame); check a timing scope's extent against the code it claims to measure, since one opened before an `#ifdef` runs to the end of the enclosing block.
- Assert every derived figure against physical bounds at the point of computation: excluded intervals disjoint, excluded fraction reported, no rate above the pacer, no share above 100%.
- Print the symbolic enum name, not a numeric code (a `switch` order is not the enum), and print the inputs rather than a ratio when an input's reliability is unestablished, keeping arithmetic in the analysis where it can be re-derived.
- Emit the state, not only the verdict: anything that resolves an offset, address, size, drive letter or artifact prints what it resolved, a gate prints the values it read to decide (a check that prints only on failure is silent exactly when it succeeds), and someone reads those lines.
- Label correct behaviour that shares a signature with a known bug (`NESTED=outer-call-still-in-flight`), and reset a diagnostic "why" field on the same cadence as the counter it explains.
- Zero every `__dpmi_allocate_dos_memory` block, and tie each derived table to an independently counted total with an assertion (`HISTOGRAM VOID`); uninitialised memory reads as data, not noise.
- Treat any header both assembly and C index into as an ABI: append at the end, never insert or reorder.
- A sentinel that falls on one side of a comparison (`-1` under a threshold) turns every instrument failure into that verdict; use `None`, an exception or an `ok` flag.
- Set a threshold detector below the effect you care about, and check the distribution just under the cut before believing a clean mechanism is the whole story.
- Run at least one cell per campaign with every optional counter, log and channel off; a bracket-set diff cannot see overhead present in both arms, and a log write is orders of magnitude dearer on real hardware than under an emulator.
- Decompose a throughput number into `per_loop_fps` and `overhead_s`; trust the extrapolation only when overhead is under ~25% of duration, else quote the wall-clock mean and a per-stage breakdown.

## Reading a result
- State the denominator and population before comparing two figures (which interval, which fixture, whether the brackets nest), and write the fixture name beside every share or percentage.
- A residual is not a term: subtraction is not measurement; check whether brackets nest before subtracting, and use a residual only as a coverage error bar.
- An addressable saving is not the term's cost; predict and measure the post-fix residual before quoting a saving.
- Check for multiple populations before taking a mean and report them separately with counts; skip-vs-do, hit-vs-miss and fast-vs-slow path all produce 4x-100x bimodal costs.
- Prefer a decomposable rate (tick rate x (1 - skip fraction)) over a percentile on a heavy-tailed run; keep the percentile for describing what a player feels.
- Read a delta against the band its comparison is in, within-session (0.0-0.1 fps here) or across-session (up to 0.85 fps), treating anything inside the band as unresolved; validate an apparatus change on differences (control-pair spread, a known arm delta), never against an absolute banked figure.
- "Clocked to wall time" does not mean external: enumerate internal quantities that grow with time (a log file reaching an offset) before looking outside the program.
- Non-detection is not absence: an intermittent detector's list describes the detector; fit the union of runs' event sets, and calibrate the fit against a null.
- Kill a hypothesis family with a measurement, not a pattern: vary the coordinate the mechanism cannot move, and measure any conversion constant in the same run rather than carrying it across the change.
- Scrutinize the number that confirms your prior hardest: check every quantity in the calculation you did not measure, and invert the arithmetic against something known.
- A number you have called absurd halts the conclusion resting on it; the next action is the measurement that closes the arithmetic, not the report.
- A comment, plan entry, prior conclusion or minutes-old inference is a hypothesis with your handwriting on it; when two candidates stand, the next line of code discriminates.
- What a verdict excludes still gets read: print an excluded noisy statistic's raw values unscored and look on purpose, and a voided cell's observations that do not depend on the broken part still stand (say which part that is).
- When a batch of cells fails, run a binary with a known prior figure BETWEEN the failures; record an unexplained transient as unexplained.

## Shell and DOS traps
- Audit a value against the shell it passes through with a byte-for-byte echo-back probe: on `COMMAND.COM` `|` and `>` truncate, `%` is stripped silently, `#` `;` `^` `&` survive; stripping and truncation differ and a presence check sees neither.
- Remove structure from a value rather than re-encoding it: one env var per stage plus a declared count the program refuses to run without.
- Two DJGPP runtime traps: `strtok` keeps one global state, so a nested `strtok` loop silently ends the outer one after its first element; `fwrite` to a full DOS volume returns short instead of failing, so assert bytes written against bytes attempted.
- A transform applied for comparison must never reach the artifact: keep the verbatim fetch and the normalised copy as two files, write from the verbatim one, assert byte and CRLF counts at staging.

- Wait on an exact PID (`kill -0`) or a result file the job writes last, never on a `pgrep -f` pattern: the pattern matches the waiter's own command line and it waits forever, silently.
## Boot config, drives and hardware
- Never `VOL`/`DIR` a removable drive without an `INT 24h` guard (an empty optical drive blocks on `Abort, Retry, Fail?` and costs a mains cycle); prefer the banner the machine already printed (MSCDEX's `Drive D: = Driver OPTICAL unit 0`) to a probe that asks.
- Diagnose in the documented order (lock/activity, chord window, both capture channels, verify_input); a no-LED-change reading is a symptom, not a diagnosis.
- Hold safety-critical arithmetic to a higher standard than the analysis it guards: prefer a round trip that fails on any wrong link to a single undocumented field, because the cost of being wrong is not a bad number.
- A hardware swap ends the session: re-anchor with a control + repeat pair, update every declared field inside the swap procedure, re-read every detected field, stop the campaign on a mismatch; a replacement part lacking a mode the workload needs contributes no rows to a shared table, and a known defect on a configuration is carried forward explicitly.
- An emulator is a correctness instrument, not a performance proxy (0.81x to 286x against real hardware); a green smoke pass proves only the branches the emulator reached, so confirm the risky port-write/IRQ/DMA path fired.

## Harness and collection
- Audit the fetch list against every file the program writes; a produced-but-unfetched file is a collection bug or an instrument to delete.
- Never edit a shell script while an instance runs (bash reads by byte offset): reverting does not rescue it, "still running" is not "unaffected", stage edits in a scratchpad and apply between runs.
- Archive, never delete, when clearing a collection directory, including the runner's own stdout and any fixed-tag capture file.
- Fault handling: record a retry as a retry, report a hung cell as a hole never a substitute, classify a timeout from a rolling window not one frame, report captured and held/repeated frame counts separately.
- A drivable project emits a bounded observable run, one schema-versioned manifest at exit (RUNMANIFEST, `shared/include/runmanifest.h`), start and end banners, decomposed metrics, and file-based config with documented precedence.

- Stage a program's config by the exact name the program reads (`src:NAME`), and make the runner refuse a mismatched name unless overridden; a config staged under its source basename is never read and every cell runs on defaults.
## Retractions and claims
- Write the scope into the claim in the same breath as the evidence ("no `RAMDRIVE.SYS` in `C:\DOS`"); before blocking or escalating, re-read the command and ask what population it sampled.
- Put a measurement between a striking result and the message reporting it, most of all when it fits a pattern the team has been rewarded for; the coordinator sets the pace, so slowing the relay is the coordinator's job.
- Commit an interpretation formed mid-investigation before the in-flight measurement that could confirm it reports, or do not claim it.
- Relax a pre-registered gate only if the justification survives with the outcome column covered, and declare it up front; a gate that voids every correct run is broken, not strict.
- A withdrawal is a claim and needs evidence; test a mechanism directly (one echo-back probe) instead of inferring it from the failure, and one mechanism must explain every instance, since "it fit" is not "it was tested".
- A confirmed cause is not necessarily the operative cause; check it is load-bearing, not merely present.
- Suspect your own new code before long-standing shared machinery, and read the harness before hypothesising about it.
- When testing a borrowed cross-port finding, verify the test reaches your path first, and hold a negative that would close the lead to the standard of a positive claim.
