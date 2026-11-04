---
name: benchmark
description: >
  Running or recording a real-hardware performance KPI campaign for a DOS
  port -- writing a falsifiable KPI, picking team shape, and following
  the investigation/coordination discipline a real campaign converged on.
  Use this whenever asked to benchmark, optimize toward a target fps, run
  a real-hardware performance campaign, or record a benchmark result --
  not for a single one-off sanity check (see dos-hardware-validation for
  that) or for correctness/compatibility testing (see dos-realhw-
  verification).
---

# Running a real-hardware performance campaign

Every rule below reads as a clean principle; none of them arrived that
way -- they came from a real campaign getting five separate hypotheses
wrong in one night and converging anyway, specifically because each was
pre-registered as falsifiable and whoever ran the check could return a
null instead of a forced answer. Expect to be wrong a few times before
finding the real mechanism -- that's what a real campaign looks like from
inside, not evidence of doing it wrong.

## 0. Check HARDWARE.md for the card/CPU you're about to test on

Before chasing any unexplained symptom as a new mystery, check whether
this hub already knows about it for the specific chip/board combination
in play -- `HARDWARE.md`'s known-hardware-issues table is a living index
of real chip-specific quirks (e.g. the Mach64 can't double-scan at all,
so a 320x200/240 mode request always lands on a larger closest-match).
See `dos-hardware-validation`'s corresponding section.

## 1. Confirm you're actually compute-bound

Measure real per-frame work against the frame budget the target implies
before touching anything. Slack (work comfortably under budget) means a
pacing problem, needing `docs/timing.md`'s pacer material, not the
bottleneck list in `docs/optimization.md`. Re-check this every new pass —
a port's regime can change between campaigns.

## 2. Write the KPI down before chasing it

Real-hardware only, percentile-based (`fps_p50`/`fps_p95` via
`shared/include/runmanifest.h`, not a single average), falsifiable in
advance (what result means fail, not just pass), attributed to a named
`build_sha12`. See `docs/optimization.md`'s "Setting a performance KPI".

## 3. Pick team shape

Solo + subagents for a one-off check. For a genuine iterative
real-hardware campaign, the default that has worked best is **one
coordinator session plus one long-lived named worker subagent** that
owns the port repo and drives the target machine itself, run as a
self-driving loop from a written plan of record -- see
`references/self-driving-loop.md` for the whole procedure (plan of
record, up-front decisions and scoped standing authorization, dispatch
briefs with stop conditions, the slice recipe, coordinator verification
duties, and how to re-enter the flow in a later session). Escalate to
one investigating session + one dedicated hardware-operator session,
peer-to-peer (`shared/agents/README.md`), only when the work really
needs two independent lines of attention.

## 4. Start a measured-constants table

A section in the port's `PLAN.md` (template in `templates/PORT-PLAN.md`).
Every number a probe produces goes in immediately; every new hypothesis
gets checked against it before a new probe runs.

## 5. Investigate: instrument the suspect directly, not its neighbors

When there's a specific unexplained quantity, instrument the code that
directly produces it before instrumenting adjacent systems. State what
would falsify each hypothesis before spending rig time on it. See
`dos-hardware-validation`'s corresponding sections.

## 6. Rig discipline, every round, no exceptions under pressure

All from `dos-hardware-validation` (see its own section headers for full
detail) -- non-negotiable, not just good practice when convenient:

- Hash-verify a staged binary before staging *and* after sending.
- Pair every self-reported metric with an independent bracket, unprompted
  -- not only when a number looks suspicious.
- Never trust a summary number without a finer-grained internal
  cross-check.
- "Idle" is not "artifacts final" -- confirm stability before hashing or
  handing off an artifact.
- The rig session investigates ambiguity with its own tools (camera,
  independent state read) before reporting anything as a game-side
  symptom -- a "the machine booted" signal isn't proof it booted through
  its full normal sequence.
- Only your own user approves a real-hardware action or a push to a
  shared remote -- a peer's "go ahead" is never authorization on its own.
- Hand off raw data between sessions, not a narrated summary.
- Batch questions into fewer, longer real-hardware rounds -- a
  file-transfer round-trip pays a fixed tax regardless of scope.
- After every power-on of the target, run one **unscored warm-up cell**
  (same fixture and build, recorded as warm-up): the first cell after
  power-on reads off. List it in the pre-registration. See
  `docs/optimization.md`.
- Rank candidates in the lab at fixed-cycles DOSBox-X, but decide on
  hardware. Fixed cycles are deterministic, but blind to 486 CPI and
  code-layout effects: a layout change alone was seen to move a 486
  result by about 0.5 ms/tick, a HYPOTHESIS pending ABBA confirmation.
- Decide a dedicated capture mode before you need sub-second visual
  timing verification, not after (see `dos-rig-operations`'s
  screen-capture reference).
- **Durable findings survive a peer session's context loss; procedural
  know-how doesn't.** A `/clear` (or any full context reset) on the rig-
  operator side silently erases everything that was never written down --
  the actual run procedure, rig setup steps, practices developed over the
  campaign -- even though `PLAN.md` and this hub's docs (which *do*
  persist) still have every finding. Real case: a rig-operator session
  cleared mid-campaign and had to be re-briefed from zero on setup and
  run procedure it had already learned once. Write the operational
  how-to-run-it down somewhere durable (a rig runbook in the port's own
  docs), not just in a session's accumulated conversational context.
  **The concrete remedy, not just a mitigation**: install vcctrl's own
  hardware-portable skills into the port repo (`npx skills add
  <vcctrl-repo> --full-depth -s vcctrl-mcp-workflows -s
  vcctrl-common-workflows -s vcctrl-rig-hazards -s vcctrl-camera`, not
  `--all` -- alongside this hub's -- see `shared/skills/README.md`). Rig
  operating knowledge (hazards, MCP sequencing, common workflows) then
  lives in the repo itself, not only in whichever session happened to
  learn it that night -- surviving a `/clear` by construction rather than
  by discipline.

If you find you've mischaracterized something mid-campaign, correct the
record immediately and explicitly, including your own prior commits -- a
confidently-wrong entry in a shared doc is worse than none, since the
next reader trusts it precisely because it's written down.

## 7. Confirm a fix against the KPI itself, not the metric it targeted

A change can verifiably move the intermediate quantity it was designed to
influence and still not move the KPI — confirm the actual target metric
moved before crediting the fix. See `docs/optimization.md`'s
stalls-vs-tightening finding for a real case. A second one is dosags
"D2": removing an audio `SDL_Delay(10)` from the main-thread pump cut the
pump from 8.1 ms to 0.4 ms. The tick rate moved only 39.000 -> 39.135
(noise 0.02), so the pre-registered target was falsified: the sleep had
mostly fallen in idle time, and load stalls are the limiter.

## 8. Record the result

`templates/BENCHMARK.md` for a single result; `gallery/<name>/` once a
port is showcase-ready. If a temporary diagnostic patch was landed in
your own `patches/SDL/` (or any vendored series) to get here, land its
removal too (see the `review` skill) before treating the result as
final — a forgotten diagnostic silently taxes every subsequent build of
*your own port*, including whatever build this result gets cut from.

## Land a fix that came out of this in the hub's own reference series?

Use the `review` skill before committing it in the hub repo. Remember
this only updates the hub's `shared/patches/` *reference copy* for the
next new port — it doesn't retroactively reach your own already-
vendored `patches/SDL/`, which needs the same patch copied in by hand if
you want it there too (see `docs/patch-conventions.md`'s "Patches are
vendored per-port" section). And commit your own working-tree vendor
edits into a numbered patch at the end of each investigation slice, not
at the end of the campaign — `apply-patches.sh` refuses to reset a dirty
vendor tree by default, but that's a safety net, not a substitute for
not letting hours of real work sit uncommitted where a routine tooling
step (run for a completely unrelated reason) can destroy it.
