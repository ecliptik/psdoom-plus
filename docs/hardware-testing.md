# Hardware testing (vcctrl integration)

This documents how real-hardware validation should work across multiple
ports. `harness/vcctrl-sweep|-cell|-collect` and the `bin/vcctrl-*` tools
that touch project-specific paths were generalized to read a profile
instead of hardcoding doskutsu's own values (see "The orchestration
layer" below) as part of dosags's own first real-hardware run.

## What vcctrl is

[vcctrl](https://github.com/ecliptik/vcctrl) is a remote-control/
automation harness for a physical vintage PC, driven by a CLI, an MCP
server (for agent control), or a browser KVM. Forgejo is the canonical,
live repository; a [GitHub mirror](https://github.com/ecliptik/vcctrl)
went live 2026-09-11 (confirmed via a direct `git ls-remote`, matching
Forgejo's `main` exactly) — Forgejo stays where changes land first. It
provides:

- **Keyboard/mouse input** injected over PS/2 via a USB4VC bridge, so an
  agent or script can drive the DOS machine as if typing at the console.
- **Screen capture** via a USB VGA capture stick, with a scrub-ring buffer
  (still/burst/timed recording).
- **Audio capture**, historically a coarse liveness signal (silence vs.
  music) via raw level alone, not a quality judgment. Now also exposes
  frequency-aware content detection (below) that distinguishes real
  music/SFX from a steady tone/hum at the same volume — level alone
  can't tell those apart, since it only proves *something* is on the
  wire.
- **Power control** via a smart plug, scoped so a power action can't hit
  the wrong machine.
- **File transfer** over FTP, always initiated from the DOS side via mTCP
  (nothing is pushed to the target unsolicited).
- A **DOS liveness signal** using the PS/2 keyboard LED return channel,
  since there is no serial console to poll.

Today it targets one physical rig (one machine, hand-swapped CPUs tracked
as machine IDs matching this repo's `HARDWARE.md` matrix — 486DX2-50,
486DX2-66, Pentium OverDrive 83, Am5x86-133).

## Config layers (already generic, reuse as-is)

- **`vcctrl.yaml`** — one per **physical rig** (gitignored, lives on the
  control host, not tracked in any repo). Declares the capability backends
  (input/leds/power/video/audio/files/board) and a `harness.profile` key
  pointing at which project profile is active.
- **`profiles/<name>.yaml`** — one per **software project**, tracked in
  that project's own repo (dosags's own `profiles/dosags.yaml`, dossage's
  own `profiles/dossage.yaml`) or, for a project that hasn't moved it out
  yet, in vcctrl's own `profiles/` directory (doskutsu's, still there —
  see "The orchestration layer" below). Holds project-specific facts: DOS
  working directory, env-var names the program reads (TAS replay, PRNG
  seed, log verbosity), boot-menu timing, the machine/CPU table for that
  rig, per-sweep wall-clock budgets.
  dossage's own profile was rewritten 2026-09-11 (a `vcctrl-harness-
  migration` follow-on to the work below) to replace guessed env-var
  names with facts verified against its own source/patches/campaign
  docs — see that file's own header and commit message for what changed
  and why; dossage's real-hardware campaign to date used vcctrl's
  generic MCP file-transfer tools directly, not vcctrl-cell.

This split already maps cleanly to a multi-port world: **one rig config,
many project profiles**, switched via `harness.profile`. A new port defines
its own profile using `templates/vcctrl-profile.yaml.template` (modeled on
doskutsu's own `profiles/doskutsu.yaml`, in doskutsu's own repo -- see
"The orchestration layer" below) and does not need to touch `vcctrl.yaml`
beyond pointing `harness.profile` at wherever the new profile ends up.

## The orchestration layer: generalized 2026-09-11 (dosags's slice 6)

`harness/vcctrl-sweep`, `-cell`, and `-collect` used to hardcode
`C:\DOSKUTSU\...` paths and doskutsu's own QA methodology: a `BLASTER`
boot-profile witness checked against a hardcoded PGSB value string, a
`CALL CLRENV` env-clear command, per-video-card `DOSKUTSU_PIN_NATIVE_MODE`
requirements, and `DOSKUTSU.CFG`/`DOSKUTSU.EXE` names. None of that was
fixable by renaming strings — it was doskutsu-specific QA methodology, not
generic DOS-testing wisdom, and a second port has no equivalent of most of
it yet. Reading the actual code (not just this doc's earlier description of
the gap) surfaced that the real coupling split three ways:

- **Genuinely reusable already**: `bin/vcctrl-cardid` reads the shared
  SDL3-DOS backend's own probe/detect log lines, identical across every
  port — "doskutsu" was only ever in its docstring.
- **Mechanical path substitution**: `vcctrl-collect`, `bin/vcctrl-cfclean`,
  `bin/vcctrl-uvconfig` just needed their hardcoded `C:\DOSKUTSU\...` paths
  and default incoming directory made profile-driven.
- **Substantive QA methodology**: `vcctrl-cell`'s env-clear command,
  config-file switching, per-card overrides, and boot-profile witness are
  now each an *optional*, profile-declared section in `profiles/<name>.yaml`
  (`target.env_clear_cmd`, `target.default_cfg`/`cfg_dest`,
  `target.card_overrides`, `target.profile_witness.value`,
  `target.exe`, `target.sweep_entry_cmd`, `env.capture_flag`) — a profile
  that doesn't declare one (dosags, today) makes the corresponding guard
  skip cleanly with a printed reason, rather than faking doskutsu's values
  or refusing outright. `profiles/doskutsu.yaml` carries its real,
  already-established values, so doskutsu's own cells are unaffected.
  `bin/vcctrl-score-pump` was found NOT to be reusable infrastructure at
  all — it is a bespoke, one-off analysis script for one already-completed
  doskutsu Pi5-migration experiment (hardcoded cell tags, a hardcoded
  baseline directory from one specific run, pass bands from that
  experiment's own pre-registered profile) and was left untouched.

**Explicitly still doskutsu-only, and not needed for dosags's own
first-run design**: `vcctrl-collect`'s legacy `--via-put` path
(`reboot_into_net()`/`verify_profile()`) reboots into a `NET` boot-menu
profile with a hardcoded menu digit and banner text. dosags's own harness
design (a single `tests/harness/` package run via `AGS.EXE --test`, no
multi-cell TAS sweep) uses the *default* fetch path instead, which already
delegates the equivalent reboot/verify mechanics to the daemon generically
— this legacy branch was left alone rather than generalized speculatively.

**Follow-on pass, same day (`vcctrl-harness-migration` fork): doskutsu's
own profile had a real, previously-invisible bug.** Verifying every field
just filled in against doskutsu's actual source (not just vcctrl-cell's
own hardcoded strings) found that `target.env`'s replay/seed/
auto_exit_tick/log_tag/pin_native_mode/shot_ticks names were all written
as `DOSKUTSU_<LEVER>` — copied from vcctrl-cell's own pre-generalization
strings — when doskutsu's actual engine (confirmed in
`vendor/nxengine-evo/src/{tas.cpp,main.cpp,graphics/Renderer.cpp}`) and
its real, currently-used QA batch files (`tests/qa/CLRENV.BAT`, `RB.BAT`)
all use the shared, neutral `DOS_PORT_*` naming instead. Practical effect:
any vcctrl-cell-driven diagnostic cell that intended to arm one of these
levers via the old names would have silently not armed it — fixed in
`profiles/doskutsu.yaml`, at the time still in the vcctrl repo (later
the same day, moved into doskutsu's own repo — see the next paragraph).
Running vcctrl's own test suite
during this pass also caught a real regression the generalization itself
introduced (`vcctrl-sweep` lost its `CONF` module attribute, which
`tests/test_core.py` asserts on) — fixed, and a reminder that "the tests
still pass" is a check worth actually running, not assuming, after a
cross-cutting refactor like this one.

## Manual workflow (works for any port, independent of vcctrl-cell)

For a one-off check that doesn't need vcctrl-cell's env/config/witness
guards, any port can drive real hardware using vcctrl's generic
file-transfer primitives directly:

```sh
vcctrl file-check                    # confirm the Pi's FTP server is up
vcctrl stage-file <PORT>.EXE         # sha256, DOS-8.3-rename, queue
vcctrl send-file --return            # reboot into NET profile, pull + verify, reboot back
vcctrl file-status                   # poll transfer completion

# ... exercise the game on the target, via the browser KVM or scripted
# keystrokes over the CLI/MCP layer ...

vcctrl file-refresh --return         # reboot, refresh target's OUT listing
vcctrl file-list
vcctrl get-file <RESULT> --return    # fetch a result/log/save file back
```

Screenshots/video: `vcctrl shot` / `lastgood` / `frame` / `burst` / `record`.

Audio liveness: `vcctrl level` (raw dB, tells you *something* is on the
wire, nothing about what). For content-aware checks, vcctrl now also
exposes frequency-based MCP tools — no human listener required to confirm
music/SFX is actually playing:

- `vcctrl_audio_verdict(ms=3000)` — the default reach-for-first tool.
  Answers NO_SIGNAL / SILENT / AUDIO_PRESENT, plus `tone_like` (true =
  steady tone/hum, false = real content, null = couldn't tell).
- `vcctrl_audio_match(file_path, ms=3000)` — control-mode only. Compares
  the live signal's frequency shape against a local reference audio file
  (a port's own music/SFX asset) and returns a similarity score. **Coarse
  by construction** — 8 bands can't distinguish two tracks with similar
  broad frequency balance, verifies nothing about tempo/melody/timing, and
  has no calibrated match/no-match threshold yet. Read it as "does the
  live signal's frequency balance resemble the reference's," never as
  "this exact track is playing right now."
- `vcctrl_spectrum(ms=3000)` — the raw 8-band (100Hz-12.8kHz octave-spaced)
  numbers underneath both of the above, plus `active_bands`. Compares
  bands to each other rather than to an absolute level, so it stays
  meaningful regardless of where a volume knob sits in the signal path.

Full detail/caveats: `vcctrl-common-workflows` skill, "Checking audio."
One sequencing trap worth knowing up front: on a title-screen-gated game, a
SILENT verdict (or a low match score) may just mean the start-music
keypress hasn't landed yet, not that audio is broken — confirm the
keypress registered (a visible on-screen change) before concluding a
fault. The underlying measurement is FFT-based (a per-band Hz *range*
sum) — an earlier single-frequency Goertzel-probe version passed every
synthetic-tone test but read real captured game music as complete
silence, because a multi-second analysis window has sub-Hz resolution and
real music essentially never sits on an exact probe frequency. If a port
ever extends this measurement further, validate against real captured
rig audio, not just synthetic tones, before trusting a new result (see
vcctrl's own `docs/lab/FINDINGS.md` sec. 42 for the full writeup — moved
there from `docs/FINDINGS.md` in vcctrl's 2026-09-11 public-release docs
reorg, see below).

## Never probe a RUNNING SDL3-DOS program through the keyboard

vcctrl proves the input link is alive with a Scroll-Lock LED round trip
through the BIOS keyboard path (`verify-input`), and every operation that
has to reboot the target first -- `get-file --return`, `send-file
--return`, the reboot chord -- routes through the same check. On this
stack that has two consequences every port inherits, because the cause is
in the shared backend, not in any one game (dosags, 2026-09-16; commits
5f86091 and 1f9790d there; confirmed independently by the vcctrl session,
which now documents it in its own rig-hazards/workflow skills and
OPEN-FAULTS sec. 27):

- **It cannot succeed while any SDL3-DOS program is running.**
  `DOSVESA_InitKeyboard()` (`SDL_dosevents.c`) replaces the BIOS IRQ-1
  handler for the life of the program, so the LED round trip never
  completes. A healthy program and a hung one look identical: REFUSED.
  "The screen stopped changing" is not "the program exited" -- a slow
  game can sit on a static frame for many minutes.
- **It is not side-effect-free.** The Scroll-Lock press still reaches the
  program's own raw-scancode handler. Two `get-file` attempts against a
  still-running game (each refused at its own reboot step) were followed
  by four key-press log lines and an early quit with no RUNMANIFEST -- the
  health check ended the run it was checking.

So: while a program may still be running, judge liveness with channels
that inject nothing -- both capture channels (`vcctrl_shot` and
`vcctrl_camera_shot`; a transient "no picture" during a VESA mode-set is
expected, recheck before concluding anything), elapsed time against a
budget sized from real measurements, and a result file appearing. Keep
`verify-input` for the question it answers well: *no program is running
-- is the link alive?* It correctly diagnosed a genuinely dead PS/2 link
after a post-transfer reboot (target side healthy on the Pi, no LED
change); only a full power cycle recovered it, not a warm reboot. That
fault recurs on the g2k 486 at the post-FTP return reboot; the signature,
check order, recovery, and the requirement that scripts recover from it
by themselves are in `dos-rig-operations`'
`references/power-management.md`.

Two collection details from the same campaign: check the per-file `ok` of
the files you actually need, not a job's aggregate `ok` -- a list that
includes files only some programs produce can never be all-ok (vcctrl's
own `harness/vcctrl-collect` already reads per-file status; match it
rather than reinventing the check); and use a fresh tag per run, since
the target side keeps old logs and a reused tag can hand back a stale
file.

## What a port should record

Follow `templates/BENCHMARK.md`'s format, and vcctrl's own result-envelope
discipline (`docs/HARNESS-STANDARD.md` in the vcctrl repo): declared +
detected hardware, a captured (not inferred) completion witness, and a
statistical-repeatability approach for FPS claims rather than a single raw
number — across-session FPS noise on real hardware has been measured at
several times the within-session band, so a single run is not a result.

Every cell that draws moving content also records a series of
physical-screen frames taken during the timed run (capture card every ~10 s,
camera every ~30 s, contact sheet in the results directory), and someone
looks at the sheet before the cell's rate is written down. A hash of the
engine's own frame cannot see what the present path did to the display
(`docs/harness-lessons.md`, "a third time"), so the runner does this on every
cell rather than relying on a per-patch check or on the operator watching the
monitor. A rate from a cell whose sheet shows stale pixels is void.

## Validation discipline: `shared/skills/dos-hardware-validation/`

Beyond the manual command sequence above, `shared/skills/dos-hardware-validation/`
is a genericized Claude Code skill capturing the discipline a real
validation campaign needs to produce a trustworthy result: design a
comparison and verify it against source before running anything, populate
with an explicit destination + sha256 verify (never trust a transfer
capability's default inbox), per-cell `set`/`forbid`/`expect_log`
three-witness discipline, RUNMANIFEST-driven metric extraction, ABBA
methodology with pre-registered thresholds, treating a harness bug as a
bug to fix (not a workaround), and handing off raw data instead of a
narrated summary. Distilled from a real doskutsu campaign on the vcctrl
rig — see `shared/skills/README.md` for provenance and adoption (it
reaches every port as `/sdldos:dos-hardware-validation` through the
hub's `sdldos` plugin).

`shared/skills/dos-rig-operations/` covers the same rig at a lower level
— input injection, screen capture, file transfer, log collection, and
power management for day-to-day work, independent of running a full
validation campaign (staging a build to poke at manually, pulling one log
back, etc.). `dos-hardware-validation` assumes this skill's discipline
rather than re-deriving it. See `shared/skills/README.md` for the full
skill list, including the build/QA-side companion
`dos-realhw-verification`.

## vcctrl went public (2026-09-11): what changes here

vcctrl made its repository public. Nothing in vcctrl's own behavior
changed in a way that affects an existing profile (see the hardware-
defaults note below), but several references from this repo need
updating and one new document is worth reading before onboarding a
third port:

- **Canonical URL**: the private Forgejo is the live, canonical repository
  (public mirror: `https://github.com/ecliptik/vcctrl`), with clean history after a scrub
  (credentials/hostnames that had leaked into old commits, including four
  an earlier 2026-08-24 rewrite missed and none of the commit messages,
  which that earlier pass never touched). A
  [`github.com/ecliptik/vcctrl`](https://github.com/ecliptik/vcctrl)
  mirror went live the same day (confirmed via a direct `git ls-remote`
  against it matching Forgejo's `main` exactly) — link either, but
  Forgejo is where changes land first. **A second rewrite pass, same
  day**, scrubbed a real LAN hostname (`jezebel` → `modernpc`, across
  the tree including two filenames) and the self-hosted forge's own
  hostname, and purged two files from history entirely (not just the
  tree): `tools/scan-history.sh`, an old `kvm-ro-share.jpg`. vcctrl's own
  `docs/HISTORY-MAP.txt` maps hashes from *before* the first rewrite to
  *after* it, but was not regenerated for the second pass — a hash from
  the narrow between-passes window has no clean map; don't cite one from
  that window without checking with whoever's touching vcctrl at the
  time.
- **Doc paths moved**: `docs/FINDINGS.md`, `OPEN-FAULTS.md`,
  `PI5-MIGRATION.md`, `VIDEO-SWAP.md`, `SOUND-PROFILES.md`, and
  `PICOGUS-CONSOLIDATION.md` all moved to `docs/lab/` in vcctrl (separated
  as "one rig's own record" from generic reference docs, still public).
  `templates/vcctrl-profile.yaml.template`'s `docs/OPEN-FAULTS.md` sec. 15
  citation was updated to `docs/lab/OPEN-FAULTS.md` accordingly — check
  for the old `docs/<name>.md` form before citing any of these six files
  again.
- **Camera/Power/MSD hardware defaults are now enforced, not just
  documented-against.** Previously, leaving a capability's `backend:` key
  absent in `vcctrl.yaml` silently defaulted it ON against vcctrl's own
  specific hardware (a Kasa smart plug, `/dev/video0`) — a framework-level
  bug (`daemon/vcctrld.py`'s `Registry._resolve_backend`), not a
  documentation gap. An absent key now means "not configured" for these
  three. This has no effect on any profile that already sets these
  explicitly — every `profile-kinds/*.yaml` template in vcctrl does — but
  it means a new port's own profile can no longer end up silently talking
  to vcctrl's specific hardware defaults by omission; it must actually
  say what it wants.
- **New `docs/KNOWN-LIMITATIONS.md` in vcctrl — read this before assuming
  the DOS-side harness "just works" for a non-doskutsu target.** It's an
  honest list of what does NOT yet generalize past vcctrl's own rig: the
  whole DOS-side contract is still hardcoded rather than config-driven —
  the boot-menu item/window, the BLASTER-to-boot-profile-name map, mTCP's
  install path and batch-file names, the `RDYPULSE.COM` Scroll-Lock
  readiness signal (the *only* boot-readiness witness vcctrl implements —
  there is no pluggable readiness-witness abstraction yet, so a target
  that can't run a custom TSR or doesn't use PS/2 LEDs has no readiness
  signal at all), and the VGA mode-restore/UniVBE paths. It also flags
  several capabilities read once from the primary profile rather than
  per-profile, literal install-prefix/bindir/service-user assumptions, a
  fixed HID-gadget identity, a US-QWERTY-only `type` charmap, and a
  bootstrap FTP path that still depends on a script living outside the
  repo. None of this is new breakage — dosags's and dossage's own profile
  work already routed around parts of it (the "orchestration layer"
  section above) — but it's now written down in one place instead of
  discovered per-port. Worth rereading before scoping any future port
  whose target isn't PS/2 DOS on vcctrl's own rig shape.
- **License and doc restructuring** (informational only): vcctrl added an
  MIT `LICENSE` (matching this hub and every port here) plus
  `THIRD-PARTY.md`; its README gained a "Configuration by target type"
  section mapping named hardware setups to `profile-kinds/*.yaml`; and its
  `CLAUDE.md`/new `AGENTS.md` are now 3-line pointers at `CONTRIBUTING.md`,
  which now holds the substance — nothing in this hub currently reads
  vcctrl's `CLAUDE.md` expecting the old content, but a future reference
  should point at `CONTRIBUTING.md` instead.
- **Skills-install line changed.** vcctrl's README no longer tells
  installers to pull all 7 skills (`--full-depth --all`) — that swept in
  `vcctrl-repo-conventions` and `vcctrl-webkvm-copy`, two skills about
  vcctrl's own contributor conventions that have nothing to do with
  driving a rig from a port repo. The new line names the four
  hardware-portable skills explicitly. `scripts/new-port.sh`,
  `PORTING.md`, `skills/port/SKILL.md`, `shared/skills/README.md`, and
  `shared/skills/benchmark/SKILL.md` all quoted or ran the old `--all`
  form and were updated to match — see those files' own history for the
  exact command.
- **`templates/vcctrl-profile.yaml.template` was stale from BEFORE this
  section's own subject** (fixed 2026-09-11, same pass as this note):
  it still described the orchestration scripts as "doskutsu-coupled...
  may not yet run a second project's profile unattended" — true before
  slice 6's generalization, false after it — and was missing every
  field that generalization added (`exe`, `default_cfg`/`cfg_dest`,
  `env_clear_cmd`, `sweep_entry_cmd`, `card_overrides`,
  `env.capture_flag`). Rewritten to match the current schema, with an
  explicit warning that `vcctrl-cell` only fits an executable that takes
  no CLI args (doskutsu's shape) — a port like dosags whose engine needs
  real arguments and stdout redirection should use the "Manual workflow"
  below instead, regardless of what this template lets you fill in. Its
  own `profiles/doskutsu.yaml` citation also moved: that file no longer
  lives in vcctrl's repo (see below).
- **`profiles/doskutsu.yaml` moved out of vcctrl's own repo**, into
  doskutsu's own (`profiles/doskutsu.yaml` there), the same day as the
  rest of this handoff — matching the per-repo-profile convention
  dosags/dossage already used. vcctrl's local (gitignored) `vcctrl.yaml`
  now points `harness.profile` at doskutsu's new path explicitly.
  Anything in this hub that used to say "vcctrl's own
  `profiles/doskutsu.yaml`" means doskutsu's repo now.

## Future work (explicitly not this milestone)

- Move `docs/HARNESS-STANDARD.md` out of vcctrl into a shared location once
  a second project adopts it (per vcctrl's own stated intent).
- Decide whether multiple physical rigs eventually need a registry, or
  whether one rig with swappable CPUs/boards remains sufficient for the
  matrix in `HARDWARE.md`.
