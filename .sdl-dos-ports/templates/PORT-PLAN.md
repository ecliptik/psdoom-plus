<!--
Template: a new port repo's PLAN.md. Fill in each milestone as it's
reached; don't pre-fill dates/results you don't have yet. See
sdl-dos-ports' PORTING.md for the recommended slice order.
-->

# <PORT NAME> — DOS port plan

## Research (do this before writing code)

- [ ] Upstream repo located and a DOS-appropriate revision chosen and pinned
      in `vendor/sources.manifest`
- [ ] License reviewed (`LICENSE-REVIEW.md` started)
- [ ] SDL/engine API surface inventoried (which calls, which subsystems)
- [ ] External dependencies inventoried (codecs, archive formats, fonts,
      networking, threading)
- [ ] Filesystem/platform assumptions inventoried (see
      `.sdl-dos-ports/docs/filesystem.md`)
- [ ] Minimum patch sequence to reach a compiling, linking DJGPP build
      proposed

## Milestones

Use the `dos_status` ladder from `templates/PORT-STATUS.md`. For each
milestone reached, note the commit/tag and, where relevant, a DOSBox-X
smoke-test result.

- [ ] BOOTSTRAP — cross-build environment set up, `shared/build/sdl3-dos.mk`
      included, project's own `vendor/sources.manifest` entry added
- [ ] COMPILES — engine compiles and links under DJGPP (SDL/audio/etc. may
      still be stubbed)
- [ ] STARTS — executable runs under DOSBox-X, SDL3 initializes, exits
      cleanly
- [ ] TITLE_SCREEN — native-resolution framebuffer opens, title/menu
      renders
- [ ] RUNMANIFEST wired — `.sdl-dos-ports/shared/include/runmanifest.h`'s
      `runmanifest_emit()` called at clean shutdown; a real
      `[RUNMANIFEST-BEGIN]`/`[RUNMANIFEST-END]` block confirmed in a
      captured log with real (non-placeholder) fps/duration values. Do
      this here, not deferred to OPTIMIZING — see `PORTING.md`'s reuse
      list for why.
- [ ] PLAYABLE — a representative slice is playable start-to-finish
- [ ] FULL_GAME — the complete game is playable
- [ ] OPTIMIZING — profiling-driven optimization pass (see
      `.sdl-dos-ports/docs/optimization.md`); real-hardware benchmarks
      recorded per `templates/BENCHMARK.md`
- [ ] RELEASE_READY — packaged without redistributed copyrighted assets,
      `LICENSE-REVIEW.md` complete, `gallery/<name>/` entry written in the
      hub repo

## Campaign plan of record

<!--
Add one dated section like this when a real-hardware campaign starts
(performance, compatibility, a hardware-specific bug hunt). It is what
lets the work run as a self-driving loop -- see the benchmark skill's
references/self-driving-loop.md. Build it from RAW logs, not summaries.
Delete this comment block and copy the skeleton per campaign.

### <Campaign name>, opened <YYYY-MM-DD>

**Gates (what "done" means, measurable, on named hardware):**
- Gate 1 -- ...

**KPI definitions:** which figure judges each gate, and what each figure
does NOT show (e.g. static-screen fps; whole-run vs steady-state rate).

**Operator decisions (dated):**
- ...

**Standing real-hardware authorization:** <granted by the operator
directly / not granted>. Scope: <power on/off, staging builds, this
plan's cells>. Anything outside it still needs an explicit ask.

**Stated in advance:** what is probably out of reach, why, and the
evidence -- so it can be challenged.

**Work order (each slice its own commit; instrument -> predict -> fix ->
emulator correctness gate -> pre-registered ABBA -> record):**
- [ ] P0 -- fix the yardstick ...
- [ ] P1 -- ...

**Retractions:** mark a wrong section retracted in place with a pointer
to the correction; never delete it.
-->

## Open questions / risks

<!-- e.g. audio decode cost on 486, licensing terms, engine features with
     no DOS equivalent -->

## Measured constants

<!--
Every number a probe, diagnostic, or instrumented run produces, so a
later hypothesis has to be reconciled against what's already measured
instead of independently re-derived. This is the single highest-leverage
habit from a real fps campaign on this hub: a probe run in the first hour
measured a delay primitive's real cost, and that exact number turned out
to be the answer to an unrelated-looking investigation hours later --
sitting unused the whole time because nothing forced later hypotheses to
be checked against it. Add a row the moment a probe/diagnostic produces a
number; check new hypotheses against this table BEFORE running a new
probe, not after. See PORTING.md and the benchmark skill.
-->

| Constant | Value | Measured by | Hardware | Date |
|---|---|---|---|---|
<!-- e.g. | delay(1) real granularity | ~1.236ms/call | tests/probes/dlygran.c | 486DX2-66 | 2026-08-31 | -->
