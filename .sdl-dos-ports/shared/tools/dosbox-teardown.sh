# tools/dosbox-teardown.sh -- conf/PID-scoped DOSBox-X teardown helper (SOURCE, do not exec)
#
# WHY THIS EXISTS:
#   Test scripts historically tore DOSBox-X down with a global `pkill -x dosbox-x`,
#   which kills EVERY dosbox-x process on the machine -- not just the one the script
#   launched. With concurrent workstreams (an operator reviewing a build in one
#   window, a smoke gate running in another, a palette matrix in a third) that global
#   kill is a cross-workstream grenade: it has aborted a live operator review twice
#   this session. See [[dosbox_global_pkill_collides_concurrent_workstreams]].
#
#   dbx_kill_conf scopes the kill to the instance launched with a SPECIFIC -conf file.
#   It finds candidate pids via `pgrep -f` on the conf path, then -- the safety that
#   makes this robust -- kills ONLY pids whose /proc/<pid>/comm is an actual dosbox
#   binary. That comm-filter means it can never self-kill the calling shell (comm=bash)
#   even though the script's own command line contains the conf path (and so matches the
#   pgrep -f), and it never touches another conf's dosbox instance.
#
# USAGE:
#   source "$(dirname "$0")/../tools/dosbox-teardown.sh"   # path relative to the caller
#   dbx_kill_conf "$CONF"          # graceful TERM of the dosbox-x started with $CONF
#   dbx_kill_conf "$CONF" KILL     # force SIGKILL follow-up
#
# Guards: an empty conf arg is a no-op (returns 0) so a teardown in a script that never
# launched dosbox (early exit, or CONF unset) can't degrade into a broad match.

# dbx_kill_conf <conf-path> [signal]
#   signal defaults to TERM; pass KILL (or 9) for the force follow-up.
dbx_kill_conf() {
  local conf="$1"
  local sig="${2:-TERM}"

  # Empty/unset conf: refuse to match (an empty pattern would match broadly). No-op.
  if [[ -z "$conf" ]]; then
    return 0
  fi

  # Candidate pids: any process whose command line carries `-conf <conf>` or `-conf=<conf>`.
  # `-f` matches the full command line; `--` ends option parsing so a conf path starting
  # with `-` can't be read as a pgrep flag.
  local pids
  pids=$(pgrep -f -- "-conf[ =]*${conf}" 2>/dev/null || true)
  [[ -n "$pids" ]] || return 0

  local pid comm
  for pid in $pids; do
    # comm-filter: only signal real dosbox binaries. The calling shell (comm=bash) and any
    # other helper whose argv happens to contain the conf path are skipped. This is what
    # makes the helper safe to call from a script whose own argv matches the pgrep pattern.
    comm=$(cat "/proc/$pid/comm" 2>/dev/null || true)
    case "$comm" in
      dosbox-x|dosbox-x-fast|dbxreview|dosbox*)
        kill "-${sig}" "$pid" 2>/dev/null || true
        ;;
    esac
  done
  return 0
}

# dbx_pids_on_display <display>
#   Prints the pids of dosbox binaries (comm-filtered, as above) whose own
#   environment has DISPLAY=<display>, e.g. ":0" or ":251". Scoping by the
#   display a process actually draws on -- read from /proc/<pid>/environ, so
#   only this user's processes are visible, which is all we may signal anyway
#   -- lets a launcher find ITS instance on ITS display without touching a
#   DOSBox-X another workstream runs on another display, whatever conf either
#   uses. (Every `--fast` run shares one conf path, so conf scoping alone
#   cannot tell two workstreams apart.)
dbx_pids_on_display() {
  local disp="$1" pid comm d
  [[ -n "$disp" ]] || return 0
  for pid in $(pgrep -x dosbox-x 2>/dev/null; pgrep -x dosbox-x-fast 2>/dev/null); do
    comm=$(cat "/proc/$pid/comm" 2>/dev/null || true)
    case "$comm" in dosbox-x|dosbox-x-fast|dbxreview|dosbox*) ;; *) continue ;; esac
    # unreadable (another sandbox or user): not ours to identify -> skipped
    [[ -r "/proc/$pid/environ" ]] || continue
    d=$( { tr '\0' '\n' < "/proc/$pid/environ"; } 2>/dev/null | sed -n 's/^DISPLAY=//p' | head -n 1)
    [[ "$d" == "$disp" ]] && echo "$pid"
  done
  return 0
}

# dbx_kill_display <display> [signal] -- signal only the dosbox on <display>.
dbx_kill_display() {
  local sig="${2:-TERM}" pid
  for pid in $(dbx_pids_on_display "$1"); do
    kill "-${sig}" "$pid" 2>/dev/null || true
  done
  return 0
}

# dbx_window_on_display <display>
#   Exit 0 if the X server on <display> has a top-level window whose WM_CLASS
#   is dosbox-x -- whoever owns it. dbx_pids_on_display cannot see a DOSBox-X
#   started from another sandbox or user (its /proc environ is unreadable),
#   but its window is on the display all the same, and it would land in any
#   root-window capture there. The X server is the authority on what is on a
#   display. Exit 1 if there is none, or no X server answers.
dbx_window_on_display() {
  local disp="$1"
  [[ -n "$disp" ]] || return 1
  command -v xwininfo >/dev/null 2>&1 || return 1
  xwininfo -display "$disp" -root -children 2>/dev/null | grep -q '("dosbox-x"'
}
