# sdl-dos-ports

A hub for porting SDL-based games to MS-DOS, built on the SDL3 DOS backend
proven by [doskutsu](https://github.com/ecliptik/doskutsu) (Cave Story on
real 486/Pentium-class DOS hardware via SDL3, DJGPP, and CWSDPMI).

This repo hosts the reusable platform layer, tracks port status, and
documents the shared porting strategy -- it contains no game source
itself. Each port lives in its own repository, wired to
[`shared/`](shared/) via git subtree at `.sdl-dos-ports/` (or, for a port
scaffolded before 2026-08-31, a submodule -- see `scripts/new-port.sh`).

See [`CLAUDE.md`](CLAUDE.md) for the rules an AI agent (or contributor)
should follow when working in this repo.

## Ports

| Priority | Project | Upstream | License | Port repo | Status | Difficulty |
|---:|---|---|---|---|---|---:|
| 0 | doskutsu (Cave Story) | [nxengine/nxengine-evo](https://github.com/nxengine/nxengine-evo) | GPL-3.0 (verified) | [ecliptik/doskutsu](https://github.com/ecliptik/doskutsu) | RELEASE_READY — 14.6-32.2 fps | reference |
| 1 | Adventure Game Studio | [adventuregamestudio/ags](https://github.com/adventuregamestudio/ags) | Artistic-2.0 (verified) | ecliptik/dosags (private) | PLAYABLE — 40Hz/40fps plan in progress on the 486DX2-66 | 5 |
| 2 | VVVVVV | [TerryCavanagh/VVVVVV](https://github.com/TerryCavanagh/VVVVVV) | unverified — see caution below | unclaimed | BACKLOG | 3 |
| 3 | Meritous | TBD | unverified | unclaimed | BACKLOG | 2 |
| 4 | OpenJazz | [OSSGames/GAME-SDL-openjazz](https://github.com/OSSGames/GAME-SDL-openjazz) | unverified | unclaimed | BACKLOG | 2-3 |
| 5 | POWDER | TBD | unverified — may not be open source, see `ports.yaml` | unclaimed | BACKLOG | 2 |
| 6 | Kobo Deluxe | TBD | unverified | unclaimed | BACKLOG | 3-4 |
| 7 | Blob Wars: Metal Blob Solid | TBD | unverified | unclaimed | BACKLOG | 3-4 |
| 8 | Passage | [jasonrohrer/Passage](https://github.com/jasonrohrer/Passage) | public domain (verified) | [ecliptik/dossage](https://github.com/ecliptik/dossage) | OPTIMIZING — 14.97-15.02 fps | 1 |
| 9 | SuperTux 0.1.x | TBD | unverified | unclaimed | BACKLOG | 4 |

Generated from [`ports.yaml`](ports.yaml), the source of truth. "Unverified"
means nobody has yet read that project's actual LICENSE/COPYING file in this
repo — **never treat it as known**, see [`docs/licensing.md`](docs/licensing.md).
VVVVVV in particular has a history of source-available terms that aren't
automatically redistribution rights; check its `ports.yaml` entry first.

Status benchmarks are each port's best real-hardware fps range across the
reference CPUs (see [`gallery/`](gallery/) for doskutsu's full per-CPU/
audio-backend breakdown); [`COMPATIBILITY.md`](COMPATIBILITY.md) has the
fuller boots/playable/hardware matrix.

## Getting started

```sh
# 1. Claim a candidate and scaffold its repo (run from this hub repo)
Use the port skill: /sdldos:port

# 2. Open the new port repo and trust it -- Claude Code offers the sdldos
#    plugin (this hub's skills, namespaced) from the repo's own
#    .claude/settings.json; vcctrl's skills are already installed flat
cd ../<name>
ls .claude/skills/          # vcctrl-* (flat); this hub's are /sdldos:*

# 3. Build in narrow slices: compile -> video -> input -> filesystem
#    -> audio -> gameplay. See PORTING.md.

# 4. Chasing a real-hardware fps/perf target?
Use the benchmark skill: /sdldos:benchmark

# 5. Landing a patch in shared/ or your own patches/<engine>/?
Use the review skill: /sdldos:review
```

| Skill | Runs from | Use it to |
|---|---|---|
| `/sdldos:port` | This hub repo only | Claim a `BACKLOG` candidate and scaffold its repo, charters, and vendor pins. |
| `/sdldos:review` | This hub, or any port repo | Check a patch against this hub's own landing conventions before committing it (provenance, DJGPP constraints, naming, numbering). |
| `/sdldos:benchmark` | This hub, or any port repo | Run or record a real-hardware performance KPI campaign. |
| `/sdldos:dos-emulator-workflow`, `/sdldos:dos-hardware-validation`, `/sdldos:dos-rig-operations`, `/sdldos:dos-realhw-verification` | Any port repo | Local DOSBox-X work, rig campaigns, day-to-day rig mechanics, and real-hardware verification discipline. |

**How the skills get there.** This repo is itself a Claude Code plugin
(`.claude-plugin/plugin.json`, name `sdldos`; root `skills/` is its skill
set). Every port scaffolded by `/sdldos:port` carries a tracked
`.claude/settings.json` that enables this hub as a plugin marketplace, so
opening and trusting that repo (or this one) offers every skill above
under the `sdldos:` prefix, with no per-repo `SKILL.md` copies. To install
it by hand elsewhere:

```sh
/plugin marketplace add https://forgejo.example.ts.net/ecliptik/sdl-dos-ports.git
/plugin install sdldos@sdl-dos-ports
```

(`forgejo.example.ts.net` stands in for the private tailnet Forgejo;
use the hub's real `origin` URL.)

A skill or layout change here only reaches installed copies once the
`version` in `.claude-plugin/plugin.json` is bumped and `/plugin update
sdldos` is run; `claude --plugin-dir <path-to-this-hub>` loads the working
tree for one session instead, for trying an edit before pushing. vcctrl's
skills aren't part of this plugin -- `/sdldos:port` installs them flat via
`npx skills`. Ports scaffolded before the plugin existed, or agents that
can't load plugins (Codex, Cursor), are covered in
`shared/skills/README.md`.

Deeper reference, only when a skill points you at it:
[`PORTING.md`](PORTING.md) for the full slice-by-slice porting process.

## Repository layout

```
sdl-dos-ports/
├── ports.yaml         # candidate backlog + status tracker (source of truth)
├── docs/              # architecture, video/audio/input/filesystem/timing, testing, licensing
├── shared/            # the reusable SDL3-DOS platform layer -- port repos consume this via subtree
├── templates/         # scaffolding for a new port repo (STATUS/PLAN/CLAUDE/benchmark/license-review templates)
├── gallery/            # per-port showcase: screenshots, performance, features
└── scripts/           # scripts/new-port.sh and friends
```

## Reference hardware

Primary targets, shared across all ports (see [`HARDWARE.md`](HARDWARE.md)
for the full matrix and real-hardware testing process via
[vcctrl](https://github.com/ecliptik/vcctrl)):

| ID | CPU | Role |
|---|---|---|
| HW-486-50 | 486DX2-50 | low-end torture test |
| HW-486-66 | 486DX2-66 | primary minimum target |
| HW-POD83 | Pentium OverDrive 83 | upgrade-path target |
| HW-5X86 | AMD Am5x86-133 | fast 486 platform |
| HW-P75 | Pentium 75 | recommended target |

## License

See [`LICENSE`](LICENSE) for this repository's own code and docs, and
[`THIRD-PARTY.md`](THIRD-PARTY.md) for the licensing model applied to
vendored/patched third-party sources in `shared/`. Every port repo tracks
its own game engine and asset licensing separately in its own
`LICENSE-REVIEW.md`.
