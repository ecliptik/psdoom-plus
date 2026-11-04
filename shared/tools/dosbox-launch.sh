#!/usr/bin/env bash
# dosbox-launch.sh -- launch DOSBox-X visible on the local X session for
# manual testing, screenshot capture, and xdotool automation.
#
# Differs from tools/dosbox-run.sh (which stages one exe and exits): this
# launcher mounts the repo and vendored CWSDPMI and leaves DOSBox-X running
# so you can drive it by hand or by script.
#
# Usage:
#   tools/dosbox-launch.sh                         # open DOSBox-X at C:\ (repo root)
#   tools/dosbox-launch.sh --fast                  # use dosbox-x-fast.conf
#   tools/dosbox-launch.sh --kill-first            # kill the instance on this DISPLAY first
#   tools/dosbox-launch.sh --exe build/game.exe    # auto-run on launch
#   tools/dosbox-launch.sh --stage                 # mount build/stage/ as C:
#   tools/dosbox-launch.sh --stage --exe GAME.EXE  # stage + auto-run
#   DOSBOX_DISPLAY=auto tools/dosbox-launch.sh     # own headless display (300-399)
#
# The --stage form runs `make stage` first (so GAME.EXE + CWSDPMI.EXE +
# data/ all sit together) and mounts that staging dir as C:. This matches the
# eventual install layout under C:\GAME\ on a real CF card and is the layout
# most engines expect when they resolve their asset directory relative to
# SDL_GetBasePath(). Use this for any test that needs the game's data tree.
#
# After launch:
#   DISPLAY=:0 scrot -u /tmp/dosbox.png                   # capture focused window
#   DISPLAY=:0 xdotool search --name DOSBox windowactivate --sync
#   DISPLAY=:0 xdotool type --delay 40 'GAME'
#   DISPLAY=:0 xdotool key Return
#   Ctrl+F9 in the window, or --kill-first on the next launch  # stop it
#   (never `pkill -x dosbox-x`: it kills every workstream's DOSBox-X)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

CONF_PARITY="$SCRIPT_DIR/dosbox-x.conf"
CONF_FAST="$SCRIPT_DIR/dosbox-x-fast.conf"
CONF="$CONF_PARITY"

# DOS-PORT: explicit conf override, e.g.
#   DOSBOX_CONF=tools/dosbox-x-oversized.conf tools/dosbox-launch.sh --stage ...
# Applied AFTER flag parsing below so it beats --fast / --parity. Exists
# because the Mach64 centring path (patch 0296) only engages on a backend
# that cannot offer 320x240, which needs its own conf -- and swapping
# dosbox-x-fast.conf in place to get it is how a half-finished debug session
# leaves a wrong parity config behind.
CONF_OVERRIDE="${DOSBOX_CONF:-}"

KILL_FIRST=0
EXE=""
STAGE=0

usage() {
  cat <<'USAGE'
Usage: dosbox-launch.sh [--fast] [--kill-first] [--stage] [--exe PATH]

  --fast, -f         Use dosbox-x-fast.conf (cycles=max) instead of parity config
  --kill-first, -k   Kill the dosbox-x already running on this DISPLAY first
                     (only that one: other displays belong to other work)
  --stage, -s        Run `make stage` and mount build/stage/ as C: (where
                     GAME.EXE + CWSDPMI.EXE + data/ sit together -- the
                     layout NXEngine-evo's ResourceManager expects on DOS).
  --exe PATH         Path to an .exe to auto-run. Without --stage, PATH is
                     relative to repo root. With --stage, PATH should be a
                     bare DOS-side filename (e.g. GAME.EXE).
  --keep-running     Accepted no-op: this launcher already backgrounds DOSBox-X
                     and never auto-exits, so the window stays open for manual
                     review / xdotool driving. Provided so documented review
                     commands that pass it do not error.
  -h, --help         Show this help
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --fast|-f)       CONF="$CONF_FAST"; shift ;;
    --kill-first|-k) KILL_FIRST=1; shift ;;
    --stage|-s)      STAGE=1; shift ;;
    --exe)           EXE="$2"; shift 2 ;;
    --keep-running)  shift ;;   # no-op: default behavior already keeps the window open
    -h|--help)       usage; exit 0 ;;
    *) echo "dosbox-launch.sh: unknown arg: $1" >&2; usage; exit 2 ;;
  esac
done

if [[ -n "$CONF_OVERRIDE" ]]; then
  # Relative paths resolve against the repo root, not the caller's cwd.
  [[ "$CONF_OVERRIDE" = /* ]] || CONF_OVERRIDE="$REPO_ROOT/$CONF_OVERRIDE"
  CONF="$CONF_OVERRIDE"
  echo "dosbox-launch.sh: conf overridden via DOSBOX_CONF -> $CONF"
fi

# Scope every "is it running / kill it" to DOSBox-X on OUR display. This
# used to be a host-wide `pgrep -x` refusal plus a global `pkill -x dosbox-x`
# for --kill-first, which killed other workstreams' runs (a gate capturing on
# its own Xvfb display, an --sb-live cell under xvfb-run) -- the hazard
# dosbox-teardown.sh documents. See dbx_pids_on_display there.
# shellcheck source=./dosbox-teardown.sh
source "$SCRIPT_DIR/dosbox-teardown.sh"
# the display allocator (dosbox-lock-lib.sh, next to the real script)
# shellcheck source=./dosbox-lock-lib.sh
source "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/dosbox-lock-lib.sh"

# Display (see dosbox-lock-lib.sh, DISPLAY ALLOCATION). This launcher takes
# no host lock, only a display:
#   - DOSBOX_DISPLAY=auto: allocate a free display from DOSBOX_DISPLAY_RANGE
#     (default 300-399) and start a headless Xvfb on it for this session --
#     for scripted/agent sessions that drive DOSBox-X with xdotool;
#   - DOSBOX_DISPLAY=:N (N != 0): take display N's flock, so this window can
#     never share a display with another cell's captures; wait up to
#     DOSBOX_DISPLAY_WAIT seconds (default 10), then refuse;
#   - unset or :0: the human's desktop, no display lock (as before).
# The flock is inherited by DOSBox-X (and, in auto mode, by the small
# supervisor that stops our Xvfb when DOSBox-X exits), so it lasts exactly
# as long as the session, after this script has returned. Everything this
# launcher starts has the other lock fds (6-9) closed, and our Xvfb all of
# 5-9: a caller that holds the host lock on fd 8 and launches a session must
# not have that session -- or an orphaned Xvfb -- keep its lock alive.
AUTO_DISPLAY=0
if [[ "${DOSBOX_DISPLAY:-}" == "auto" ]]; then
  if ! dbxlock_display_acquire "" "${DOSBOX_DISPLAY_WAIT:-10}" >/dev/null; then
    echo "dosbox-launch.sh: no free display in ${DOSBOX_DISPLAY_RANGE:-300-399}" >&2
    exit 1
  fi
  DOSBOX_DISPLAY=":$DBXLOCK_DISPLAY"; AUTO_DISPLAY=1
elif [[ -n "${DOSBOX_DISPLAY:-}" ]]; then
  _pin="$(dbxlock_display_num "$DOSBOX_DISPLAY")"
  # DOSBOX_DISPLAY_HELD=1: the caller holds this display's flock already
  # (see dosbox-run.sh); taking it again would block on the caller.
  if [[ -n "$_pin" && "$_pin" != "0" && "${DOSBOX_DISPLAY_HELD:-0}" != "1" ]]; then
    if ! dbxlock_display_acquire "$_pin" "${DOSBOX_DISPLAY_WAIT:-10}" >/dev/null; then
      echo "dosbox-launch.sh: display :$_pin is held by another cell (still after ${DOSBOX_DISPLAY_WAIT:-10}s); not launching. Use DOSBOX_DISPLAY=auto or another display." >&2
      exit 1
    fi
  fi
fi
LAUNCH_DISPLAY="${DOSBOX_DISPLAY:-:0}"

if [[ "$KILL_FIRST" == "1" ]] && [[ -n "$(dbx_pids_on_display "$LAUNCH_DISPLAY")" ]]; then
  echo "Stopping the dosbox-x running on $LAUNCH_DISPLAY..."
  dbx_kill_display "$LAUNCH_DISPLAY"
  sleep 1
  dbx_kill_display "$LAUNCH_DISPLAY" KILL
fi

if [[ -n "$(dbx_pids_on_display "$LAUNCH_DISPLAY")" ]]; then
  echo "dosbox-x is already running on $LAUNCH_DISPLAY. Use --kill-first to restart it." >&2
  exit 1
fi
# A DOSBox-X from another sandbox or user is invisible to the pid check above
# (its environ is unreadable) but its window is on the display. It is not
# ours to kill, even with --kill-first: refuse.
if dbx_window_on_display "$LAUNCH_DISPLAY"; then
  echo "a DOSBox-X window is already on $LAUNCH_DISPLAY and it is not one this session can identify (another sandbox or user): not launching and not killing it. Use another DOSBOX_DISPLAY, or stop that run." >&2
  exit 1
fi

if [[ ! -f "$CONF" ]]; then
  echo "dosbox-launch.sh: conf not found: $CONF" >&2
  exit 2
fi

# Always target the local X session (:0), not any SSH-forwarded DISPLAY the
# caller's shell might have inherited. Override with DOSBOX_DISPLAY=... if
# genuinely needed. Matches the Snow / Basilisk / vellm emulator convention.
export DISPLAY="${DOSBOX_DISPLAY:-:0}"

# Mount layout depends on --stage:
#   default:  C: = repo root, D: = vendor/cwsdpmi
#             (handy for sdl3-smoke and one-off .exe testing)
#   --stage:  C: = build/stage/, D: = vendor/cwsdpmi
#             (the runtime layout GAME.EXE expects: GAME.EXE +
#              CWSDPMI.EXE + data/ all co-located, matching the install
#              layout under C:\GAME\ on real CF cards. SDL_GetBasePath()
#              + "data/" then resolves correctly.)
#
# D: always points at vendor/cwsdpmi so CWSDPMI.EXE is on PATH for DJGPP
# binaries that aren't yet staged. The BLASTER env var matches the
# [sblaster] block in dosbox-x.conf; SDL3-DOS reads it.
#
# SDL_DOS_AUDIO_SB_SKIP_DETECTION -- escape hatch from patches/SDL/0001.
# CORRECTED 2026-09-22: this comment used to say DOSBox-X's emulated SB16
# returns 0xFF on the DSP detection read regardless of timing tuning. That was
# an artifact of dosbox-run.sh's `-silent`, which switches the emulated SB off;
# this launcher never passes -silent, and here detection passes (dsp_ver=4,
# IRQ-5 firing) with or without the skip. It stays set for now only to keep
# interactive runs unchanged; it is not needed. Real hardware MUST NOT set it
# -- it would mask a real card-detection failure.

if [[ "$STAGE" == "1" ]]; then
  echo "Running 'make stage' to populate build/stage/..."
  make -C "$REPO_ROOT" stage >/dev/null
  C_DRIVE="$REPO_ROOT/build/stage"
else
  C_DRIVE="$REPO_ROOT"
fi

DBX_ARGS=(-conf "$CONF" -nopromptfolder
          -c "MOUNT C $C_DRIVE"
          -c "MOUNT D $REPO_ROOT/vendor/cwsdpmi"
          -c 'SET PATH=Z:\;C:\;D:\'
          -c 'SET BLASTER=A220 I5 D1 H5 T6'
          -c 'SET SDL_DOS_AUDIO_SB_SKIP_DETECTION=1'
          -c 'SET SDL_INVALID_PARAM_CHECKS=0')

# LAUNCH_LOG_VERBOSE_VAR -- the name of a guest-side env var this port's
# engine reads to force verbose/INFO-level boot logging, forced to 1 for
# this testing launcher so smoke-gate banner-emit checks can witness INFO
# banners even when the untagged-run default is WARN. Unset (the default)
# does nothing -- a port opts in by exporting e.g.
# LAUNCH_LOG_VERBOSE_VAR=DOS_PORT_LOG_VERBOSE before calling this script.
if [[ -n "${LAUNCH_LOG_VERBOSE_VAR:-}" ]]; then
  DBX_ARGS+=(-c "SET ${LAUNCH_LOG_VERBOSE_VAR}=1")
fi

DBX_ARGS+=(-c "C:")

# LAUNCH_EXTRA_SET -- optional caller-supplied env for the guest, as one or
# more NAME=VALUE pairs separated by whitespace, injected as additional DOS
# SET commands before the exe runs. Unset (the default) changes nothing.
# Injected AFTER the fixed SETs above so a caller can also override one of
# them deliberately (last SET of a name wins in DOS).
if [[ -n "${LAUNCH_EXTRA_SET:-}" ]]; then
  for _kv in $LAUNCH_EXTRA_SET; do
    DBX_ARGS+=(-c "SET $_kv")
    echo "  extra guest env: SET $_kv"
  done
fi

if [[ -n "$EXE" ]]; then
  EXE_DOS="$(echo "$EXE" | tr '/' '\\' | tr '[:lower:]' '[:upper:]')"
  DBX_ARGS+=(-c "$EXE_DOS")
fi

CONF_NAME="$(basename "$CONF")"
if [[ "$AUTO_DISPLAY" == "1" ]]; then
  # our own Xvfb (without the display fd, so it cannot outlive the session
  # by holding it), then DOSBox-X under a supervisor that holds the display
  # flock and stops the Xvfb once DOSBox-X exits
  Xvfb "$DISPLAY" -screen 0 1280x1024x24 -nolisten tcp 5>&- 6>&- 7>&- 8>&- 9>&- >/dev/null 2>&1 &
  XVFB_PID=$!
  for _ in 1 2 3 4 5 6 7 8 9 10; do xdpyinfo -display "$DISPLAY" >/dev/null 2>&1 && break; sleep 0.5; done
  echo "Launching DOSBox-X (DISPLAY=$DISPLAY, allocated, headless Xvfb pid $XVFB_PID, config=$CONF_NAME)..."
  # the supervisor releases the display flock only after our Xvfb is GONE
  # (not just signalled), so the next owner of the display cannot start its
  # Xvfb while ours is still shutting down
  ( dosbox-x "${DBX_ARGS[@]}"; kill "$XVFB_PID" 2>/dev/null
    for _ in $(seq 1 100); do kill -0 "$XVFB_PID" 2>/dev/null || break; sleep 0.1; done
  ) 6>&- 7>&- 8>&- 9>&- &
  SUP_PID=$!
  DBX_PID=""
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    DBX_PID="$(pgrep -P "$SUP_PID" 2>/dev/null | head -n 1 || true)"; [[ -n "$DBX_PID" ]] && break; sleep 0.2
  done
  DBX_PID="${DBX_PID:-$SUP_PID}"
else
  echo "Launching DOSBox-X (DISPLAY=$DISPLAY, config=$CONF_NAME)..."
  dosbox-x "${DBX_ARGS[@]}" 6>&- 7>&- 8>&- 9>&- &
  DBX_PID=$!
fi
echo "DOSBox-X running (PID $DBX_PID)."
echo
echo "  screenshot:  DISPLAY=$DISPLAY scrot -u /tmp/dosbox.png"
echo "  focus:       DISPLAY=$DISPLAY xdotool search --name DOSBox windowactivate --sync"
echo "  type:        DISPLAY=$DISPLAY xdotool type --delay 40 'GAME'"
echo "  key:         DISPLAY=$DISPLAY xdotool key Return"
echo "  stop:        Ctrl+F9 in the window, or kill $DBX_PID (never pkill -x dosbox-x)"
