# Skill templates

Genericized Claude Code skills, same adoption model as `shared/agents/`
(see its own `README.md`): these are process templates distilled from real
campaign experience on one port, stripped of that port's specific numbers
and names, meant to be copied (or symlinked) into a port repo's own
`.claude/skills/` rather than invoked from inside this hub repo directly.

A skill here only helps if a port actually has it wired up locally --
this hub is a source of truth for the *content*, not a place these run
from day to day.

| Skill | What it covers | Reusable as-is vs. needs a per-port fill-in |
|---|---|---|
| `dos-hardware-validation/` | Running a real-hardware validation or A/B benchmark campaign on a vcctrl-controlled rig: design-before-run discipline, populate/verify, per-cell set/forbid/expect_log witnessing, RUNMANIFEST-driven metrics, ABBA methodology, bug-vs-workaround discipline, raw-data handoffs | Reusable close to as-is -- the harness protocol and methodology are port-agnostic. Fill in your own RUNMANIFEST field names/env-var names if you don't reuse `DOS_PORT_*` (see `docs/patch-conventions.md`'s naming-debt note), and your own vcctrl profile per `templates/vcctrl-profile.yaml.template` |
| `dos-rig-operations/` | Day-to-day rig mechanics independent of a full campaign: input-injection landed-vs-dropped detection, screen-capture evidence discipline, file transfer + packaging/handoff, log collection, power management, and multi-agent coordination (a spawned specialist doesn't inherit rig access; single-coordinator-per-campaign to prevent double-dispatch) | Reusable as-is -- mostly points at vcctrl's own `vcctrl-rig-hazards`/`vcctrl-common-workflows` for mechanics and adds the port-session framing/hazard classes on top |
| `dos-realhw-verification/` | Knowing a build/fix/diagnosis is actually correct on real hardware, not just apparently correct: two-witness build verification, stale-cache failure shapes, DOSBox-X/86Box tiering, build-host tooling traps, real-hardware-vs-emulator divergence debugging | Reusable as-is -- the epistemics (what a check's failure would look like) and failure-shape catalog are port-agnostic; the worked examples are illustrative, not something to copy literally |
| `dos-emulator-workflow/` | Local, no-rig-required DOSBox-X development: which of the three `shared/tools/dosbox-*.sh` scripts to reach for, the emulator-only escape hatches already baked into the shipped confs (and why they must never reach real hardware), the "necessary but not sufficient" pattern for a probe that can only partially answer a hardware question locally, and local-timing observation vs. an actual performance claim | Reusable as-is for the tooling/mechanics and the DOSBox-X/hardware boundary; the shipped `.conf` files' own `cycles=`/video/sound calibration is one port's reference-machine numbers, not a universal setting -- re-calibrate for your own port |
| `review/` | Checking a patch (a port's own vendored `patches/SDL/`, `patches/<engine>/`, or this hub's own `shared/patches/` reference series) against this hub's landing conventions before committing: provenance verification, DJGPP hard constraints, neutral naming, slot numbering, temporary-diagnostic removal planning | Reusable as-is -- `/sdldos:review` via the plugin |
| `benchmark/` | Running a real-hardware performance KPI campaign: KPI writing, team shape, investigation/rig discipline, recording the result | Reusable as-is -- `/sdldos:benchmark` via the plugin |

## Adopting a skill into a port repo

**Default: the `sdldos` plugin.** This hub is a real Claude Code plugin
(`.claude-plugin/plugin.json`, name `sdldos`; the root `skills/`
directory is its skill set -- the hub-only `port` skill plus symlinks
into the entries above, one source of truth). A port repo enables it
with a tracked `.claude/settings.json` that only turns it on:

```json
{ "enabledPlugins": { "sdldos@sdl-dos-ports": true } }
```

The marketplace URL points at the private tailnet Forgejo, and port repos
are mirrored publicly, so it lives in the untracked
`.claude/settings.local.json` instead (placeholder host shown; use the
hub's real `origin` URL):

```json
{
  "extraKnownMarketplaces": {
    "sdl-dos-ports": {
      "source": { "source": "git",
                  "url": "https://forgejo.example.ts.net/ecliptik/sdl-dos-ports.git" }
    }
  },
  "enabledPlugins": { "sdldos@sdl-dos-ports": true }
}
```

`scripts/new-port.sh` writes both files, and the `.gitignore` rule that
lets it be tracked while the rest of `.claude/` stays local (`.claude/*`
plus `!.claude/settings.json` -- git can't re-include a file under an
ignored parent directory, so a bare `.claude/` line would swallow it).
Anyone who opens and trusts the repo (with the local file in place) is offered the plugin and gets
`/sdldos:port`, `/sdldos:review`, `/sdldos:benchmark`,
`/sdldos:dos-hardware-validation`, ... -- no per-repo copy of any
`SKILL.md`. The installed plugin is a snapshot keyed by the `version`
in `.claude-plugin/plugin.json`: `/plugin update sdldos` fetches a new
version and reports "already at the latest version" otherwise, so every
skill or layout change here must bump that version (1.0.0 -> 1.0.1 was
the first such bump, 2026-09-03; 1.0.2, 2026-09-17, added the benchmark
skill's `references/self-driving-loop.md`; 1.0.3, same day, the
post-FTP keyboard-hang recovery procedure) or it never reaches an
installed copy.
`claude --plugin-dir <path-to-this-hub>` loads the working tree for one
session instead (it overrides the installed copy), which is how to test
a skill edit here before pushing it. The marketplace install copies the
whole repo into Claude Code's plugin cache with the `skills/` symlinks
preserved, so they resolve there too (verified 2026-09-03).

**vcctrl's skills are not a plugin** and still install flat, per repo,
via `npx skills add <vcctrl repo> --full-depth -a claude-code -s
vcctrl-mcp-workflows -s vcctrl-common-workflows -s vcctrl-rig-hazards -s
vcctrl-camera` (`--full-depth` because that repo has no root-level
`SKILL.md`; explicit `-s` flags, not `--all`, because vcctrl also ships
`vcctrl-repo-conventions` and `vcctrl-webkvm-copy` -- its own contributor
conventions, not anything a port repo needs);
`scripts/new-port.sh` runs this at scaffold time. A port picks up new
vcctrl skills with `npx skills update`.

**Skills are knowledge; real-hardware access is a separate, deliberate
step.** `scripts/new-port.sh` will also register vcctrl's MCP daemon
automatically *if* `VCCTRL_MCP_URL` is set in the environment when it
runs -- but there is no default for that variable anywhere in this repo,
unlike `VCCTRL_REMOTE` above, and there never will be: the rig's real
MCP endpoint is a secret vcctrl's own operator deliberately keeps out of
every tracked file (mirroring why `vcctrl.yaml` itself stays gitignored
on their side), and that boundary is theirs to hold, not this hub's to
cross by baking a literal into a committed script. Left unset (the
default), registration is skipped cleanly -- see vcctrl's own
`MCP-SERVER.md` sec 5 for manual registration once you actually have the
real URL. This is intentional: a fresh port repo gets vcctrl's
*knowledge* automatically, not ungated *access* to physical hardware.

A port scaffolded *before* the plugin existed adopts it by adding the
`.claude/settings.json` above (and, if its `.gitignore` has a bare
`.claude/` line, changing that to `.claude/*` + `!.claude/settings.json`
so the file can be tracked), then deleting whatever flat copies of this
hub's skills it carried -- `npx skills`-installed `.agents/skills/<name>`
directories and `.claude/skills/<name>` symlinks for `port`, `review`,
`benchmark` and the `dos-*` entries -- so Claude Code doesn't show two
entries per skill. vcctrl's skills stay as they were.

**For an agent that can't load Claude Code plugins** (Codex, Cursor, or a
first trust with no network), the flat path still works and reads the
same files: `npx skills add
https://forgejo.example.ts.net/ecliptik/sdl-dos-ports.git --full-depth
--all -a <agent>` from the port's root; or, with no `npx` at all,
`.sdl-dos-ports/shared/scripts/sync-skills.sh` symlinks this hub's
`shared/skills/` entries into `.claude/skills/` (hub skills only -- no
vcctrl, and no `port`, which is hub-side by design). Expect duplicate
entries if the plugin is also enabled. The one caveat of symlinked skill
directories: anything that enumerates files by walking them needs `-L`
(GNU `find`) or the equivalent, or it silently sees nothing.

## Provenance

`dos-hardware-validation/` and `dos-rig-operations/` were distilled from
the vcctrl side of a real multi-hour real-hardware session validating
doskutsu on the vcctrl rig (Gateway 2000 / POD-83, ATI Mach64) during
doskutsu's `shared/` submodule migration -- an 8-cell ABBA fps campaign,
four real harness bugs found and fixed along the way, and vcctrl's own
prioritized read on what else in its rig-operations surface was worth
generalizing.

`dos-realhw-verification/` was distilled from doskutsu's side of the same
migration window -- its own build/QA and hardware-vs-emulator debugging
campaign history, including a fix that shipped twice on unconfirmed
static hypotheses before real-hardware markers refuted them, and a
multi-round investigation that initially misattributed a bug to a video
chipset before real-hardware surface dumps found the actual cause in the
port's own fast-path code.

Port-specific details (doskutsu's own env var names, the specific fps
numbers, specific bug tickets) were stripped the same way the agent
charter templates strip doskutsu's wave-by-wave findings -- see
`shared/agents/README.md`'s framing. The underlying discipline is what's
shared; the campaign history belongs to whichever port lived it.

`dos-emulator-workflow/` differs in provenance from the other three: it
wasn't distilled from a single incident, but extracted from gotchas
already documented inline in this hub's own `shared/tools/dosbox-*.sh`
comments and `shared/tests/dpmi-lfn-smoke/README.md` (the global-`pkill`
hazard, the `2>&1`-under-DOSBox-X caveat, the `SDL_DOS_AUDIO_SB_SKIP_DETECTION`
escape hatch, the "necessary but not sufficient" probe framing) --
material that was already real and already correct, just not yet
surfaced as something a session would actually consult before starting
local DOSBox-X work.
