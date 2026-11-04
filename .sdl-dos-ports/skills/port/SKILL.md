---
name: port
description: >
  Bootstrapping a new DOS port from a ports.yaml BACKLOG candidate all the
  way to "a real, wired-up repo with a porting agent ready to start the
  compile slice" -- upstream/license verification, scripts/new-port.sh's
  mechanical scaffold, skill wiring (this hub's skills via the sdldos
  plugin enabled in .claude/settings.json, vcctrl's via npx skills),
  shared/agents charter
  instantiation, vendor/sources.manifest pins, the ports.yaml claim commit,
  and a concrete handoff message to the port repo's own agent. Use this
  whenever claiming a new port candidate, starting a new port repo, or
  asked to "start/bootstrap/scaffold a new port" -- run it from this hub
  repo, not from inside the new port repo.
---

# Bootstrapping a new port

This closes the gap between `scripts/new-port.sh` (which only does the
mechanical repo/subtree/template scaffold) and an actual working session
in the new repo. Run this skill from the sdl-dos-ports hub, not from
inside the new port repo -- everything it produces gets written into the
target repo from here, and the final step hands off to whatever agent is
running there.

Follow `CLAUDE.md`'s agent rules throughout: never guess an upstream URL
or license, keep changes in narrow buildable slices, and remember that
`shared/`'s own rules (nothing game-specific, DOS-specific behavior stays
`#ifdef`-guarded, etc.) don't apply here -- this skill only ever writes
into the new port's own repo and this hub's `ports.yaml`, never into
`shared/`.

## Inputs

- The candidate's name from `ports.yaml` (must exist there; add it first,
  following the existing schema, if it doesn't).
- Where the new repo should live (defaults to `../<name>`, a sibling of
  this hub repo, matching `scripts/new-port.sh`'s own default) and whether
  it already exists -- the operator may have already run `git init` or
  even `scripts/new-port.sh` by hand before invoking this skill. Check
  before assuming a fresh scaffold; adapt to whatever's already there
  rather than overwriting it.
- Whether a porting agent is already running in that repo (check
  `ListAgents` for a session named after the port, or ask the operator).
  This skill's final step hands off to that agent -- if none exists yet,
  it ends by telling the operator what to start and what to paste in.

## Step 1: Verify upstream + license (never guess)

If `ports.yaml`'s entry for this candidate still has `upstream_url: null`
or `upstream_license_verified: false`, this is the actual first task, not
scaffolding:

1. Locate the canonical upstream repository. Don't guess a URL from
   memory -- search for it, confirm it's the real canonical source (not a
   fork, mirror, or unrelated same-named project), and pick a
   DOS-appropriate revision (recent enough to be maintained, old enough to
   avoid dependencies that fight DJGPP -- see what similar candidates in
   `ports.yaml` picked for precedent).
2. Fetch that revision (a shallow clone is enough for this step) and
   **actually read its LICENSE/COPYING file** at the pinned SHA. A
   remembered or commonly-believed license is not verification -- see
   `docs/licensing.md`. Record what it actually says, including any
   dual-licensing or per-file exceptions.
3. Update the candidate's `ports.yaml` entry: `upstream_url`,
   `upstream_revision` (a real SHA, not a branch name), `upstream_license`,
   `upstream_license_verified: true`, and a `notes:` line citing where you
   read the license. Commit this in the hub as its own small commit --
   don't bundle it with the scaffold commit below.

If these fields are already filled in (someone else already claimed and
verified this candidate), skip to Step 2 -- but spot-check the recorded
SHA still resolves and the license note looks like it came from actually
reading the file, not a guess, before trusting it.

## Step 2: Mechanical scaffold

```sh
scripts/new-port.sh <name> [target-dir]
```

This creates the repo (or, if the operator already created one, see the
adaptation note above -- run the equivalent steps by hand: subtree add,
directory skeleton, vendoring `patches/SDL`+`patches/SDL_mixer` as real
files copied from this hub's own `shared/patches/`, skill install, and
the template fills), wired to this hub via `.sdl-dos-ports/` (a git
subtree -- no `.gitmodules`, content merged directly into the port's own
git history, reachable immediately after a plain `git clone`) for
everything in `shared/` *except* patches, which the new repo now owns as
its own standalone copy (see `docs/patch-conventions.md`'s "Patches are
vendored per-port, not shared" -- no ongoing sync back to this hub's
series once scaffolded). This step also writes the new repo's `.claude/settings.json`, which
registers this hub as a plugin marketplace and enables the `sdldos`
plugin (so `/sdldos:review`, `/sdldos:benchmark`, ... are offered to
anyone who opens and trusts the repo -- no per-repo copy of any
`SKILL.md`), plus the `.gitignore` rule that lets that one file be
tracked, and installs vcctrl's four hardware-portable skills flat via
`npx skills add <repo> --full-depth -a claude-code -s vcctrl-mcp-workflows
-s vcctrl-common-workflows -s vcctrl-rig-hazards -s vcctrl-camera` (not
`--all` -- that would also pull in `vcctrl-repo-conventions` and
`vcctrl-webkvm-copy`, which are about vcctrl's own contributor
conventions, not this port). Don't redo either by hand.

Skill *knowledge* is auto-installed; real-hardware *access* is not, and
that's deliberate, not a gap. If `VCCTRL_MCP_URL` is set in the
environment when this step runs, it registers vcctrl's MCP daemon
automatically; if unset (the default), it's skipped cleanly with a
pointer to vcctrl's own `MCP-SERVER.md` for manual registration. This
repo never carries the rig's real MCP endpoint in tracked files --
that's vcctrl's own operator's boundary, not this hub's to cross (see
`shared/skills/README.md` if you're setting this up and want the why in
full).

## Step 3: Instantiate agent charters

`shared/agents/README.md` has the full table of what to copy and how
reusable each one is as-is. Copy every file in `shared/agents/*.md` (not
the `.template` files yet -- see below) into the new repo's
`.claude/agents/`, then fill in the per-port placeholders:

- `team-lead.md` -- the specialist roster (which of the charters below
  actually exist for this port) and doc paths.
- `sdl-engine.md` -- reusable close to as-is; it owns the port's own
  vendored `patches/SDL` copy from Step 2.
- `build-qa.md` -- this port's actual binary name and the banner/log
  lines a smoke check should expect (you won't know the real ones until
  compile succeeds; a reasonable placeholder now, refined once the
  porting agent has a first build).
- `realhw.md` -- transfer mechanism (vcctrl by default, per
  `docs/hardware-testing.md`) and this port's BAT/launcher naming.
- `probe-engineer.md` -- reusable close to as-is.

Copy `ENGINE-SPECIALIST.md.template` to `.claude/agents/<engine-name>-engine.md`
and fill in the engine's name and source paths -- this needs you to have
actually looked at the vendored engine's directory layout (fetch it if you
haven't yet), not guessed at a conventional structure. Leave
`PERF-CAMPAIGN.md.template` uninstantiated; per its own table entry it
only gets stood up once the port is actually doing profiling-driven
optimization, which is nowhere close to true yet.

These charters and the `.claude/skills/` symlinks are gitignored in
every port repo (matching doskutsu's own convention) -- don't fight that
by trying to force-add them. The one tracked file under `.claude/` is
`settings.json`, the plugin enablement; whether vcctrl's npx-installed
`.agents/skills/` copies are tracked is the port's own call.

## Step 4: Pin `vendor/sources.manifest`

`scripts/new-port.sh` leaves `SDL`/`SDL_mixer`/`SDL_image` as `PIN_ME`
placeholders. The SDL/SDL_mixer pins must match whatever SHA
`shared/patches/sdl3-dos/` and `shared/patches/sdl3-mixer/` were actually
built against -- don't independently pick a "recent" SDL revision, since
the patch series won't apply against an arbitrary one. Check another
already-pinned port's manifest (doskutsu's or dossage's
`vendor/sources.manifest`) for the current shared pin and match it
exactly, or check the patches' own `git format-patch` headers if no
sibling port is available. If this candidate's engine doesn't use
`SDL_image`, remove that manifest line rather than leaving a `PIN_ME` that
never gets resolved. Add the engine's own entry using the SHA verified in
Step 1.

## Step 5: Write real content, not template placeholders

`scripts/new-port.sh` only substitutes `<NAME>`/`<name>` into
README.md/STATUS.md/PLAN.md/LICENSE-REVIEW.md -- it doesn't write the
actual content. Fill in enough real substance that the porting agent
being handed off to isn't starting from a blank page: a real summary of
what the engine is and its SDL surface, the license terms from Step 1 in
LICENSE-REVIEW.md, and PLAN.md's slice breakdown adapted to whatever's
actually true about this engine (does it use SDL_mixer at all? Threads?
A custom asset format?). This does not need to be
`plans/SDL_DOS_PORTING_PROGRAM.md`-section-18 depth for a small candidate
-- PORTING.md's own guidance is "for something as small as Passage it's a
page." Deeper research (a full RESEARCH.md/SDL-SURFACE.md/etc. for a large
engine) is the handed-off porting agent's job, not this skill's -- don't
try to front-load all of PORTING.md's step 3 here.

## Step 6: Push and claim in `ports.yaml`

Set the new repo's git remote and push, if it has one configured and the
operator hasn't said they'll do this part themselves. Then, in this hub
repo: update the candidate's `ports.yaml` entry (`dos_status: RESEARCH`,
`port_repo_url` set to the real pushed URL) and commit. This is the
"claim" a future session should see when checking `ports.yaml` for
available candidates -- don't leave it stale at `BACKLOG` after doing all
of the above.

## Step 7: Hand off to the porting agent

Check `ListAgents` for a session already running in the new repo (the
operator may have started one before invoking this skill, per this
skill's own description). If one exists, send it a concrete brief via
`SendMessage` -- not "go start porting," but the actual state: what's
scaffolded, where its own `.claude/agents/` charters live and that this hub's
skills arrive as `/sdldos:*` through the plugin, what Step 5's PLAN.md says, and that its first task is
`PORTING.md`'s step 3 (research) feeding into step 4's compile slice. If
no agent is running yet, tell the operator what to start (a session with
its working directory set to the new repo) and give them the same brief
text to paste in once it's up, rather than leaving them to reconstruct it
from what you did.

## What this skill does not do

- Deep engine research (`RESEARCH.md`/`SDL-SURFACE.md`/`DEPENDENCIES.md`/
  `PLATFORM-SURFACE.md` for a large engine) -- that's the handed-off
  porting agent's Step 3, this skill only gets it started.
- Any actual porting work (compile, video, input, ... per `CLAUDE.md`'s
  slice order) -- that's the handed-off agent's job entirely.
- Anything inside `shared/` -- this skill only ever touches the new port
  repo and this hub's `ports.yaml`.
