#!/usr/bin/env bash
# dosbox-run.sh -- run a DOS executable under DOSBox-X and capture its stdout.
#
# Headless mode (the common case): stages the exe + any --include files into
# a temp C:\, writes a RUN.BAT that invokes the exe with stdout redirected to
# STDOUT.TXT, runs DOSBox-X with `-silent -exit`, and copies STDOUT.TXT out.
#
# AUDIO: under `-silent` DOSBox-X's emulated Sound Blaster does not answer at
# all (DSP reads 0xFF, no IRQ ever fires). The default mode therefore sets
# SDL_DOS_AUDIO_SB_SKIP_DETECTION=1 so audio init still "succeeds" -- but the
# device never plays: any audio figure from a default-mode run is mixer-side
# only. Pass --sb-live for a run where the SB really plays (no -silent, a
# private Xvfb display, host audio to SDL's dummy driver, detection ON exactly
# as on real hardware). Proven 2026-09-22 with the hub's AUDOPEN probe and
# dosags DSPV1-3: -silent -> 0xFF / 0 IRQs; no -silent -> dsp_ver=4, IRQ-5
# firing, with or without skip-detection, on both shipped confs.
#
# Typical use:
#   tools/dosbox-run.sh --exe build/hello.exe --stdout /tmp/hello.out
#   tools/dosbox-run.sh --exe build/hello.exe --fast
#
# Interactive mode (opens the window -- prefer dosbox-launch.sh for playtest):
#   tools/dosbox-run.sh --exe build/hello.exe --interactive
#
# HOST-WIDE LOCK: every run takes flock on /tmp/dos-port-dosbox.lock for the
# life of its DOSBox-X -- EXCLUSIVE for --sb-live and --interactive runs,
# SHARED otherwise. DOSBox-X at cycles=max tracks host wall-clock, so a
# second instance changes what a timing-, rate- or screen-dependent run
# measures; those need the host to themselves, while plain -silent runs
# whose result is a count can share it. See --lock below and the lock
# section of shared/skills/dos-emulator-workflow/references/tooling.md.
#
# STAGES: every run stages into $TMPDIR/dos-port-dosbox.XXXXXX (default
# TMPDIR=/tmp), removed on exit unless --keep-stage. Its path is printed to
# stderr at the start and the end of the run, written to --stage-file (or
# to <--stdout>.stage), and described by a sidecar <stage>.meta (caller,
# pid, times, keep, exit) that dosbox-stage.sh uses to collect kept stages
# safely; a caller that has copied its results out marks the stage with
# `dosbox-stage.sh copied <stage>`. See dosbox-stage.sh's header.
#
# DOS has no LFN support. Filenames passed via --include are staged to C:\
# with uppercased basenames; the exe is also uppercased. If you need the
# exe to reference other files, they must be 8.3 and on C:\.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONF_PARITY="${SCRIPT_DIR}/dosbox-x.conf"
CONF_FAST="${SCRIPT_DIR}/dosbox-x-fast.conf"

EXE=""
ARGS=""
STDOUT_PATH=""
INTERACTIVE=0
INCLUDES=()
KEEP_STAGE=0
STAGE_FILE=""
FAST=0
CONF_OVERRIDE=""
ENVS=() # --env KEY=VALUE, repeatable -- see usage below
# --merge-stderr: capture stderr too (e.g. SDL_Log goes there). DOSBox-X / DOS shells
# don't always honor `2>&1` cleanly -- prefer printf-to-stdout in tests where possible.
MERGE_STDERR=0
SB_LIVE=0 # --sb-live: emulated SB really plays (see header)
LOCK_MODE="${DOSBOX_LOCK_MODE:-}" # --lock shared|exclusive|none (default by run type)
HOST_INPUT="${DOSBOX_HOST_INPUT:-}" # --no-host-input sets "none" (see --lock)
CWSDPMI="${CWSDPMI:-$SCRIPT_DIR/../vendor/cwsdpmi/cwsdpmi.exe}"

usage() {
  cat <<'USAGE'
Usage: dosbox-run.sh --exe PATH [--args "..."] [--stdout PATH] [--include PATH]...
                     [--fast | --conf PATH] [--interactive] [--keep-stage] [--merge-stderr]
                     [--sb-live] [--lock shared|exclusive|none] [--no-host-input]
                     [--stage-file PATH]

  --exe PATH         DOS executable to run (required). Basename is placed at C:\.
  --args "..."       Arguments passed to the exe (quoted as a single string).
  --stdout PATH      Where to copy captured stdout. Default: stream to host stdout.
  --include PATH     Additional file to stage into C:\ (repeatable). If CWSDPMI
                     is present at vendor/cwsdpmi/cwsdpmi.exe it is auto-included.
  --fast             Use dosbox-x-fast.conf (cycles=max) instead of parity config.
  --conf PATH        Use an arbitrary DOSBox-X conf instead of either shipped one.
                     Mutually exclusive with --fast (say which conf you mean).
                     The two shipped confs deliberately set oplmode=none, so a
                     port validating a MIDI backend under DOSBox-X (dosags's
                     Sound gate needed oplmode=opl3 for OPL3 chip detection)
                     previously had to stage a scratch conf and hand-run this
                     script; that conf now lives in the port's own tree and is
                     passed here. Same staging/RUN.BAT/exit behavior otherwise --
                     this only changes which file lands on `-conf`.
  --interactive      Open the DOSBox-X window instead of running headless.
  --keep-stage       Leave the staging temp dir on exit (useful for debugging,
                     or for a harness that reads results out of it). Mark it
                     `dosbox-stage.sh copied <stage>` once read, so
                     `dosbox-stage.sh gc` may collect it after the grace.
  --stage-file PATH  Write the stage's path to PATH before DOSBox-X starts.
                     Default with --stdout OUT: OUT.stage. The path is also
                     printed to stderr ("dosbox-run.sh: stage <path> ...").
                     Env DOSBOX_STAGE_CALLER names the caller recorded in
                     <stage>.meta (default: the calling script, see
                     dosbox-stage.sh).
  --merge-stderr     Capture both stdout and stderr (>STDOUT.TXT 2>&1). Needed for
                     programs that log to stderr (e.g. SDL_Log on DJGPP). DJGPP's
                     runtime processes 2>&1 itself so the syntax works under DOS.
  --env KEY=VALUE    SET KEY=VALUE in RUN.BAT before invoking the exe (repeatable).
                     This is how to reach an SDL_HINT_* value from this script --
                     SDL_GetHint() falls back to getenv() for anything never
                     SDL_SetHint()'d in code, and RUN.BAT's SET is a real DOS
                     environment variable the exe's own getenv() sees, exactly
                     like this script's own hardcoded BLASTER/
                     SDL_DOS_AUDIO_SB_SKIP_DETECTION lines below. Previously a
                     real gap (dosags S5 WaveBlaster validation had to stage
                     RUN.BAT by hand for exactly this); mirrors tests/harness/
                     run-rig.sh's own --env flag (real-hardware side) for the
                     DOSBox-X side.
  --sb-live          Run with DOSBox-X's Sound Blaster actually playing: drop
                     `-silent` (which disables the emulated SB outright), run
                     under a private Xvfb display allocated from
                     DOSBOX_DISPLAY_RANGE (default 300-399) and held for the
                     run (or DOSBOX_DISPLAY if set, whose display is then
                     held the same way), send host audio to SDL_AUDIODRIVER=dummy unless set,
                     and do NOT set SDL_DOS_AUDIO_SB_SKIP_DETECTION, so SB
                     detection runs as it does on real hardware. Use it for any
                     run whose audio figures (IRQ counts, ring fill, clip timing)
                     should mean something. Clip timing becomes device-paced, so
                     a run that waits on sounds is no longer tick-deterministic
                     across two runs -- keep screen-equality A/B rows sound-off.
  --lock MODE        Host-wide lock on /tmp/dos-port-dosbox.lock (flock), held
                     while DOSBox-X runs. Default: exclusive with --sb-live or
                     --interactive (timing/screen-dependent), shared otherwise.
                     "none" skips it (only for a caller that knows why).
                     SHARED on a run with a display (--sb-live or
                     --interactive) also needs --no-host-input (env
                     DOSBOX_HOST_INPUT=none): the caller asserts that the
                     cell's input comes from inside the guest (AutoPilot,
                     tick-keyed scripts) or that it has none. Input injected
                     from the host (xdotool clicks and keys) arrives at
                     wall-clock times and lands on a different emulated tick
                     when the host is loaded -- not deterministic even at
                     fixed cycles -- so such a cell takes EXCLUSIVE.
                     Env: DOSBOX_LOCK_MODE (same values), DOSBOX_LOCK_WAIT
                     (seconds to wait, default 3600; then exit 75),
                     DOSBOX_LOCK_FILE (default /tmp/dos-port-dosbox.lock),
                     DOSBOX_LOCK_HELD=1 (a parent -- a gate, an ABBA series,
                     ratchet.sh -- already holds the lock exclusively around
                     this call: don't take it again, which would deadlock).
                     DOSBOX_DISPLAY_HELD=1 is the same for the display: the
                     caller holds the pinned DOSBOX_DISPLAY's flock (fd 5).
                     Writers have priority: a queued EXCLUSIVE taker blocks
                     new SHARED takers (intent lock <lockfile>.intent), so a
                     stream of shared runs cannot starve it. Holders and
                     waiting writers are listed in <lockfile>.who
                     (informational; a waiter prints it, each line marked
                     alive or STALE). Implementation: dosbox-lock-lib.sh.
  -h, --help         Show this help.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --exe)         EXE="$2"; shift 2 ;;
    --args)        ARGS="$2"; shift 2 ;;
    --stdout)      STDOUT_PATH="$2"; shift 2 ;;
    --include)     INCLUDES+=("$2"); shift 2 ;;
    --fast)        FAST=1; shift ;;
    --conf)        CONF_OVERRIDE="$2"; shift 2 ;;
    --interactive) INTERACTIVE=1; shift ;;
    --keep-stage)  KEEP_STAGE=1; shift ;;
    --stage-file)  STAGE_FILE="$2"; shift 2 ;;
    --merge-stderr) MERGE_STDERR=1; shift ;;
    --env)         ENVS+=("$2"); shift 2 ;;
    --sb-live)     SB_LIVE=1; shift ;;
    --lock)        LOCK_MODE="$2"; shift 2 ;;
    --no-host-input) HOST_INPUT=none; shift ;;
    -h|--help)     usage; exit 0 ;;
    *) echo "dosbox-run.sh: unknown arg: $1" >&2; usage; exit 2 ;;
  esac
done

if [[ -n "${CONF_OVERRIDE:-}" && "$FAST" == "1" ]]; then
  echo "dosbox-run.sh: --conf and --fast are mutually exclusive (pass the conf you actually want)" >&2
  exit 2
fi
if [[ -n "${CONF_OVERRIDE:-}" ]]; then
  CONF="$CONF_OVERRIDE"
elif [[ "$FAST" == "1" ]]; then
  CONF="$CONF_FAST"
else
  CONF="$CONF_PARITY"
fi

if [[ -z "$EXE" ]]; then
  echo "dosbox-run.sh: --exe is required" >&2
  usage; exit 2
fi
if [[ ! -f "$EXE" ]]; then
  echo "dosbox-run.sh: exe not found: $EXE" >&2
  exit 2
fi
if [[ ! -f "$CONF" ]]; then
  echo "dosbox-run.sh: conf not found: $CONF" >&2
  exit 2
fi
for e in "${ENVS[@]:-}"; do
  [[ -z "$e" ]] && continue
  if [[ "$e" != *=* ]]; then
    echo "dosbox-run.sh: --env expects KEY=VALUE, got: $e" >&2
    exit 2
  fi
done

# Stage ---------------------------------------------------------------------
# dosbox-stage.sh (next to this script in the hub; a port runs this script
# through a tools/ symlink, so resolve the real path) supplies the caller
# name and the .meta writer.
# shellcheck source=./dosbox-stage.sh
source "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/dosbox-stage.sh"
STAGE_CALLER="$(dbxstage_caller "$PPID")"

# Low disk: kept stages of a harness that never deletes them filled the
# host disk once (2026-09-25). Below DOSBOX_STAGE_GC_FREE_MB (default
# 10240) free in the stage directory, collect THIS caller's own stages by
# dosbox-stage.sh's rules (never another caller's -- that is a person's
# explicit "dosbox-stage.sh gc"); below DOSBOX_STAGE_MIN_FREE_MB (default
# 1024), refuse to start rather than fail mid-run on ENOSPC.
# DOSBOX_STAGE_AUTO_GC=0 skips the collection (never the refusal).
STAGE_DIR_ROOT="${TMPDIR:-/tmp}"
_stage_free_mb() { df -Pk "$STAGE_DIR_ROOT" 2>/dev/null | awk 'NR == 2 { print int($4 / 1024) }'; }
FREE_MB="$(_stage_free_mb)"
if [[ -n "$FREE_MB" && "$FREE_MB" -lt "${DOSBOX_STAGE_GC_FREE_MB:-10240}" && "${DOSBOX_STAGE_AUTO_GC:-1}" != "0" ]]; then
  echo "dosbox-run.sh: ${FREE_MB} MB free in $STAGE_DIR_ROOT; collecting caller $STAGE_CALLER's own old stages" >&2
  bash "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/dosbox-stage.sh" gc --apply --quiet \
    --dir "$STAGE_DIR_ROOT" --caller "$STAGE_CALLER" 5>&- 6>&- 7>&- 8>&- 9>&- >&2 || true
  FREE_MB="$(_stage_free_mb)"
fi
if [[ -n "$FREE_MB" && "$FREE_MB" -lt "${DOSBOX_STAGE_MIN_FREE_MB:-1024}" ]]; then
  echo "dosbox-run.sh: only ${FREE_MB} MB free in $STAGE_DIR_ROOT (< ${DOSBOX_STAGE_MIN_FREE_MB:-1024} MB); not running." >&2
  echo "  Old stages are the usual cause. See what can go:  $(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/dosbox-stage.sh gc --dir $STAGE_DIR_ROOT" >&2
  echo "  (a dry run; add --apply to delete -- it collects every caller's stages, so it is a person's call)" >&2
  exit 73
fi
STAGE="$(mktemp -d -t dos-port-dosbox.XXXXXX)"
dbxstage_meta_add "$STAGE" "caller=$STAGE_CALLER" "pid=$$" "pstart=$(_dbxstage_pstart $$)" \
  "created=$(date +%s)" "keep=$KEEP_STAGE" "exe=$(basename "$EXE")"
cleanup() {
  local rc=$?
  # release: drop our .who line while fd 8 still holds the lock
  if [[ "${LOCK_HELD_HERE:-0}" == "1" ]]; then dbxlock_release; fi
  if [[ "$KEEP_STAGE" == "1" ]]; then
    dbxstage_meta_add "$STAGE" "exit=$rc" "ended=$(date +%s)"
    echo "dosbox-run.sh: stage kept at $STAGE" >&2
  else
    rm -rf "$STAGE" "$STAGE.meta"
    echo "dosbox-run.sh: stage $STAGE removed on exit" >&2
  fi
}
trap cleanup EXIT
echo "dosbox-run.sh: stage $STAGE ($([[ "$KEEP_STAGE" == "1" ]] && echo kept || echo removed) on exit; caller $STAGE_CALLER)" >&2
if [[ -z "$STAGE_FILE" && -n "$STDOUT_PATH" ]]; then STAGE_FILE="$STDOUT_PATH.stage"; fi
if [[ -n "$STAGE_FILE" ]]; then
  echo "$STAGE" > "$STAGE_FILE" || { echo "dosbox-run.sh: cannot write --stage-file $STAGE_FILE" >&2; exit 2; }
fi

cp "$EXE" "$STAGE/"
EXE_BASENAME="$(basename "$EXE")"
EXE_DOSNAME="${EXE_BASENAME^^}"

# Auto-include cwsdpmi.exe if vendored. DJGPP binaries need it; the hello
# smoke test doesn't strictly, but having it in the stage costs nothing.
if [[ -f "$CWSDPMI" && ! " ${INCLUDES[*]:-} " =~ [[:space:]]"$CWSDPMI"[[:space:]] ]]; then
  INCLUDES+=("$CWSDPMI")
fi

for f in "${INCLUDES[@]:-}"; do
  [[ -z "$f" ]] && continue
  if [[ ! -f "$f" ]]; then
    echo "dosbox-run.sh: --include not found: $f" >&2
    exit 2
  fi
  # DOS uppercases at runtime; uppercase here so the RUN.BAT references match.
  dest_name="$(basename "$f" | tr '[:lower:]' '[:upper:]')"
  cp "$f" "$STAGE/$dest_name"
done

# RUN.BAT -- invokes the exe with stdout captured to STDOUT.TXT.
# With --merge-stderr we add `2>&1` so SDL_Log (and any stderr writer) is also
# captured. DJGPP's runtime parses argv-level redirection itself, so 2>&1 works
# under DOS even though MS-DOS COMMAND.COM proper doesn't support it natively.
if [[ "$MERGE_STDERR" == "1" ]]; then
  REDIR='> STDOUT.TXT 2>&1'
else
  REDIR='> STDOUT.TXT'
fi
case "${EXE_DOSNAME##*.}" in
  BAT|bat)
    INVOKE_LINE="$(printf 'COMMAND /C %s %s %s\r\n' "$EXE_DOSNAME" "$ARGS" "$REDIR")"
    ;;
  *)
    INVOKE_LINE="$(printf '%s %s %s\r\n' "$EXE_DOSNAME" "$ARGS" "$REDIR")"
    ;;
esac
{
  printf '@ECHO OFF\r\n'
  printf 'SET BLASTER=A220 I5 D1 H5 T6\r\n'
  # SDL_DOS_AUDIO_SB_SKIP_DETECTION -- escape hatch from patches/SDL/0001.
  # Needed ONLY because the default mode runs `-silent`, which switches the
  # emulated SB off (DSP reads 0xFF, no IRQ). CORRECTED 2026-09-22: this
  # comment used to say DOSBox-X's SB16 returns 0xFF "regardless of timing
  # tuning"; that was the -silent artifact -- without -silent detection passes
  # (dsp_ver=4). With skip set here the driver opens a device that never plays.
  # --sb-live leaves it unset. Real hardware MUST NOT set it (it would mask a
  # real card-detection failure).
  if [[ "$SB_LIVE" != "1" ]]; then
    printf 'SET SDL_DOS_AUDIO_SB_SKIP_DETECTION=1\r\n'
  fi
  for e in "${ENVS[@]:-}"; do
    [[ -z "$e" ]] && continue
    printf 'SET %s\r\n' "$e"
  done
  printf '%s' "$INVOKE_LINE"
} > "$STAGE/RUN.BAT"

# Host-wide lock ------------------------------------------------------------
# Taken as late as possible (staging above needs no lock) and held by fd 8
# until this script exits; the dosbox-x child inherits the fd, so if this
# script is killed while DOSBox-X lives on, the lock stays held until
# DOSBox-X is gone too -- which is exactly what it protects.
if [[ -z "$LOCK_MODE" ]]; then
  if [[ "$SB_LIVE" == "1" || "$INTERACTIVE" == "1" ]]; then LOCK_MODE=exclusive; else LOCK_MODE=shared; fi
fi
case "$LOCK_MODE" in shared|exclusive|none) ;; *) echo "dosbox-run.sh: --lock must be shared, exclusive or none" >&2; exit 2 ;; esac
if [[ "$LOCK_MODE" == "shared" && ( "$SB_LIVE" == "1" || "$INTERACTIVE" == "1" ) && "$HOST_INPUT" != "none" ]]; then
  echo "dosbox-run.sh: refusing --lock shared on a run with a display (--sb-live/--interactive) without --no-host-input." >&2
  echo "  Host-injected input (xdotool) lands on a different emulated tick when the host is loaded, so it is not" >&2
  echo "  deterministic even at fixed cycles: such a cell takes EXCLUSIVE. Pass --no-host-input only when the cell's" >&2
  echo "  input comes from inside the guest (AutoPilot, tick-keyed scripts) or it has none." >&2
  exit 2
fi
if [[ "${DOSBOX_LOCK_HELD:-0}" == "1" ]]; then
  LOCK_MODE=none   # the caller holds it exclusively around us
fi
# dosbox-lock-lib.sh sits next to this script in the hub; a port runs this
# script through a tools/ symlink, so resolve the real path first.
# shellcheck source=./dosbox-lock-lib.sh
source "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/dosbox-lock-lib.sh"
if [[ "$LOCK_MODE" != "none" ]]; then
  if ! dbxlock_acquire "$LOCK_MODE" "${DOSBOX_LOCK_WAIT:-3600}" \
        "tool=dosbox-run.sh exe=$(basename "$EXE")"; then
    echo "dosbox-run.sh: DOSBox-X lock still busy after ${DOSBOX_LOCK_WAIT:-3600}s; not running" >&2
    exit 75
  fi
  LOCK_HELD_HERE=1
fi

# Display -------------------------------------------------------------------
# Taken AFTER the host lock -- always that order, so no two takers can each
# hold one and wait for the other. --sb-live and --interactive draw on an X
# display; two cells on one display land in each other's root-window
# captures, so the display is held by a per-display flock for this run (see
# dosbox-lock-lib.sh, DISPLAY ALLOCATION):
#   - pinned (DOSBOX_DISPLAY set, other than :0): take that display's flock;
#     a second cell pinned to the same display waits for this one;
#   - unpinned --sb-live: allocate a free display from DOSBOX_DISPLAY_RANGE
#     (default 300-399) and start our own Xvfb on it (was: xvfb-run -a's
#     own pick, which two concurrent runs could race). Done for any lock mode:
#     under DOSBOX_LOCK_HELD the mode is the parent's and unknown here, and
#     allocating costs an exclusive run nothing;
#   - unpinned --interactive goes to :0, the human's desktop: no display
#     lock (a person watching, not a capture).
# The flock sits on fd 5, which DOSBox-X (only) inherits.
# DOSBOX_DISPLAY_HELD=1 (mirrors DOSBOX_LOCK_HELD): the caller already holds
# the pinned display's flock -- typically so it can start its own Xvfb only
# once it owns the display. Taking it again here would open a NEW file
# description on the same lock file and block on our own parent forever,
# so it is skipped. It requires a pinned DOSBOX_DISPLAY.
ALLOC_DISPLAY=""
if [[ "${DOSBOX_DISPLAY_HELD:-0}" == "1" && -z "${DOSBOX_DISPLAY:-}" ]]; then
  echo "dosbox-run.sh: DOSBOX_DISPLAY_HELD=1 needs DOSBOX_DISPLAY (the display the caller holds)" >&2
  exit 2
fi
if [[ "$SB_LIVE" == "1" || "$INTERACTIVE" == "1" ]]; then
  if [[ -n "${DOSBOX_DISPLAY:-}" ]]; then
    PIN_NUM="$(dbxlock_display_num "$DOSBOX_DISPLAY")"
    if [[ -n "$PIN_NUM" && "$PIN_NUM" != "0" && "${DOSBOX_DISPLAY_HELD:-0}" != "1" ]]; then
      if ! dbxlock_display_acquire "$PIN_NUM" >/dev/null; then
        echo "dosbox-run.sh: display :$PIN_NUM still in use by another cell after ${DOSBOX_LOCK_WAIT:-3600}s; not running" >&2
        exit 75
      fi
    fi
  elif [[ "$SB_LIVE" == "1" ]]; then
    if ! dbxlock_display_acquire >/dev/null; then
      echo "dosbox-run.sh: no free display in ${DOSBOX_DISPLAY_RANGE:-300-399} after ${DOSBOX_LOCK_WAIT:-3600}s; not running" >&2
      exit 75
    fi
    ALLOC_DISPLAY="$DBXLOCK_DISPLAY"
  fi
fi

# Invoke DOSBox-X -----------------------------------------------------------
DBX_ARGS=(-conf "$CONF" -nopromptfolder
          -c "MOUNT C $STAGE"
          -c "C:"
          -c "CALL RUN.BAT")

# FDs: DOSBox-X keeps fd 8 (the host lock) and fd 5 (its display flock) --
# deliberately, so they last exactly as long as it does. Every other lock
# fd (6, 7, 9) is closed for it, and anything else that could outlive the
# cell (Xvfb) gets all of 5-9 closed: an orphaned helper holding fd 8 keeps
# the lock forever (dosags, 2026-09-24: an Xvfb held it SHARED after its
# parent exited, and every writer deadlocked).
if [[ "$INTERACTIVE" == "1" ]]; then
  # Visible run -- user drives; don't auto-exit.
  export DISPLAY="${DOSBOX_DISPLAY:-:0}"
  dosbox-x "${DBX_ARGS[@]}" 6>&- 7>&- 9>&-
elif [[ "$SB_LIVE" == "1" ]]; then
  # No -silent: it disables the emulated SB. Needs a display instead.
  DBX_ARGS+=(-c "EXIT" -exit -nogui -nomenu)
  export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
  if [[ -n "${DOSBOX_DISPLAY:-}" ]]; then
    DISPLAY="$DOSBOX_DISPLAY" dosbox-x "${DBX_ARGS[@]}" 6>&- 7>&- 9>&- >/dev/null 2>&1 || {
      rc=$?
      echo "dosbox-run.sh: dosbox-x exited non-zero ($rc)" >&2
      exit $rc
    }
  else
    if ! command -v Xvfb >/dev/null 2>&1; then
      echo "dosbox-run.sh: --sb-live needs Xvfb (or DOSBOX_DISPLAY set)" >&2
      exit 2
    fi
    # Our own Xvfb rather than xvfb-run, so it can be started with every
    # lock fd closed (xvfb-run's Xvfb inherited fds 5 and 8 and, orphaned,
    # would hold both). A watchdog -- also holding no lock fd -- stops it
    # once DOSBox-X is gone, even if this script was killed first.
    Xvfb ":$ALLOC_DISPLAY" -screen 0 1280x1024x24 -nolisten tcp 5>&- 6>&- 7>&- 8>&- 9>&- >/dev/null 2>&1 &
    XVFB_PID=$!
    for _ in $(seq 1 50); do
      [[ -S "/tmp/.X11-unix/X$ALLOC_DISPLAY" ]] && break
      kill -0 "$XVFB_PID" 2>/dev/null || break
      sleep 0.1
    done
    if ! kill -0 "$XVFB_PID" 2>/dev/null; then
      echo "dosbox-run.sh: Xvfb failed to start on :$ALLOC_DISPLAY" >&2
      exit 2
    fi
    DISPLAY=":$ALLOC_DISPLAY" dosbox-x "${DBX_ARGS[@]}" 6>&- 7>&- 9>&- >/dev/null 2>&1 &
    DBX_PID=$!
    ( exec 5>&- 6>&- 7>&- 8>&- 9>&-
      while kill -0 "$DBX_PID" 2>/dev/null; do sleep 1; done
      kill "$XVFB_PID" 2>/dev/null ) >/dev/null 2>&1 &
    rc=0; wait "$DBX_PID" || rc=$?
    # stop our Xvfb and WAIT for it to be gone before this script exits and
    # releases the display flock: otherwise the next owner of the display
    # starts its Xvfb while ours is still shutting down, and loses it
    kill "$XVFB_PID" 2>/dev/null || true
    wait "$XVFB_PID" 2>/dev/null || true
    if [[ "$rc" != "0" ]]; then
      echo "dosbox-run.sh: dosbox-x exited non-zero ($rc)" >&2
      exit "$rc"
    fi
  fi
else
  DBX_ARGS+=(-c "EXIT" -silent -exit -nogui -nomenu)
  dosbox-x "${DBX_ARGS[@]}" 6>&- 7>&- 9>&- >/dev/null 2>&1 || {
    rc=$?
    echo "dosbox-run.sh: dosbox-x exited non-zero ($rc)" >&2
    exit $rc
  }
fi

# Deliver captured stdout ---------------------------------------------------
OUT="$STAGE/STDOUT.TXT"
if [[ -f "$OUT" ]]; then
  if [[ -n "$STDOUT_PATH" ]]; then
    cp "$OUT" "$STDOUT_PATH"
  else
    cat "$OUT"
  fi
else
  echo "dosbox-run.sh: no STDOUT.TXT produced (exe may have crashed under DPMI)" >&2
  exit 3
fi
