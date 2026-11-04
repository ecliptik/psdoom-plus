# The self-driving campaign loop

How to run a multi-day real-hardware campaign so the operator sets the
direction once and only has to step in for genuine decisions. Distilled
from dosags's 40fps push (2026-09-15..17): fps_p50 5.0 -> 34.6 on a
486DX2-66 through four pre-registered ABBA wins, several harness bugs
found and fixed, three of the coordinator's own claims retracted along
the way -- with the operator mostly watching. Every rule below is there
because its absence cost time in that campaign.

## Shape: one coordinator, one long-lived worker

- **Coordinator** (the session the operator talks to): owns the plan,
  the operator conversation, verification, and the hub repo. Never
  guesses at results; never edits a file the worker currently owns.
- **Worker** (one named, long-lived subagent/fork, resumed by name for
  the whole campaign): owns the port repo and drives the target machine
  itself. It accumulates the operational know-how (tags, transfer
  quirks, how long things take), so resume it rather than starting a
  fresh one; brief a fresh one only from the written plan.
- One worker at a time per port repo. Two agents in one working tree
  race on files and on git; parallel READ-ONLY research is fine.
- This is a subagent shape, not an agent team. Reach for the
  investigating-session + hardware-operator-session split in
  `shared/agents/` only when two humans-worth of attention is really
  needed.

## 1. Write the plan of record before spending hardware time

A dated section in the port's `PLAN.md` (skeleton in
`templates/PORT-PLAN.md`, "Campaign plan of record"). It must hold:

- **Gates**: what "done" means, as measurable statements on named
  hardware.
- **KPI definitions**: exactly which figure judges each gate, and what
  each figure does NOT show (static-screen fps, whole-run vs
  steady-state rates, what a stall is).
- **Operator decisions**, dated, in their words where possible.
- **Standing authorization and its scope** (see 2).
- **Work order**: phases and slices, each with its own gate.
- **Stated-in-advance honesty caveats**: what is probably out of reach
  and why -- and the evidence, so it can be challenged.

Build it from RAW logs, not from summaries of earlier work. Reviewing
the raw per-phase log is what showed that a headline fps number was a
static-screen figure and that a "correction" was itself wrong.

## 2. Get decisions up front, and scope the authorization

- Ask the operator's open questions in one batch, with a recommended
  option and the trade-off for each, through the structured question
  tool (it notifies their phone; a question buried in prose does not).
- Ask for **standing real-hardware authorization scoped to the plan**:
  power on/off, staging builds, running the plan's cells. Anything
  outside it (boot configuration, other software, anything unusual)
  still needs an explicit ask. Record the grant and its scope in the
  plan -- it is what lets the loop run unattended, and it is only valid
  because it came from the operator directly, never relayed.
- When a decision does not block the next slice, **dispatch the work
  first, then ask**. Never idle the hardware on a question whose answer
  only matters later.

## 3. Brief the worker so it does not hand back early

Every dispatch states: the slice(s), the design constraints, the
validation it must pass, **stop conditions decided in advance**, and
what to report. Chain the next slices in the same message ("if the
gates hold, continue straight into X and Y") so a clean result does not
cost a round-trip.

Stop conditions that worked: a pre-registered ABBA regresses; the
independent cross-check disagrees with the self-reported figure; a
correctness gate fails; anything looks wrong on the target machine; the
next step needs an operator choice. Otherwise: keep going.

Known frictions, and the instruction that fixes each:

- A worker tends to end its turn "waiting for a notification" during a
  long run, and nothing wakes it. Say so explicitly: poll internally on
  a fixed interval and return only on real completion, failure, or a
  decision. The coordinator should still expect to nudge occasionally
  (turn limits), and a nudge is one line: "check the current state and
  continue."
- A worker will keep adding one more probe forever. Bound each
  investigation ("this is the closing diagnostic round"), and when a
  guess-one-function loop stops converging, change the METHOD (exhaustive
  partition, a synthetic probe, a cheaper fixture) rather than approving
  another guess.
- The target can stop answering the keyboard at a post-transfer reboot
  and the harness will just sit there refusing. The worker's scripts must
  detect it (a reboot-took witness) and recover by themselves (one power
  cycle, verified boot, link check) -- see `dos-rig-operations`'
  `references/power-management.md`. Until they do, the coordinator's
  own two-channel look plus `verify-input` settles it in a minute.
- Attribution, date, and tag conventions drift across a long run.
  Restate them in the dispatch when they change.

## 4. The slice recipe (performance work)

instrument -> source-grounded hypothesis -> write the prediction down ->
fix -> emulator correctness gate (screenshot hash identical, harness
green, game speed correct) -> pre-registered ABBA on real hardware
against the CURRENT baseline -> commit -> record in the plan.

- One mechanism per build, one commit per slice.
- Pre-register the threshold from the measured noise floor, and compare
  the result with the written prediction -- a 4x miss in either
  direction is a finding about the model, not a footnote.
- When a change touches the clock, the pacer, or the metric itself, add
  an independent witness (RTC, external wall clock) in the same slice.
- Develop on the fastest fixture that exercises the code path; return
  to the heavy one to confirm (`docs/optimization.md`).

**Prefer one extra field on a line you already emit, over any argument.**
In one campaign the four consecutive findings that cracked its hardest
problem were each a single field added to an existing log line -- sprite
count, largest-sprite dimensions, an engine dirty flag, and a per-GUI
tally. **None needed a hardware cell. Each answered exactly one
question. Three of the four overturned or tightened something that would
otherwise have been asserted**, including the one that identified the
root cause outright.

> **Correction, 2026-09-21 (proven from source; the effect on that
> campaign's figures is NOT yet measured).** Two of those fields -- the
> engine dirty flag and the `unchanged` half of the per-GUI tally -- were
> read AFTER the engine had cleared the flag (one loop of the draw routine
> re-renders each changed GUI and calls `ClearChanged()`; the fields were
> read in the next loop). They could only ever report "unchanged", so
> "100% of 421 observations" was a tautology, and the fix built on it
> skipped the invalidation for changing GUIs as well. The identity and
> size findings (which GUI, 460x260, masked) are unaffected. It surfaced
> when the fix was being re-specified, from one read of the source order.
> The rule it adds: before trusting a field, work out what it would print
> if the answer were the opposite, and find the code path that lets it
> print that. A field that cannot say "changed" has not measured
> "unchanged". "A field turns a disagreement into a number" holds only
> for a field that can come out either way.

That campaign's own retrospective is the rule worth carrying: *the
expensive part was never the hardware time -- it was the rounds spent
reasoning from figures that did not describe what we thought they
described.* Nine separate times two figures were compared whose scopes
differed, and every one was resolvable by reading rather than by
measuring.

So when a question arises mid-slice, **cost the field before costing the
argument.** A field is minutes, rides on the next build, needs no cell,
and converts a disagreement into a number. Write down which single
question it answers -- if it does not answer exactly one, it is the
wrong field.

**When a fixture bounds the answer, ask whether the question is about the
fixture or about the port.** A gate fixture is a vehicle for a question,
and the two are easy to conflate once the gate has been failing for a
while.

A campaign closed its frame-rate target with a genuinely closed argument:
the cost was one half-screen transparent GUI the game draws over a moving
scene, removing it was *sufficient*, clipping it was *insufficient*, and
removing it was unavailable because it would change what the game draws.
All true — and it answered "can this game hit the target", while the
operator had asked "can the port hit the target at this colour depth".
**A capability probe with that GUI hidden cleared the bar**, turning "not
reachable on this machine" into "the port reaches it; this game's GUI is
what it does not reach it with."

Two rules follow, and the second is what keeps the first honest.

**Re-read the goal's own wording when a search closes.** The worker's
retrospective is the useful form: *"I had the bound and the numbers, and
I concluded the search is closed rather than there is a question here
about the port that the fixture cannot answer."* A closed argument about
the fixture is not a closed argument about the subject.

**And label a capability probe as one, loudly, in the same breath as its
result.** It changes what the program draws, so it is not a gate cell and
must never be recorded as one — the gate's own verdict is unchanged and
the closed argument still stands. Revert the probe from the fixture
immediately afterwards, or every later cell silently measures a different
program. Give it the same mechanistic pre-registration as any other cell:
here the tail-frame population had to collapse alongside the rate rising,
because a rate that rose while the tail persisted would have meant the
model was wrong and the headline flattering.

**And when the fix lands, pre-register the MECHANISTIC signature, not
just the headline metric.** If the model says an unchanged GUI is being
invalidated, then the fix must show that GUI's contribution to sprite
pixels collapsing and the dirty rect ceasing to inflate -- not merely
that frame rate improved. A headline number can move for unrelated
reasons; the mechanism moving is what confirms the model was right.

## 5. Coordinator duties on every report

- **Verify before relaying**: the commits exist, the tree is clean, the
  machine is in the power state claimed. Then report, separating what
  was verified from what is the worker's account.
- **Read the numbers, not the adjectives.** Check a surprising claim
  against the trusted baseline it should reconcile with before acting
  on it; send it back if it was never reconciled.
- **Look at the screen before relaying a rate.** Open the cell's
  physical-screen contact sheet for any moving-content figure; the
  operator should never be the one who notices trails first.
- **When the operator says "is it stuck?", look yourself** -- both
  capture channels, power state, lock state -- then tell the worker
  exactly what you saw. Twice this caught a real fault (a dead input
  link; a run that had finished while the worker still waited) and
  twice it cleared a false alarm, all within a minute.
- **Retract in place, promptly, by name.** Mark the wrong section
  retracted with a pointer to the correction; do not delete history. A
  plan that records its own errors is one the operator can trust.
- **Fold durable findings back into the hub as they land** (`docs/`,
  `HARDWARE.md`), labelled proven or unproven. The coordinator owns the
  hub precisely so this never collides with the worker.
- Queue non-urgent instructions to a running worker rather than
  interrupting hardware actions mid-flight.

## 6. Getting back into the flow later

1. Read the port's plan of record and the memory note that points at
   it; confirm the standing authorization still applies to what you are
   about to do (it is scoped to that plan, not to the port forever).
2. Read the last RAW result log, not the last summary.
3. Check the target machine's state (power, lock, screen) before
   assuming anything.
4. Resume the named worker if it still exists; otherwise start one and
   have it read the plan section first.
5. Dispatch the next unchecked slice with stop conditions. Ask only
   what is genuinely undecided.

## 7. Before designing a cell, ask what is already on disk

A real-hardware campaign trains an instinct to answer questions by
running one more cell. Tally where a long night's decisive moves actually
came from and that instinct looks expensive. From one campaign's
stall investigation, in order:

| finding | what settled it |
|---|---|
| event positions lie on a lattice | logs already collected |
| a cumulative-writes drift explains the counts | logs already collected -- **falsified** |
| the probe's stage list truncated after stage one | the probe's own source (a nested `strtok`) |
| a 1.3 KB transfer times out at exactly 180 s | the harness's source (a composed path) |
| "no `RAMDRIVE.SYS` on this machine" | one more `DIR` -- **it was in `C:\WINDOWS`** |
| the stall magnitude is 4.00 BIOS ticks | the same logs -- **circular, withdrawn** |
| a count-to-4 wait explains it | the same logs -- **refuted, bounded above by 4** |
| a byte counter drives the period | **a cell.** Nothing on disk could have said it |

**Seven of eight were reads.** The eighth needed hardware and was the
single hardest result of the campaign -- it killed a family outright
where four pattern arguments had only wounded it.

That ratio is not an argument against cells; it is an argument about
**order**. A cell costs a boot, a queue slot, thermal cycling on old
parts, and an hour of latency before anyone learns anything. A read
costs minutes and often returns a *better* answer, because it can
address the mechanism directly rather than through its symptoms.

**So before writing a pre-registration, ask three questions.** Does data
already collected bear on this -- including runs voided for other
reasons, which keep every observation that did not depend on the broken
part? Does the source of the thing under test answer it -- the probe's,
the harness's, the engine's? And is the question actually about the
machine at all, or about something we wrote?

**Two failure modes this catches, both seen in one night.** A claim about
"the machine" resting on evidence about one directory. And two
hypotheses argued at length about long-standing shared infrastructure
when the defect sat in freshly written code with no history of working.

**And the corollary: read before escalating.** A licensing question was
minutes from reaching an operator over a driver that was already on the
card. A boot-config change was queued to buy an elimination that a cell
already running would deliver for free. Both were one read away.
