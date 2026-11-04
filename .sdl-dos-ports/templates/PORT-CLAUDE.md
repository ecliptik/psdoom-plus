<!--
Template: a new port repo's own CLAUDE.md. Replace <NAME>/<ENGINE>
placeholders. Keep the DJGPP hard-constraints and workflow sections as-is
unless this port has a genuine reason to diverge from them.
-->

# <NAME>

<GAME> ported to MS-DOS. This repo owns the game engine (<ENGINE>) and
anything genuinely specific to it. The generic SDL3-DOS platform layer,
build fragments, DOSBox-X/probe/smoke-test tooling, and porting docs live
in the [sdl-dos-ports](https://github.com/ecliptik/sdl-dos-ports) hub repo,
consumed here as a git subtree at `.sdl-dos-ports/` (a plain directory
merged into this repo's own git history -- no `.gitmodules`, no separate
checkout step; a normal `git clone` of this repo gets it immediately).

Read `.sdl-dos-ports/CLAUDE.md` and `.sdl-dos-ports/docs/` before making
platform-layer decisions — if a change would belong in `shared/` rather
than here, make it there instead (see that repo's rules for `shared/`
changes) and bump this repo's `.sdl-dos-ports/` pin (`git subtree pull
--prefix=.sdl-dos-ports <hub-remote> main --squash`).

## What lives here vs. in the hub subtree

- Here: `vendor/<engine>/` (pinned engine source), `patches/<engine>/`
  (DOS patches specific to this engine — renderer, format decoders,
  scripting VM, anything not shared platform work), `setup/` (this game's
  DOS-style setup utility, if any), game-specific tests, `qa-results/`,
  `profiles/<name>.yaml` (this port's vcctrl profile).
- In `.sdl-dos-ports/shared/` (subtree-pinned, read-only from here except
  via a PR to the hub repo): SDL3-DOS platform patches, MIDI/audio
  hardware backends, the `sdl3-dos.mk` build fragment, DOSBox-X
  automation, the probe library, and the agent-team charter templates
  this repo's own `.claude/agents/` should be based on.

## DJGPP / DOS hard constraints

- Binary `fopen` mode for anything not text.
- 32-bit `size_t` assumptions only.
- Set an adequate `stubedit` stack size.
- No SIMD unless gated the same way `shared/build/sdl3-dos.mk`'s
  `NOSIMD_FLAGS` gates it downstream.
- No shared libraries.
- ASCII-only source files — no smart quotes, em-dashes, or other non-ASCII
  characters.

## Workflow

1. Port in narrow, buildable slices: compile → video → input → filesystem
   → audio → gameplay → optimization. No giant first patch.
2. Build and validate under DOSBox-X after every meaningful change. A host
   build is not a DOS build.
3. Never guess about upstream <ENGINE> code — inspect the pinned revision
   before proposing a patch.
4. Preserve upstream source: vendor pinned by SHA + a local patch series
   in `patches/<engine>/` (see `.sdl-dos-ports/docs/patch-conventions.md`),
   not unexplained direct source divergence.
5. Wire in `.sdl-dos-ports/shared/include/runmanifest.h` at the STARTS/
   TITLE_SCREEN milestone — call `runmanifest_emit()` once at clean
   shutdown rather than hand-rolling your own fps/frame-timing report.
   Do this early, not deferred to the OPTIMIZING milestone: a
   dossage/Passage investigation once spent hours chasing an fps gap
   before finding the actual bug lived inside the engine's own bespoke
   fps computation — the number being measured and the mechanism under
   suspicion were the same unreviewed code. See
   `.sdl-dos-ports/shared/skills/dos-hardware-validation/references/runmanifest-log.md`.
6. Do not optimize without numbers beyond the baseline RUNMANIFEST gives
   you — instrument further only when a specific question needs it (see
   `.sdl-dos-ports/docs/timing.md` and `optimization.md`).
7. Real hardware (via vcctrl, see `.sdl-dos-ports/docs/hardware-testing.md`)
   is authoritative for performance/compatibility claims; DOSBox-X/86Box
   are automation gates.

## Never contribute upstream

Never send anything back to <ENGINE>'s upstream, or to SDL/SDL_mixer/
SDL_image, or to any other vendored project — no pull requests, no issues,
no bug reports. All patch and research work stays in this repo or the
sdl-dos-ports hub. See `.sdl-dos-ports/CLAUDE.md`'s "Never contribute
upstream" section for the full policy.

## Licensing

Never guess this port's license. Confirm it by reading <ENGINE>'s actual
LICENSE/COPYING file at the pinned revision, then record it in the hub's
`ports.yaml` (`upstream_license`, `upstream_license_verified: true`) and in
this repo's own `LICENSE-REVIEW.md` — do not rely on a remembered or
commonly-believed license. Never commit commercial/copyrighted game data,
proprietary fonts, or music requiring separate permission. Keep
`LICENSE-REVIEW.md` current before building any release-packaging
automation.
