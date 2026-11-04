# Starting a new port

This describes the practical steps to take a candidate from `ports.yaml`
through to a working DOS port, reusing everything already proven by
doskutsu and staged in `shared/`.

```
claim -> scaffold -> research -> compile -> video -> input -> filesystem
  -> audio -> gameplay -> real-hardware baseline -> performance campaign
  -> release
```

## Before writing any code

Four things a real campaign's own retrospective found worth setting up
before the first line of code, in priority order:

1. **Stand up an external, program-independent way to measure elapsed
   wall-clock time** — never trust only what the game reports about its
   own performance. A self-reported metric computed from a clock the code
   under test can perturb tends to fail in the *flattering* direction,
   not an obviously wrong one — see `docs/timing.md`'s self-lying-metric
   entry for the real case this came from.
2. **Wire in `shared/include/runmanifest.h` at the STARTS/TITLE_SCREEN
   milestone**, not deferred to OPTIMIZING (see step 4 below) — don't
   hand-roll fps/frame-timing math from scratch.
3. **Start a measured-constants table in the port's own `PLAN.md`**
   (template in `templates/PORT-PLAN.md`) — every number a probe produces
   goes in it immediately, and every new hypothesis gets checked against
   it before a new probe runs. The single highest-leverage habit in this
   whole document: a real campaign's very first probe of the night
   measured a constant that turned out to be the answer to an unrelated-
   looking investigation hours later, sitting unused because nothing
   forced later hypotheses to be checked against it.
4. **Decide team shape now, not once you're already deep into a
   campaign** — see step 4's team-shape note below.

Every rule in this document reads as a clean principle; none of them
arrived that way. They came from a real campaign getting things wrong
repeatedly and converging anyway — five separate hypotheses in one
night were wrong, and the campaign still converged on the real answer,
specifically because each one was pre-registered as falsifiable and
whoever ran the check was empowered to return a null instead of a forced
answer. If you follow this document and it still takes several rounds of
being wrong before you find the real mechanism, that's what a real
campaign looks like from inside, not evidence you're doing it wrong.

## 0. Prerequisites: the DJGPP toolchain

`shared/build/sdl3-dos.mk`'s `djgpp-check` target only verifies a working
DJGPP install is already on `PATH` -- it doesn't build one. If you don't
already have a modern DJGPP cross-compiler (`i586-pc-msdosdjgpp-gcc` or
`i386-pc-msdosdjgpp-gcc`), build one from source via
[jwt27/build-gcc](https://github.com/jwt27/build-gcc) -- a well-known,
actively-maintained community project for exactly this, confirmed as a
working real-world recipe by
[DevilutionX's own `dos-prep.sh`](https://github.com/diasurgical/DevilutionX/blob/master/Packaging/windows/dos-prep.sh),
an unrelated project that also ports to DOS via SDL3:

```sh
git clone https://github.com/jwt27/build-gcc.git
cd build-gcc
sudo ./build-djgpp.sh --prefix=/opt/i386-pc-msdosdjgpp-toolchain --batch binutils gcc-14.2.0 djgpp-cvs
```

Then add `/opt/i386-pc-msdosdjgpp-toolchain/bin` to `PATH` before running
`shared/scripts/setup-symlinks.sh`.

## 1. Pick a candidate

Look at [`ports.yaml`](ports.yaml) for a `BACKLOG` entry. Lower `priority`
numbers are recommended next steps, but difficulty and your own interest
matter too — `difficulty: 1-2` candidates (Passage, Meritous, POWDER) exist
specifically to prove the pattern works with minimal engineering before
attempting something like Adventure Game Studio.

If the entry's `upstream_url` is `null`, your first task is locating the
canonical upstream repository and picking a DOS-appropriate revision —
never guess. Record the URL and pinned SHA in `ports.yaml` before writing
any code.

## 2. Scaffold the port repo

```sh
scripts/new-port.sh <name>
```

This creates a new sibling repository (or a local directory ready to push,
if you haven't set up a remote for it yet), pre-populated from
[`templates/`](templates/), with `sdl-dos-ports` added as a git subtree at
`.sdl-dos-ports/` (no `.gitmodules`, no separate checkout step -- a plain
`git clone` of the new repo gets `.sdl-dos-ports/` immediately) so the new
repo can immediately reference `.sdl-dos-ports/shared/...` from its build.

`scripts/new-port.sh` also writes the new repo's `.claude/settings.json`,
which registers this hub as a Claude Code plugin marketplace and enables
the `sdldos` plugin -- so anyone who opens and trusts the new repo is
offered this hub's skills under one colon-namespaced prefix
(`/sdldos:review`, `/sdldos:benchmark`, `/sdldos:dos-hardware-validation`,
...), with no per-repo copy of any `SKILL.md`. vcctrl's skills are not a
plugin, so the script installs the four hardware-portable ones flat into
the same repo via `npx skills add <repo> --full-depth -a claude-code -s
vcctrl-mcp-workflows -s vcctrl-common-workflows -s vcctrl-rig-hazards -s
vcctrl-camera` (not `--all` -- that would also pull in
`vcctrl-repo-conventions` and `vcctrl-webkvm-copy`, which document
vcctrl's own contributor conventions, not anything a port needs). To
install the plugin by hand somewhere else:

```sh
/plugin marketplace add https://forgejo.example.ts.net/ecliptik/sdl-dos-ports.git
/plugin install sdldos@sdl-dos-ports
```

(`forgejo.example.ts.net` stands in for the private tailnet Forgejo;
use the hub's real `origin` URL.)

See `shared/skills/README.md` for adopting it into an older port or from
an agent that can't load plugins.

Update this repo's `ports.yaml`: set `dos_status: RESEARCH` and
`port_repo_url` for the candidate you claimed.

## 3. Research before touching code

Follow the model in `plans/SDL_DOS_PORTING_PROGRAM.md` (local, gitignored —
the original program specification this repo implements): inventory the
SDL/engine surface, dependencies, filesystem assumptions, and license
terms before writing a line of DOS-specific code. For an engine as large as
AGS this is its own milestone (RESEARCH.md, SDL-SURFACE.md,
DEPENDENCIES.md, PLATFORM-SURFACE.md, LICENSE-REVIEW.md); for something as
small as Passage it's a page.

## 4. Port in narrow, buildable slices

```
compile -> video -> input -> filesystem -> audio -> gameplay -> optimization
```

Build and validate under DOSBox-X after every meaningful slice — a host
build is not a DOS build. Don't accumulate speculative compatibility edits
across multiple subsystems before compiling.

Reuse before rewriting:
- SDL3-DOS platform behavior (VESA/Cirrus/S3, SB16/OPL2/OPL3/GUS/WaveBlaster
  audio, gameport joystick, DPMI timing): your own `patches/SDL/`,
  vendored once from `.sdl-dos-ports/shared/patches/sdl3-dos/` at scaffold
  time (real files now, not a live link -- see `docs/patch-conventions.md`).
- MIDI playback: `.sdl-dos-ports/shared/audio/midi_sched.{c,h}` plus the
  hardware backend of your choice.
- Cross-build stages for SDL3/SDL3_mixer/SDL3_image:
  `.sdl-dos-ports/shared/build/sdl3-dos.mk` (your Makefile defines its own
  final `game` stage and includes this fragment for the rest).
- DOSBox-X automation and bring-up smoke tests:
  `.sdl-dos-ports/shared/tools/` and `.sdl-dos-ports/shared/tests/`.
- Hardware diagnostics (VESA/DAC/sound-card probes) if you hit unexplained
  real-hardware behavior: `.sdl-dos-ports/shared/tests/probes/`.
- Run telemetry: `.sdl-dos-ports/shared/include/runmanifest.h` — a
  header-only, drop-in library for a standardized, greppable
  `[RUNMANIFEST-BEGIN]`/`[RUNMANIFEST-END]` block (fps percentiles, build
  fingerprint, environment, exit code) emitted once at clean shutdown. See
  `shared/skills/dos-hardware-validation/references/runmanifest-log.md`.
  **Wire this in at the STARTS/TITLE_SCREEN milestone, not later as part
  of OPTIMIZING** — a port that hand-rolls its own fps/frame-timing
  computation from the start (rather than using the shared header) risks
  the exact trap dossage/Passage hit: its own engine-side fps math turned
  out to be entangled with the very clock bug a later investigation spent
  hours chasing, because the number being chased and the mechanism under
  suspicion were the same unreviewed code. Using the shared header from
  the start doesn't prevent every such bug, but it means your baseline
  measurement isn't itself bespoke, unreviewed code.

**Default to in-process subagents, not a multi-session agent team.** A
single Claude Code session driving the slices above, delegating focused
sub-tasks (a patch investigation, a build/smoke pass, a log analysis) to
in-process subagents as needed, is the default working mode for a port of
this scale — it doesn't need a standing coordinator or dedicated sessions
per role. Copy `templates/PORT-CLAUDE.md` as your repo's `CLAUDE.md` to
start.

`shared/agents/` holds full AI agent-team charters (a team-lead plus five
or six specialist roles, each meant to run as its own coordinated session)
carried over from doskutsu's own campaign. Treat this as an **opt-in
escalation**, not the default: stand it up only once a port's real-hardware
campaign has grown large enough that a single session genuinely can't track
all the concurrent specialist work (see `shared/agents/README.md`), not at
the start of a new port. In particular, don't give each specialist its own
separate remote environment by default — that multiplies cost and
rig-coordination risk (see
`shared/skills/dos-rig-operations/references/agent-coordination.md`) for
ports that don't need it.

**A validated middle shape exists for the real-hardware performance-
campaign phase specifically**, distinct from both the solo-plus-subagents
default and the full team-lead-plus-specialists escalation: **one
investigating session (the port's own) plus one dedicated rig-operator
session**, coordinating peer-to-peer rather than through a standing
coordinator. This is not a hypothetical middle option — it's what
actually happened during dossage/Passage's real-hardware fps campaign,
and the split earned its keep: a dedicated rig-operator session can
independently apply hash-verification and witness discipline (see
`dos-hardware-validation`'s per-cell rules) without the investigating
session having to context-switch into rig mechanics, and having a
*separate* session doing that verification is what caught a real build
bug (see `shared/agents/probe-engineer.md`'s stubify hazard) before it
reached real hardware. Reach for this shape once a port is doing genuine
iterative real-hardware investigation (repeated hypothesis-test-measure
rounds), not for a single one-off validation run. See the `/sdldos:benchmark`
skill for the full campaign checklist this shape is part of.

## 5. Real-hardware QA

Once you reach `PLAYABLE`, follow [`docs/hardware-testing.md`](docs/hardware-testing.md)
to validate on real hardware via vcctrl, using
[`templates/vcctrl-profile.yaml.template`](templates/vcctrl-profile.yaml.template)
as your project's profile. Record results with
[`templates/BENCHMARK.md`](templates/BENCHMARK.md). Once a port needs to
chase a specific performance KPI rather than a one-off check, run
`/sdldos:benchmark` — the campaign checklist distilled from a real one, tying
together `docs/optimization.md`'s KPI-writing policy and the
`dos-hardware-validation` skill's investigation/rig discipline.

## 6. Showcase it

Once the port is playable, add `gallery/<name>/` (screenshots, a short
README covering features/performance/hardware notes) so the next porter can
see what's possible.
