# Agent charter templates

**Opt-in, not the default.** A new port's default working mode is a single
session using in-process subagents for focused sub-tasks — see
`PORTING.md`'s step 4. Stand up this full team (a team-lead session plus
five or six specialist sessions, each with its own charter and — if run via
remote environments — its own cost and rig-coordination surface) only once
a port's real-hardware campaign has grown large enough that a single
session genuinely can't track all the concurrent specialist work. Most
ports, including small ones like Passage, will not need this.

**There's a validated middle shape for a real-hardware performance
campaign specifically, between solo-plus-subagents and this full team:
one investigating session plus one dedicated rig-operator session,
coordinating peer-to-peer.** This isn't hypothetical — it's the shape a
real dossage/Passage fps campaign actually used, and it earned its keep:
a dedicated rig-operator session applies hash-verification/witness
discipline independently, without the investigator context-switching into
rig mechanics, and that independence is what caught a real build defect
(see `probe-engineer.md`'s stubify-stack-size hazard) before it reached
real hardware. Reach for this once a port is doing genuine iterative
real-hardware investigation, not a one-off validation run — run the
`benchmark` skill for the full campaign checklist.

These are genericized versions of the AI agent-team charters doskutsu
developed over its own porting campaign. Copy them into a port repo's
`.claude/agents/` and fill in the placeholders — they're process templates,
not doskutsu's actual campaign history (KPI numbers, wave-by-wave findings,
specific file line numbers have been stripped; those belonged to doskutsu's
own `docs/internal/`, not to a shared template).

| File | Role | Reusable as-is vs. needs a per-port fill-in |
|---|---|---|
| `team-lead.md` | Wave/phase coordinator, gate decisions, specialist arbitration | Fill in your specialist roster and doc paths |
| `sdl-engine.md` | Owns this port's own vendored `patches/SDL/` (seeded once from `shared/patches/sdl3-dos/` at scaffold time, not a live link) | Reusable close to as-is |
| `ENGINE-SPECIALIST.md.template` | Owns your game engine's own patches | Fill in engine name, source paths, file-format specifics |
| `build-qa.md` | Cross-build + DOSBox-X smoke + visual A/B | Fill in your binary name and expected banner/log lines |
| `realhw.md` | Release packaging + real-hardware handoff | Fill in transfer mechanism (vcctrl by default — see `docs/hardware-testing.md`) and BAT/launcher naming |
| `probe-engineer.md` | Standalone DJGPP diagnostic probes | Reusable close to as-is |
| `PERF-CAMPAIGN.md.template` | Diagnostic/instrumentation coordination for a perf-focused wave | Only stand this up once you're actually doing profiling-driven optimization (see `docs/optimization.md`) |

## Conventions carried over from doskutsu that these templates assume

- **Narrow charters.** Each specialist has a lane; "what you do NOT do" is
  as load-bearing as the charter itself.
- **Team-lead doesn't do the work.** It sequences, arbitrates, and
  synthesizes; specialists author patches/builds/packages.
- **STOP-and-ack contract narrowing.** A specialist that decides to narrow
  or reinterpret its brief mid-task says so and gets acknowledgment before
  writing code, rather than silently substituting its own judgment.
- **Hypotheses, not predictions.** Performance claims are measured, not
  estimated — see `docs/optimization.md` and `docs/timing.md`.
- **Never contribute upstream.** Every specialist's patches are
  workspace-local — see `CLAUDE.md`'s "Never contribute upstream" section.
- **A project-local durable-lessons doc**, analogous to doskutsu's own
  `docs/internal/` findings docs, is worth standing up once a port has
  enough campaign history to need one — these templates don't assume one
  exists yet.
