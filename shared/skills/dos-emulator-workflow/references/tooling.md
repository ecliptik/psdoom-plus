# DOSBox-X tooling mechanics

Covers `shared/tools/dosbox-launch.sh`, `dosbox-run.sh`,
`dosbox-teardown.sh`, `ratchet.sh`, and the three `.conf` variants. All four scripts
are already fully generic (usable as-is by any port) -- this is a guide
to using them well, not a spec of what they do (read the scripts' own
`--help`/header comments for the authoritative flag list, which can
change).

## Picking a mode

- **Manual/visual work** (play-testing, screenshots, driving via
  `xdotool`, watching a bug happen): `dosbox-launch.sh`. It mounts and
  stays running; you drive it or watch it. `--stage` mounts
  `build/stage/` (the real install layout: exe + `CWSDPMI.EXE` + `data/`
  co-located) rather than the repo root -- use `--stage` for anything
  where the engine resolves assets relative to its own base path, which
  is most gameplay-adjacent testing.
- **Scripted/CI-style smoke checks** (does it boot, does the expected
  banner line show up, does it exit 0): `dosbox-run.sh`. Headless, stages
  one exe (+ `--include` files), captures stdout to a file, exits.

## The stdout-only logging convention, and why it exists

`dosbox-run.sh`'s headless capture works by redirecting stdout to a file
in a generated `RUN.BAT` (`> STDOUT.TXT`). If a probe or test logs via
something that writes to **stderr** (e.g. SDL's own `SDL_Log()`), that
output is invisible to this capture path *and there is no fix available
inside DOSBox-X's own shell*: DOSBox-X's built-in command interpreter
does not support `2>&1` redirection the way a real DOS `COMMAND.COM`-plus-
DJGPP-runtime combination does when a DJGPP binary is invoked directly --
DOSBox-X parses `2>&1` as a redirect to a file literally named `&1`,
producing an empty capture, not a merged stream. (`dosbox-run.sh`'s
`--merge-stderr` flag works around this differently -- it only helps for
a program whose *own runtime* processes the `2>&1` in its invocation
line, which DJGPP's does; it does not make DOSBox-X's shell itself
understand the syntax.)

**Practical rule: write any new probe/smoke-test diagnostic output via
`printf`/stdout, not a stderr-writing logger**, if it needs to be
captured by a headless DOSBox-X run. This is why `shared/tests/sdl3-smoke/`'s
own probe reimplements upstream SDL's test coverage with `printf()`
instead of using `SDL_Log()` directly -- not a style preference, a
capture-mechanism requirement.

## Env-var hooks on `dosbox-launch.sh`

- **`DOSBOX_CONF=<path>`** -- override which `.conf` is used, resolved
  against the repo root if relative. Applied after `--fast`/parity flag
  parsing, so it wins over both.
- **`LAUNCH_LOG_VERBOSE_VAR=<NAME>`** -- forces `SET <NAME>=1` in the
  guest so a port's own verbose-logging env var is on for this launch,
  letting a smoke-gate banner-emit check witness INFO-level banners even
  when the port's untagged-run default is a quieter level.
- **`LAUNCH_EXTRA_SET="NAME=VALUE ..."`** -- injects arbitrary additional
  `SET` commands into the guest environment, applied after the launcher's
  own fixed `SET`s so a caller can deliberately override one of them.
  This is the same mechanism `docs/hardware-testing.md` and
  `dos-hardware-validation/references/runmanifest-log.md` reference for
  setting a port's environment-detection override.

## The three `.conf` variants

- **`dosbox-x.conf`** (parity) -- calibrated to approximate a specific
  real reference machine's CPU throughput (`cycles=fixed <N>`), for
  anything where real-HW-equivalent *timing* matters even locally (audio
  dropout investigation, anything timing-sensitive enough that
  `cycles=max` would hide or fabricate a problem). The shipped file is
  one port's own calibration, explicitly not a universal setting --
  re-benchmark and re-calibrate `cycles`/`oplmode`/`memsize`/video for
  your own port's reference machine rather than assuming the checked-in
  number transfers.
- **`dosbox-x-fast.conf`** -- identical except `cycles=max`, runs as fast
  as the host CPU allows (several times faster than a real reference
  machine). Use for fast iteration where wall-clock timing doesn't
  matter -- most compile/video/input/filesystem work.
- **`dosbox-x-oversized.conf`** -- a variant for a specific display/video
  scenario (see the conf's own header for current specifics, which can
  change as it's tuned).

## Emulator-only escape hatches baked into the shipped confs

`dosbox-run.sh`'s generated `RUN.BAT` (default mode) and
`dosbox-launch.sh` set `SDL_DOS_AUDIO_SB_SKIP_DETECTION=1`. **This is an
emulator-only escape hatch.**

**Corrected 2026-09-22 -- the reason this section used to give was
wrong.** It said DOSBox-X's emulated SB16 returns a fixed failure value
on the DSP detection read regardless of timing tuning. The real cause is
`dosbox-run.sh`'s own `-silent` flag, which switches the emulated Sound
Blaster off entirely: the DSP answers nothing (0xFF) and no IRQ ever
fires. Without `-silent`, detection passes (`dsp_ver=4`, IRQ 5 firing)
with or without the skip, on both shipped confs (hub AUDOPEN probe plus
dosags DSPV1-3). The consequence that matters: **in `dosbox-run.sh`'s
default mode the skip makes the driver open a device that never plays.**
Mixer-side figures are real, but device-side ones (IRQ counts, ring
drain, clip timing, anything "audible") mean nothing. dosags reported
"all SB paths play" and a ring-fill anomaly from exactly such runs
before this was found.

**For an audio result that means something, pass `--sb-live`.** It drops
`-silent`, runs under a private `xvfb-run -a` display (or
`DOSBOX_DISPLAY`), sends host audio to SDL's dummy driver, and leaves
detection on, as on real hardware. Clip timing then becomes device-paced,
so a run whose script waits on a sound is no longer tick-identical across
two runs. Keep screen-equality A/B rows sound-off rather than live.

See
`references/emulator-vs-hardware-boundary.md` for the general rule this
is an instance of, and why setting it on real hardware would actively
mask a real regression rather than just being redundant.

## Driving and screenshotting DOSBox-X with `xdotool`/`scrot`

Sourced from doskutsu's own hardened harnesses (`tests/run-gameplay-smoke.sh`,
`tests/setup-review-walk.sh`) and confirmed the hard way a second time by
dossage hitting the same trap independently -- worth having here instead of
re-derived per port.

- **Focus once, then send keys with no `--window`.** `xdotool key --window
  <id>` / `type --window <id>` uses XSendEvent, which SDL2 (and therefore
  DOSBox-X) ignores -- it can silently no-op even against a valid, current
  window ID. This is not a staleness problem, it's the wrong event type for
  an SDL2 app. Use plain `xdotool key`/`type` (no `--window`) so events go
  via XTEST to whatever holds real keyboard focus, and focus the window
  once up front rather than per keystroke -- re-focusing before every key
  introduces a window-manager round-trip that desynchronizes keystroke
  timing (`run-gameplay-smoke.sh`'s `key()` does exactly this: focus once,
  then bare `xdotool key --delay 60 "$name"` for every subsequent send).
- **`xdotool search --name DOSBox windowactivate --sync` can race** if other
  clients also have "DOSBox" somewhere in their title -- loop/retry it
  rather than treating a single failure as fatal (`run-gameplay-smoke.sh`
  retries up to 10 times with a short sleep).
- **Cache the window ID once you have it; don't re-search by name after a
  video mode change.** The underlying X window survives a DOS text-mode ->
  VESA graphics-mode switch (it isn't destroyed/recreated), but its
  `WM_NAME` title goes blank on the switch, so a later `--name DOSBox`
  re-search stops matching anything. `setup-review-walk.sh`'s
  `focus_dosbox()` caches the id in `DBX_WIN` on first success and reuses
  it for every later `windowactivate`/`windowfocus` call. On a real
  (non-headless) window manager, `windowactivate` alone raises the window
  *without* granting it keyboard focus -- call `windowfocus` too, and
  re-assert both before each input batch since focus can drift on a live
  desktop with other windows.
- **`scrot -u` (capture the focused window) needs a window that's actually
  focused** -- if focus silently failed, this degrades straight into "failed
  to grab image" rather than a clear error; check the exit code rather than
  assuming a screenshot attempt succeeded. `scrot` also **appends
  `_000`/`_001` instead of overwriting** an existing target path -- `rm -f`
  the target before every shot, or a later diff/comparison silently runs
  against a stale frame from a prior run (this shipped a false "the patch
  didn't land" finding in doskutsu at least once). Fallback when `scrot`
  fails: `import -window "$(xdotool getactivewindow)" out.png`.
- **Prefer a private Xvfb for automated/headless gates over the operator's
  real `:0`.** Two independent reasons: an Xvfb doesn't touch whatever the
  operator is actually looking at on their real desktop, and a shared `:0`
  can end up in a state where `xdotool`/`xdpyinfo` block indefinitely --
  which reads exactly like a boot wedge in the port under test, not a
  display problem, and costs real debugging time until you think to check
  the display itself. doskutsu's automated gates default to
  `DOSBOX_DISPLAY=:88` with `--own-xvfb`; reserve the real `:0` for
  genuinely manual/interactive sessions where a human needs to see the
  window.

## Audio pre-buffering under `cycles=max` can make a working runtime change look unresponsive

Under `dosbox-x-fast.conf`, CPU emulation runs far faster than real time,
but the emulated DAC still drains its buffer at a fixed real-time rate --
so SDL's audio callback can pre-buffer far more audio than real-time
playback has actually consumed yet. Concretely: the callback can fire
many times immediately after stream-open, filling a large buffer, then go
quiet for tens of real seconds even though a keypress changed some
audio-affecting variable (e.g. a loudness/volume flag) moments later in
wall-clock time -- because everything already sitting in the buffer was
committed before the variable changed, and nothing is requesting more
audio yet to pick up the new value.

**Consequence:** testing "did a runtime audio-parameter change take
effect" by watching callback behavior under `cycles=max` can show a long,
misleading lag -- or apparent total non-response -- that has nothing to do
with the port's own logic being wrong. Either test audio-reactivity under
the parity config (`dosbox-x.conf`, not `-fast`), or budget a real
wall-clock wait proportional to however much audio is likely pre-buffered,
not to game-logic/CPU time, before concluding a change had no effect.

## The global-`pkill` hazard

`dosbox-teardown.sh` exists because a global `pkill -x dosbox-x` kills
*every* DOSBox-X process on the machine, not just the one a script
launched -- in a session with concurrent workstreams (a human reviewing a
build in one window, an automated smoke gate running in another), that
global kill has aborted a live, unrelated review more than once. Source
`dosbox-teardown.sh` and call `dbx_kill_conf "$CONF"` (conf-scoped,
comm-filtered so it can never self-kill the calling shell) instead of
reaching for `pkill` directly in any new script.

## The ratchet: `ratchet.sh` and a port's `tests/ratchet.yaml`

`shared/tools/ratchet.sh` runs a port's deterministic DOSBox-X checks and
fails on any regression. The script's header is the format spec, and
`shared/tools/ratchet-example.yaml` is a commented example. The file is a
strict flat subset of YAML: still valid YAML, but parsed with awk, so the
tool needs only bash and the usual system tools (no yq here). Its parts:

- **`runs:`** -- named commands, each executed once per invocation, so a
  multi-minute cell is not repeated for every number read from it.
- **`checks:`** -- each reads one number, from a run (`run: <name>`) or
  from its own `command:`:
  - `key: overwrites` reads `overwrites=<n>`; or `regex:` with one
    capture group.
  - `match:` narrows the read to the lines that matter.
  - The number is compared with a `limit` (`eq`, `le`, `ge`).
- **`build:`** -- optional, run once before anything else.

- **Exit code is the verdict.**
  - 0: pass.
  - 1: a non-quarantined check FAIL, ERROR or TIMEOUT, a failed control,
    or a failed build. A hang is a regression. A missing field is a
    failure too: ERROR means no number could be read.
  - 2: nothing failed, but something was BLOCKED or INTERRUPTED.
  - 3: the tool itself could not run (config error, lock held).
- **Limits only move by review.** A value better than its limit is
  IMPROVED. With `--propose-lower`, the tool writes
  `tests/ratchet.yaml.proposed` beside the original and never touches the
  real file.
- **Controls prove a check still has power.** A check's optional
  `control:` is a command that breaks what the check guards (a
  `DOSAGS_BREAK_*` build, the pre-fix binary). It runs only with
  `--controls` (on demand or weekly, not nightly). The control must make
  the check FAIL (OK). If it passes, the check is powerless (NOPOWER); if
  it produces no number, it proves nothing (NONUMBER). Both count as
  failures.
- **Quarantine is a human decision.** Every run appends to
  `build/ratchet/state.tsv`. A check with 2+ non-passing and 1+ passing
  results in its last 10 is reported SHOULD QUARANTINE.
  `quarantine: true` keeps it reported but not gating. A check that fails
  every time is a regression, not a flake, and is not flagged.
- **One unit at a time, under the lock.** Every run, own-command check
  and control holds the host-wide DOSBox-X lock (next section),
  EXCLUSIVE by default. Its `dosbox-run.sh` calls see `DOSBOX_LOCK_HELD=1`
  and don't take it again.
  - `lock: shared` on a run or own-command check is for FIXED-cycles
    units only (see "Lock mode for fixed-cycles runs" below). Such a unit
    takes the lock SHARED, can overlap other shared runs on the host, and
    skips the `ps` quiet-wait. An exclusive taker still gets priority over
    it through the intent lock.
  - The file is checked when read: `--fast` in the command is an error
    (cycles=max). A `--conf` whose last `cycles =` isn't `fixed N` or a
    number is an error. With no conf on the command line, the author is
    vouching for fixed cycles, and `--list` and the report say so.
  - As a fallback for launchers that don't take the lock yet, it then
    waits until no `dosbox-x` has run on the host for `--quiet-for`
    seconds in a row (default 15). `--no-host-wait` drops that once
    every launcher honours the lock.
  - Lock plus quiet-wait give up after `--busy-wait` (900 s): BLOCKED.
  - The timeout starts after the waits.
  - A freeze-capture watcher that finds "the" dosbox-x in `ps` can be
    handed the wrong screen by any concurrent instance; an `--sb-live`
    run was caught doing it.
- **Displays by `kind`:**
  - `sb-live`: display variables unset, so `dosbox-run.sh --sb-live`
    uses its own `xvfb-run -a`.
  - `capture`: `DOSBOX_DISPLAY_NUM`/`DOSBOX_DISPLAY` exported (default
    251, the ratchet's own number); an Xvfb is started there if none
    answers.
  - `plain`: no display.
- **Unattended safety.**
  - `flock` on `/tmp/sdl-dos-ports-ratchet.lock`: one ratchet per host.
  - Each unit runs in its own session. On timeout the whole session
    (DOSBox-X, xvfb-run, Xvfb) gets TERM, then KILL.
  - SIGTERM stops the running unit and still writes the report.
- **Output:** `build/ratchet/<run>/ratchet-report.txt` and
  `ratchet-summary.txt` (the one line a nightly job posts); `latest`
  points at the newest run. Don't give ratchet commands `--keep-stage`:
  the stages pile up in `/tmp` night after night.

## The host-wide DOSBox-X lock: `/tmp/dos-port-dosbox.lock`

**Rule:** timing-, rate- and screen-dependent DOSBox-X runs are
EXCLUSIVE. DOSBox-X at `cycles=max` tracks host wall-clock, so a second
instance on the same box changes what such a run measures: MIDI
lateness, silent IRQs, ring fill, tick rates, device-paced clip ends,
and a freeze capture's timing. That is also why concurrent lab runs make
timing cells non-comparable. Runs whose result is a count unaffected by
host load (a `-silent` smoke test, boot-to-banner, a build probe) can be
SHARED.

- **`dosbox-run.sh`** takes `flock` on the lock for the life of its
  DOSBox-X.
  - Default: EXCLUSIVE with `--sb-live` or `--interactive` (the mode
    freeze-capture gates use), SHARED otherwise.
  - Override with `--lock shared|exclusive|none` or `DOSBOX_LOCK_MODE`.
  - It waits `DOSBOX_LOCK_WAIT` seconds (default 3600), then exits 75.
  - **Writers have priority.** Plain `flock` lets a new SHARED taker in
    while an EXCLUSIVE waiter is queued, so a stream of overlapping
    shared runs could starve a writer (dosags saw an hour). Every taker
    therefore goes through an intent lock, `/tmp/dos-port-dosbox.lock.intent`:
    - EXCLUSIVE: intent exclusive, then main exclusive, then release
      intent.
    - SHARED: intent shared, then main shared, then release intent.
    A queued writer holds intent, so new readers wait behind it; it
    waits only for the readers already inside. It releases intent as
    soon as it holds the main lock, so a second writer can queue on the
    main lock and be the next holder, with no reader slipping between
    them. The implementation is `shared/tools/dosbox-lock-lib.sh`, a
    sourced library used by `dosbox-run.sh` and `ratchet.sh`; a harness
    that takes the lock itself should use it or follow the same protocol
    on the same two files.
  - `/tmp/dos-port-dosbox.lock.who` has one line per holder
    (`pid=... mode=...`) and per queued writer
    (`waiting: pid=... mode=exclusive`). Each process removes its own
    lines, and every edit also prunes lines whose process is gone or was
    replaced (checked by the recorded `pstart=`, the process start
    time). So a taker killed while queued leaves no permanent line. A waiter prints the file with every line marked `(alive)` or
    `(STALE: pid N is not running)`: a process that was killed never
    cleans up, so the recorded pid is checked. `.who` is informational,
    and the flock itself is the truth. To ask "is the lock free?", test it
    (`flock -n -x /tmp/dos-port-dosbox.lock true`); never read `.who`.
- **A SHARED series must lock per cell, never once around the series.**
  Writer priority only works when a lock is acquired. A tool that takes
  the lock SHARED once and then runs a whole sweep under it keeps the
  lock for the sweep's full length. A writer that queues behind it waits
  out every remaining cell, and so does every new shared taker, which is
  now blocked on the writer's intent. One hour-long shared sweep stalls
  the whole host. So a fixed-cycles sweep takes and releases the lock
  per cell: `dosbox-run.sh` does that by default when called once per
  cell without `DOSBOX_LOCK_HELD`. Then a queued writer gets in at the
  next cell boundary. Displays are per cell for the same reason.
- **A multi-run tool** (a gate, an ABBA series, `ratchet.sh`) takes the
  lock EXCLUSIVE once around the whole series (through the intent lock,
  as above) and exports `DOSBOX_LOCK_HELD=1`. The `dosbox-run.sh` calls
  inside it then skip the lock. A child that opens its own fd and asks
  again would deadlock against its parent.
- **`ratchet.sh`** holds it EXCLUSIVE per unit (each run, own-command
  check and control), not for its whole invocation, so other work can
  interleave between units.
  - It takes the lock before its `ps` quiet-wait, so no lock-honouring
    run can start inside the quiet window.
  - The quiet-wait stays as a fallback while any launcher on the box
    does not take the lock yet (a port still on an older subtree pin, a
    harness that calls `dosbox-x` directly). `--no-host-wait` drops it
    once they all do.
- **`dosbox-launch.sh` takes no lock.** An interactive review window can
  stay open for hours and would stall every gate. It runs on its own
  display (`:0`), and a human watching is not a measurement.
- **Displays are allocated and locked too.** The host lock says when a
  run may start, not where its window goes. Two SHARED cells on one Xvfb
  display land in each other's root-window captures: dosags voided two
  such pairs, on :231 and :211. So every display a cell draws on is held
  by a per-display flock, `/tmp/dos-port-display.<N>.lock`, for the
  cell's life (`dbxlock_display_acquire` / `_release` in
  `dosbox-lock-lib.sh`; stale-safe, since a dead holder's flock drops by
  itself).
  - `dosbox-run.sh`, unpinned `--sb-live`: gets a free display from
    `DOSBOX_DISPLAY_RANGE` (default 300-399) and runs `xvfb-run -n N` on
    it, instead of `xvfb-run -a`'s own racy pick. This applies in any
    lock mode.
  - `dosbox-run.sh`, pinned (`DOSBOX_DISPLAY=:N`, N != 0): takes display
    N's flock, so a second cell pinned to the same display WAITS for the
    first (up to `DOSBOX_LOCK_WAIT`). A worker running several cells at
    once should stop pinning and let them be allocated.
  - `dosbox-launch.sh`: `DOSBOX_DISPLAY=auto` allocates a display and
    starts a headless Xvfb for the session (stopped when DOSBox-X exits).
    A pinned `:N` takes that display's flock, waiting
    `DOSBOX_DISPLAY_WAIT` (10 s) before refusing.
  - `:0` (the human's desktop) is never locked.
  - **A harness that owns the display itself** (to start its Xvfb only
    once it holds the display) sets `DOSBOX_DISPLAY_HELD=1`, which
    mirrors `DOSBOX_LOCK_HELD`. Then `dosbox-run.sh` and
    `dosbox-launch.sh` skip the flock for that pinned display. Taking it
    again would open a new file description on the same lock file and
    block on the harness forever. The pattern:
    1. `dbxlock_display_acquire N`: fd 5 is now the display's flock,
       held by the harness shell.
    2. Start Xvfb on `:N` with fds 4-9 closed, then wait until that
       Xvfb (its own pid) answers.
    3. Run `DOSBOX_DISPLAY=:N DOSBOX_DISPLAY_HELD=1 dosbox-run.sh ...`.
    4. Cleanup kills the Xvfb and **waits until it has exited**.
    5. The harness process exits, which releases fd 5.
    The wait in step 4 matters. A cleanup that only signals Xvfb releases
    the display while that Xvfb is still shutting down. The next owner
    then starts its own Xvfb into the dying one's socket and loses the
    display: its capture comes back empty, which is what the hub's test
    hit before the wait was added. `dosbox-run.sh` and the launcher's
    supervisor wait for their own Xvfb the same way.
  - Order: the host lock first, then the display. Every tool keeps that
    order, so none can hold one while waiting for the other in reverse.
  - Call `dbxlock_display_acquire` directly, not inside `$(...)`: the
    flock belongs to the calling shell, and a command substitution's
    subshell would drop it at once. Read `DBXLOCK_DISPLAY`.
- **Nothing that can outlive a cell may inherit a lock fd.** A process
  started while a lock fd is open inherits it and keeps the lock for its
  whole life. In dosags (2026-09-24), an orphaned Xvfb inherited fd 8
  from a lock-holding shell and held the lock SHARED after its parent
  exited, and every exclusive writer deadlocked.
  - The hub tools start every Xvfb, and every other helper that may
    outlive the cell, with fds 5-9 closed (`5>&- 6>&- 7>&- 8>&- 9>&-`).
  - Only the cell's own DOSBox-X keeps fd 8 (the host lock) and fd 5
    (its display), on purpose: those must last exactly as long as it
    does.
  - `dosbox-run.sh` starts its own Xvfb for that reason (not
    `xvfb-run`, whose Xvfb inherited the fds), with a lock-free watchdog
    that stops the Xvfb once DOSBox-X is gone.
  - A port harness that holds the lock in a shell and starts anything
    long-lived must do the same.

  **Troubleshooting a stuck lock** (a writer waiting forever):
  ```
  source .sdl-dos-ports/shared/tools/dosbox-lock-lib.sh; dbxlock_describe
  # by hand:
  INO=$(stat -c %i /tmp/dos-port-dosbox.lock)
  grep ":$INO " /proc/locks        # READ = shared, WRITE = exclusive, "->" = a waiter
  find /proc/[0-9]*/fd -lname /tmp/dos-port-dosbox.lock 2>/dev/null |
    cut -d/ -f3 | sort -u | while read p; do echo "$p $(cat /proc/$p/comm)"; done
  ```
  - `/proc/locks` says THAT the lock is held, and in which mode. Its pid
    is the process that placed the lock, which for `flock -s 8` is the
    flock utility, normally already gone. So it does not say who holds
    the lock.
  - The `/proc/*/fd` scan lists the processes that really hold it.
  - Anything there that is not `dosbox-x`, a shell or a `.sh` tool (an
    `Xvfb`, a `sleep`, a `python`) is a helper that inherited the fd.
    A `timeout`, `env` or `setsid` wrapper is judged by its children: it
    passes as "a wrapper around a cell" when a child is DOSBox-X or a
    shell/`.sh` running the cell. It stays SUSPECT, naming the child, when
    what it wraps is anything else, such as an Xvfb or a keep-alive.
    `dbxlock_describe` flags it SUSPECT. Kill it (by pid, never
    `pkill`), and fix whatever started it without closing the lock fds.
  - If the kernel shows a lock but no visible process has the file open,
    the holder is in another sandbox or belongs to another user.
- **Display isolation is a separate requirement, not a lock:** every
  capture tool must track its own DOSBox-X (pid or stage) and grab only
  its own display. Never pick "the" `dosbox-x` out of `ps`: that was the
  root cause of an `--sb-live` run corrupting a gate's frames. Kill only
  your own pid or your own display (`dbx_kill_display` in
  `dosbox-teardown.sh`), never `pkill -x dosbox-x`.

### Lock mode for fixed-cycles runs (coordinator rule, 2026-09-23)

A run at FIXED cycles (for example `cycles=fixed 40000`) is deterministic
whatever else the host is doing. dosags showed byte-identical logs
(lab-mmd MMDCF), and the same figures with or without overlapping runs
(lab-metric). Such a run may take the lock SHARED, even when it measures
time, so fixed-cycles cells from different workers can run side by side.
Keep it to about one per spare core; the lock doesn't count holders.
EXCLUSIVE stays the rule for:
- anything at `cycles=max` whose result depends on time or rate;
- freeze-capture / screen-series gates, unless the gate itself runs at
  fixed cycles;
- `--sb-live` cells at `cycles=max`.
Where a result depends on emulated time, say in the record which cycles
mode the cell ran.

**Fixed cycles do not make host-driven input deterministic** (dosags,
2026-09-24). Fixed cycles make EMULATED time deterministic. But input
injected from the host (`xdotool` clicks and keys, sent at wall-clock
times) lands on a different emulated tick when the host is loaded.
lab-journeys saw +439 MIDI events from exactly that. So SHARED is only
for cells whose input comes from inside the guest (AutoPilot, tick-keyed
scripts) or that have no input at all. A cell with host-driven input
takes EXCLUSIVE, at any cycles setting. The tools enforce what they can
see:
- `dosbox-run.sh` refuses `--lock shared` on a run with a display
  (`--sb-live`, `--interactive`), the kind host input can reach, unless
  the caller passes `--no-host-input` (or `DOSBOX_HOST_INPUT=none`).
  Passing it asserts that the cell's input comes from inside the guest,
  or that it has none. A `-silent` run has no display for host input to
  reach.
- `ratchet.sh` rejects `lock: shared` on a unit whose command mentions
  `xdotool`.
Neither tool can see an `xdotool` run from a separate process, so the
assertion is the caller's.

## Stage directories: `dosbox-stage.sh`

Every `dosbox-run.sh` run stages into `$TMPDIR/dos-port-dosbox.XXXXXX`
(default `/tmp`). The stage is removed on exit unless the run passed
`--keep-stage`. A kept stage holds the exe, the game package and
everything the run wrote, and nothing ever deleted one. On 2026-09-25 a
harness that keeps every stage filled the host disk: ~2,150 stages,
~55 GB, and a build failed on ENOSPC. Deleting a stage the moment it's
read is wrong too, because a crashed copy-out or a copy race can only be
recovered while the stage still exists. So stages are collected by rules.

- **Where a run's stage is.** `dosbox-run.sh` prints
  `dosbox-run.sh: stage <path> (kept|removed on exit; caller <c>)` to
  stderr at the start, and `stage kept at <path>` at the end. It also
  writes the path to `--stage-file PATH`, or by default with `--stdout
  OUT` to `OUT.stage`, before DOSBox-X starts. Read it from there. Don't
  search `/tmp` for the newest stage, which races with every other run.
- **`<stage>.meta`**, a sidecar outside the DOS-visible mount, records the
  caller, pid and pstart, the created, ended and copied times, keep and
  exit. The caller is `<repo>/<script>` of the script that ran
  `dosbox-run.sh`, walking up past `timeout`/`env`/`setsid`-style
  wrappers. Set `DOSBOX_STAGE_CALLER` to name it yourself, for example
  per lab worker. `dosbox-stage.sh caller` prints what would be recorded.
- **Mark copy-out.** A harness that keeps a stage to read results out of
  it runs `dosbox-stage.sh copied <stage>` once it has copied them. That
  lets `gc` take the stage after a 2 h grace. A caller that never marks
  still loses its stages at the 48 h max age.
- **`dosbox-stage.sh gc`** only lists what it would delete unless given
  `--apply`. It never touches a stage that is in use: a process with its
  cwd or an open fd inside it, a live `MOUNT C <stage>` command line, or a
  meta pid that is alive with the recorded pstart. Otherwise it goes per
  caller, newest first:
  - it keeps the newest `--keep 20`;
  - it deletes copied stages older than `--grace 2h`;
  - it deletes leftovers of a killed run (keep=0) older than
    `--orphan-age 1h`;
  - it deletes anything older than `--max-age 48h`.
  `--budget SIZE` then deletes the oldest remaining stages until the total
  fits, but never one in use or younger than the grace. `--caller NAME`
  restricts it to one caller. Stages with no meta (made before this
  existed) are grouped as `?<exe>`, their apparent caller from `RUN.BAT`.
  One gc at a time (flock).
- **Low disk.** When the stage directory has less than
  `DOSBOX_STAGE_GC_FREE_MB` free (default 10240), `dosbox-run.sh` first
  runs `gc --apply --caller <its own caller>`. It only ever collects its
  own caller's stages. Collecting across callers takes an explicit
  `dosbox-stage.sh gc`, run by a person, or by an owner for their own
  caller. Below `DOSBOX_STAGE_MIN_FREE_MB` (default 1024) the run refuses
  to start (exit 73) and names the gc command. `DOSBOX_STAGE_AUTO_GC=0`
  skips the collection, never the refusal.
- **Port follow-up.** A harness that always passes `--keep-stage` should
  read the stage path from `--stage-file` and mark each stage `copied`
  once its results are out. dosags' `tests/harness/run-dosbox.sh` is the
  first such port. This is its own change to make in its own repo.
- **Editing a tooling script while runs wait on the lock.** bash reads a
  script as it runs. An in-place edit (truncate and rewrite) of
  `dosbox-run.sh` while a run of it is queued on the lock makes that run
  resume at the old byte offset in the new text. Write the new file and
  rename it over the old one, which is what `git checkout` and
  `git subtree pull` do, or let the queued runs finish first.
