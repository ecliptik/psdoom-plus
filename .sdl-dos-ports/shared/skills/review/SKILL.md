---
name: review
description: >
  Reviewing a patch destined for a port's own patches/<vendor>/ (including
  a vendored SDL/SDL_mixer copy), patches/<engine>/, or this hub's own
  shared/patches/ reference series -- against this hub's own landing
  conventions before it's committed: provenance verification, DJGPP hard
  constraints, neutral naming (hub's reference series only), numbering/slot
  discipline, and the "read gating code, not commit titles" rule. Use
  this whenever a patch (yours, a peer session's, or a teammate's) is
  about to be committed anywhere in this hub or a port repo, especially
  one authored or handed over by a different session.
---

# Reviewing a patch before landing it

Distinct from the generic `/code-review` command: this checks a patch
against *this hub's own* conventions (`docs/patch-conventions.md`), not
general code quality. Run every step; a patch that's correct C but wrong
by these conventions still shouldn't land.

Every port owns a real, vendored copy of `patches/SDL`/`patches/SDL_mixer`
(seeded once at scaffold time, never symlinked) — landing a patch there
only ever affects that one port. Landing a patch in this hub's own
`shared/patches/` only updates the *reference copy* a future new port
will scaffold from; it does not reach any already-scaffolded port. See
`docs/patch-conventions.md`'s "Patches are vendored per-port, not
shared" section. Which repo you're reviewing in tells you which of these
you're doing — the checks below apply to both.

## 1. Verify provenance

Is this a real `git format-patch` output (genuine commit hash/author/date)
or a hand-typed/reconstructed diff? If it arrived through chat, treat it
as unverified until confirmed byte-for-byte against the author's own
checkout — chat transport silently strips trailing whitespace on blank
context lines. See `docs/patch-conventions.md`'s provenance section.

## 2. Check the slot number

Is the `NNNN` the actual next-free slot in the *target* series (`ls |
sort | tail` in that series' own `patches/<vendor>/`, whether that's a
port's own vendored copy or this hub's `shared/patches/<vendor>/`), not
a guessed or stale number? Every series' numbering is independent of
every other port's — slots are reserved, never renumbered.

## 3. Check DJGPP hard constraints

Binary `fopen` mode (`"rb"`/`"wb"`/`"ab"`, never text mode) for anything
not text, 32-bit `size_t` assumptions only, ASCII-only (no smart quotes,
em-dashes, other non-ASCII), no SIMD unless `NOSIMD_FLAGS`-gated, no
shared libraries. Grep the diff for `fopen(` and check every mode string
individually — a series with a consistent binary-mode convention can
still have one line that regresses it. Same DJGPP target whether it's a
port's own vendored series or the hub's reference copy.

## 4. Check neutral naming (this hub's own `shared/patches/` reference series only)

Any new `SDL_HINT_*` or log-file name landing in `shared/patches/`
(the hub's reference copy a future port will scaffold from) must be
project-agnostic (`SDL_HINT_DOS_*`, not a per-port prefix) — see
`docs/patch-conventions.md`'s neutral-naming section. Not applicable to
a patch landing in a port's own vendored series or its own
`patches/<engine>/` — it's port-owned, seen by nobody else, so a
port-specific name there is fine, even expected.

## 5. If it cites another engine's patch history as precedent, read the actual gating code

Don't trust a cited patch's commit title or a later patch's summary of an
earlier one's rationale — read what's actually gated behind what
condition. A patch series' own stated rationale is not evidence; a real
case on this hub had a later patch in the same series directly disprove
an earlier patch's premise. See `docs/patch-conventions.md`.

## 6. If it's a temporary diagnostic, confirm its removal is scheduled

Applies wherever it lands — a port's own vendored series or this hub's
reference copy. A diagnostic left with no removal plan silently taxes
every future build of whatever series it landed in (see
`docs/patch-conventions.md`'s real incident for what this actually
costs). Its removal needs to either land alongside it once the
measurement is in hand, or get an explicit note in the commit message of
what condition triggers the removal patch. Don't land an open-ended
"temporary" patch with no removal plan.

## 7. Confirm it builds and smokes clean

Per `CLAUDE.md`: a change to `shared/` isn't done until validated under
DOSBox-X. If you don't have the toolchain to do this yourself, say so
explicitly and get the validation from whoever does (see
`shared/agents/README.md`'s team-shape guidance) before landing — never
land an unvalidated shared-layer change silently.

## Generating the patch yourself?

Commit working-tree vendor edits into a numbered patch at the end of
each investigation slice, not at the end of a campaign — `apply-patches.sh`
refuses to reset a dirty vendor tree by default, but that's a safety net,
not a substitute: hours of real work sitting uncommitted in a vendor tree
can still be lost to a routine `apply-patches.sh` run made for a
completely unrelated reason before that check was added, or on a tree
someone deliberately forces past it.

## What this skill does not replace

Real hardware validation of the *result* the patch produces (see
`dos-hardware-validation`) — this skill reviews the patch as a patch, not
whether the underlying fix/feature actually works on real hardware.
