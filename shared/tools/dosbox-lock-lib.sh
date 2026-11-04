# shared/tools/dosbox-lock-lib.sh -- the host-wide DOSBox-X lock, as a
# SOURCED library (do not exec). Used by dosbox-run.sh and ratchet.sh; a
# port's own harness may source it too (resolve the path with readlink -f
# through the tools/ symlink, as dosbox-run.sh does).
#
# THE LOCK: flock on $DBXLOCK_FILE (default /tmp/dos-port-dosbox.lock,
# env DOSBOX_LOCK_FILE), EXCLUSIVE for timing-, rate- and screen-dependent
# runs, SHARED for runs whose result does not depend on host load. See the
# lock section of shared/skills/dos-emulator-workflow/references/tooling.md.
#
# WRITER PRIORITY (intent lock, $DBXLOCK_FILE.intent): flock alone lets a
# new SHARED taker in while an EXCLUSIVE waiter is queued, so a stream of
# overlapping shared holders can starve a writer indefinitely (dosags saw an
# exclusive waiter wait over an hour, 2026-09-23). So every taker passes
# through the intent lock first:
#   EXCLUSIVE: intent EXCLUSIVE -> main EXCLUSIVE -> release intent.
#   SHARED:    intent SHARED    -> main SHARED    -> release intent.
# A queued writer holds intent while it waits for the main lock, so no new
# reader can get past intent: the writer only waits for the readers already
# inside. Readers that are themselves still waiting on the main lock hold
# intent SHARED, so a writer arriving behind them waits for them too -- the
# order of arrival is kept.
# The writer releases intent as soon as it holds the main lock, rather than
# keeping it for its whole run: a second writer arriving during that run then
# takes intent itself and waits on the main lock, which makes it the NEXT
# holder when the first writer finishes -- no reader can slip between them.
# (If the first writer kept intent, the second would queue on intent beside
# new readers, and when intent freed a reader could win it and make the
# second writer wait out that reader's whole run.)
#
# KNOWN RACE (design note; not fixed). flock queues are not FIFO. While
# writer W1 holds intent and waits on main, readers R arriving later block
# on intent -s and a second writer W2 blocks on intent -x. When W1 gets
# main and releases intent, the kernel wakes all of them and they race.
# Shared requests are compatible with each other, so in practice the
# readers win -- even readers that arrived AFTER W2. Those readers then
# hold intent -s while they wait on main behind W1, so W2 gets intent only
# once they are all inside, and then waits for their cells to end. W2's
# wait is W1's cell plus the overtaking readers' cells. It is bounded,
# because readers arriving after W2 takes intent are blocked, but it is not
# arrival order, and with many readers arriving during a long W1 cell it can
# be long.
#
# PROPOSED FIX (writer tickets; design only, no code yet):
#   - Replace the intent lock with an ordered queue. A taker draws a ticket
#     T (a counter file incremented under a tiny flock, like .who.lock) and
#     creates  <lock>.queue/<T>.<mode>.<pid>,  holding an EXCLUSIVE flock on
#     that entry file for as long as it is queued.
#   - Admission: a writer with ticket T waits until no entry < T exists; a
#     reader with ticket T waits until no WRITER entry < T exists. Readers
#     queued between two writers go in together, as one batch, and a
#     writer is never overtaken by a later arrival: phase-fair, FIFO
#     between phases.
#   - Waiting without polling: to wait for entry E, take `flock -s` on E's
#     file. It returns when E's owner has taken the main lock (and
#     removed E) or has died -- the flock drops with its process, so a
#     killed waiter cannot wedge the queue. Then re-scan and re-check.
#   - After admission: take the main lock in the requested mode (it still
#     excludes the current holders), then remove your own entry. Stale
#     entries (file gone, or pid not in /proc) are pruned by whoever scans.
#   - Test to write first: W1 holding, R1 queued, W2 queued, R2 queued.
#     The expected order is W1, R1, W2, R2. Today R2 can overtake W2.
#   - Cost: a queue directory, a ticket counter, and a scan per wake-up.
#     Worth doing if the race shows up in practice. The intent lock
#     already removes the unbounded starvation.
#
# .who ($DBXLOCK_FILE.who): informational, one line per holder or waiting
# writer, each removed by its own process on release/acquire:
#   pid=<pid> pstart=<start time> tool=... mode=shared|exclusive ... since=<utc>
#   waiting: pid=<pid> pstart=<start time> tool=... mode=exclusive ... since=<utc>
# Every edit prunes lines whose process is gone or was replaced (see
# _dbxlock_line_live), so a taker killed while queued leaves no permanent line.
# Readers of it mark each line (alive) or (STALE: pid N is not running) --
# a holder killed outright cannot clean up. The flock is the truth: to ask
# whether the lock is free, test it; never read .who.
#
# API (all return 0 on success):
#   dbxlock_acquire MODE WAIT_S DESC   MODE shared|exclusive; DESC is
#       "tool=... <more key=value>". Holds the main lock on fd 8 on success
#       (a child that inherits fd 8 keeps the lock alive with it). Returns 1
#       when WAIT_S seconds pass first. Prints one "waiting" line to stderr
#       (with the current .who) if it has to wait, unless DBXLOCK_QUIET=1.
#   dbxlock_release                    remove our .who lines, close fd 8.
#   dbxlock_describe                   the .who lines, each marked alive/STALE,
#                                      then the kernel's view (dbxlock_holders).
#   dbxlock_holders [FILE]             who really holds it: /proc/locks (with
#                                      ORPHANED for a dead placer) and /proc/*/fd
#                                      (SUSPECT for a holder that is not
#                                      DOSBox-X, a shell or a .sh tool).
# LONG-LIVED CHILDREN: anything started while a lock fd is open inherits it
# and keeps the lock alive for its whole life. Start every process that may
# outlive the cell -- Xvfb above all -- with fds 5, 6, 7, 8 and 9 closed
# (`5>&- 6>&- 7>&- 8>&- 9>&-`). Only the cell's own DOSBox-X keeps fd 8 (the
# lock) and fd 5 (its display), deliberately: they must last exactly as
# long as it does. (dosags, 2026-09-24: an orphaned Xvfb that inherited fd 8
# held the lock SHARED after its parent exited; every writer deadlocked.)
# fds used: 8 main (held), 7 intent (transient), 6 .who edits (transient),
# 5 display (held; see DISPLAY ALLOCATION below).
# DOSBOX_LOCK_HELD=1 is the CALLER's business: when a parent already holds
# the lock around us, do not call dbxlock_acquire at all.
# Writer priority acts only at acquire time: a SHARED hold spanning a whole
# series keeps every writer (and, behind the writer's intent, every new
# reader) waiting for the entire series. Shared series lock per cell.

DBXLOCK_FILE="${DOSBOX_LOCK_FILE:-/tmp/dos-port-dosbox.lock}"

_dbxlock_utc() { date -u +%Y-%m-%dT%H:%M:%SZ; }

# _dbxlock_pstart PID: the process's start time (field 22 of /proc/<pid>/stat,
# clock ticks since boot). With the pid it identifies one process for good:
# a reused pid has a different start time. Empty if the pid is gone.
_dbxlock_pstart() {
    local st rest
    st="$(cat "/proc/$1/stat" 2>/dev/null)" || return 0
    [[ -n "$st" ]] || return 0
    rest="${st##*) }"                 # comm may contain spaces or parentheses
    set -- $rest                      # fields 3.. of stat; starttime is the 20th
    echo "${20:-}"
}

# _dbxlock_line_live LINE: is the process a .who line names still the one that
# wrote it?
#   - pid gone                                        -> stale
#   - pstart=N recorded and the pid's start time != N  -> stale (pid reused)
#   - no pstart (an older line or another writer) and tool=<x>.sh whose name
#     is no longer in the pid's cmdline                -> stale (pid reused)
#   - otherwise (e.g. a label-style tool=lab-x with no pstart) -> live; a
#     reused pid can hide such a line until it dies, a small accepted risk
#     that ends as those lines age out.
_dbxlock_line_live() {
    local line="$1" pid ps tool
    pid="${line#waiting: }"; pid="${pid#pid=}"; pid="${pid%% *}"
    [[ "$pid" =~ ^[0-9]+$ && -d "/proc/$pid" ]] || return 1
    if [[ " $line " =~ [[:space:]]pstart=([0-9]+)[[:space:]] ]]; then
        [[ "$(_dbxlock_pstart "$pid")" == "${BASH_REMATCH[1]}" ]]; return
    fi
    if [[ " $line " =~ [[:space:]]tool=([^[:space:]]+\.sh)[[:space:]] ]]; then
        tool="${BASH_REMATCH[1]}"
        tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null | grep -qF -- "$tool" || return 1
    fi
    return 0
}

# All edits run under an exclusive flock on <lock>.who.lock, and every edit
# also PRUNES stale lines (holders and waiters whose process is gone or was
# replaced -- a taker killed while queued can never remove its own "waiting:"
# line). Pruning inside the same flock-protected edit means it cannot race a
# live writer adding or dropping its own line.
_dbxlock_who_edit() {   # $1 = add|drop|dropwait, $2 = line (add)
    local who="$DBXLOCK_FILE.who" tmp line
    exec 6>>"$DBXLOCK_FILE.who.lock" || return 0
    flock -x -w 5 6 || { exec 6>&-; return 0; }
    tmp="$who.$$"
    : > "$tmp" 2>/dev/null || { exec 6>&-; return 0; }
    if [[ -f "$who" ]]; then
        while IFS= read -r line; do
            [[ -n "$line" ]] || continue
            case "$1" in
                drop) [[ "$line" == "pid=$$ "* || "$line" == "waiting: pid=$$ "* ]] && continue ;;
                dropwait) [[ "$line" == "waiting: pid=$$ "* ]] && continue ;;
            esac
            _dbxlock_line_live "$line" || continue
            printf '%s\n' "$line" >> "$tmp"
        done < "$who"
    fi
    [[ "$1" == "add" ]] && printf '%s\n' "$2" >> "$tmp"
    if [[ -s "$tmp" ]]; then mv -f "$tmp" "$who"; else rm -f "$tmp" "$who"; fi
    exec 6>&-
    return 0
}

# _dbxlock_expected_comm COMM: is this a process that may legitimately hold
# the lock -- DOSBox-X itself, a shell, or a .sh tool (a script's comm is
# its file name, cut to 15 characters)?
_dbxlock_expected_comm() {
    case "$1" in
        dosbox-x|dosbox-x-fast|bash|sh|dash|zsh|ksh|flock|*.sh|*.s|dosbox-launch.*|dosbox-run.s*) return 0 ;;
    esac
    return 1
}

# _dbxlock_children PID: "pid comm" of each direct child (one /proc pass;
# only run for a wrapper holder, so the cost stays off the common path).
_dbxlock_children() {
    local p st rest pp comm
    for p in /proc/[0-9]*; do
        st="$(cat "$p/stat" 2>/dev/null)" || continue
        rest="${st##*) }"; set -- $rest; pp="${2:-}"
        if [[ "$pp" == "$WANT_PPID" ]]; then
            comm="$(cat "$p/comm" 2>/dev/null)"
            echo "${p#/proc/} $comm"
        fi
    done
}

# dbxlock_holders [FILE]: the KERNEL's view of who holds the lock, which
# .who cannot give. Two sources:
#   /proc/locks   -- one line per flock on the file's inode (and one per
#                    blocked waiter, "->"). Its pid is the process that PLACED
#                    the lock -- for `flock -s 8` that is the short-lived flock
#                    utility, normally already gone -- so it says THAT the lock
#                    is held and how, not by whom.
#   /proc/*/fd    -- every process we can see with the file open: the real
#                    holders. Any that is not DOSBox-X, a shell or a .sh tool
#                    (an Xvfb, a sleep, a python) is flagged SUSPECT: a helper
#                    that inherited the lock fd and keeps it alive.
# A lock held while no visible process has the file open is held from
# another sandbox or user (their /proc/<pid>/fd is unreadable) -- said so.
dbxlock_holders() {
    local f="${1:-$DBXLOCK_FILE}" ino real line pid comm mode p fdl found=0 seen=0
    [[ -e "$f" ]] || { echo "  (no lock file $f)"; return 0; }
    ino="$(stat -c %i "$f" 2>/dev/null)"; real="$(readlink -f "$f")"
    while IFS= read -r line; do
        [[ "$line" == *" FLOCK "* && "$line" == *":$ino "* ]] || continue
        found=1
        mode=shared; [[ "$line" == *" WRITE "* ]] && mode=exclusive
        pid="$(awk '{ for (i = 1; i <= NF; i++) if ($i ~ /^[0-9a-f]+:[0-9a-f]+:[0-9]+$/) { print $(i-1); exit } }' <<< "$line")"
        if [[ "$line" == *" -> "* ]]; then
            echo "  kernel: pid $pid ($(cat "/proc/$pid/comm" 2>/dev/null)) is WAITING for it $mode"
        elif [[ "$pid" =~ ^[0-9]+$ && -d "/proc/$pid" ]]; then
            echo "  kernel: held $mode (placed through pid $pid, $(cat "/proc/$pid/comm" 2>/dev/null))"
        else
            echo "  kernel: held $mode (placed through pid $pid, now gone -- normal for the flock utility)"
        fi
    done < /proc/locks
    (( found )) || echo "  kernel: no flock on $f"
    for p in /proc/[0-9]*; do
        pid="${p#/proc/}"
        for fdl in "$p"/fd/*; do
            [[ "$(readlink "$fdl" 2>/dev/null)" == "$real" ]] || continue
            comm="$(cat "$p/comm" 2>/dev/null)"; seen=1
            if _dbxlock_expected_comm "$comm"; then
                echo "  open by pid $pid ($comm)"
            elif [[ "$comm" == "timeout" || "$comm" == "env" || "$comm" == "setsid" ]]; then
                # a wrapper: fine when it wraps a cell (its lifetime is the
                # cell's), SUSPECT when what it wraps is a long-lived helper
                # (an Xvfb, a keep-alive, a sleep loop) -- that is how the
                # orphaned-Xvfb lock leak looked
                local kids kid kcomm cell=""
                kids="$(WANT_PPID="$pid" _dbxlock_children)"
                while read -r kid kcomm; do
                    [[ -n "$kid" ]] || continue
                    if _dbxlock_expected_comm "$kcomm"; then cell="$kid $kcomm"; break; fi
                done <<< "$kids"
                if [[ -n "$cell" ]]; then
                    echo "  open by pid $pid ($comm) -- a wrapper around a cell (child pid ${cell%% *}, ${cell#* })"
                elif [[ -z "$kids" ]]; then
                    echo "  open by pid $pid ($comm) -- SUSPECT: a $comm wrapper with no child left, still holding the lock fd"
                else
                    echo "  open by pid $pid ($comm) -- SUSPECT: a $comm wrapper whose child is $(head -n 1 <<< "$kids" | awk '{print "pid " $1 " (" $2 ")"}'), not a DOSBox-X cell"
                fi
            else
                echo "  open by pid $pid ($comm) -- SUSPECT: not DOSBox-X or its shell; a helper that inherited the lock fd?"
            fi
            break
        done
    done
    (( found && ! seen )) && echo "  no visible process has it open: held from another sandbox or user, or by a process that is exiting right now (their /proc/<pid>/fd cannot be read)"
    return 0
}

dbxlock_describe() {
    local who="$DBXLOCK_FILE.who" line pid comm
    if [[ ! -s "$who" ]]; then
        echo "  .who: none recorded"
    else
        while IFS= read -r line; do
            [[ -n "$line" ]] || continue
            pid="${line#waiting: }"; pid="${pid#pid=}"; pid="${pid%% *}"
            if [[ "$pid" =~ ^[0-9]+$ && -d "/proc/$pid" ]]; then
                comm="$(cat "/proc/$pid/comm" 2>/dev/null)"
                if _dbxlock_expected_comm "$comm"; then echo "  $line (alive)"
                else echo "  $line (alive, SUSPECT: pid $pid is $comm)"; fi
            else echo "  $line (STALE: pid $pid is not running)"; fi
        done < "$who"
    fi
    dbxlock_holders "$DBXLOCK_FILE"
    return 0
}

dbxlock_acquire() {
    local mode="$1" wait_s="$2" desc="$3" opt deadline rem said=0
    case "$mode" in exclusive) opt=-x ;; shared) opt=-s ;; *) return 2 ;; esac
    deadline=$(( $(date +%s) + wait_s ))
    exec 7>>"$DBXLOCK_FILE.intent" || return 1
    exec 8>>"$DBXLOCK_FILE" || { exec 7>&-; return 1; }
    [[ "$mode" == "exclusive" ]] && _dbxlock_who_edit add "waiting: pid=$$ pstart=$(_dbxlock_pstart $$) $desc mode=exclusive since=$(_dbxlock_utc)"
    # 1) intent
    if ! flock -n "$opt" 7; then
        if [[ "${DBXLOCK_QUIET:-0}" != "1" ]]; then
            echo "dosbox lock: waiting (up to ${wait_s}s) to take the $mode lock; recorded:" >&2
            dbxlock_describe >&2; said=1
        fi
        rem=$(( deadline - $(date +%s) )); (( rem < 0 )) && rem=0
        if ! flock -w "$rem" "$opt" 7; then
            exec 7>&- 8>&-; _dbxlock_who_edit dropwait; return 1
        fi
    fi
    # 2) main
    if ! flock -n "$opt" 8; then
        if [[ "${DBXLOCK_QUIET:-0}" != "1" && "$said" == "0" ]]; then
            echo "dosbox lock: waiting (up to ${wait_s}s) to take the $mode lock; recorded:" >&2
            dbxlock_describe >&2
        fi
        rem=$(( deadline - $(date +%s) )); (( rem < 0 )) && rem=0
        if ! flock -w "$rem" "$opt" 8; then
            exec 7>&- 8>&-; _dbxlock_who_edit dropwait; return 1
        fi
    fi
    # 3) release intent: held main now; see the header for why not later
    flock -u 7; exec 7>&-
    [[ "$mode" == "exclusive" ]] && _dbxlock_who_edit dropwait
    _dbxlock_who_edit add "pid=$$ pstart=$(_dbxlock_pstart $$) $desc mode=$mode since=$(_dbxlock_utc)"
    return 0
}

dbxlock_release() {
    _dbxlock_who_edit drop
    exec 8>&-
    return 0
}

# ---------------------------------------------------------------------------
# DISPLAY ALLOCATION. The host lock says WHEN DOSBox-X may run; it says
# nothing about WHERE its window goes. Two cells that hold the lock SHARED at
# the same time on one Xvfb display land in each other's root-window captures
# (dosags voided two pairs on 2026-09-24: lab-journeys on :231, the main
# worker's AQ gate against its own Shards sweep on :211). So every display a
# cell draws on is held by a per-display flock for the cell's life:
#   /tmp/dos-port-display.<N>.lock
#
#   dbxlock_display_acquire [N [WAIT_S]]
#       With N: take display N's flock, waiting up to WAIT_S seconds
#       (default DOSBOX_LOCK_WAIT, else 3600) -- a pinned display, so two
#       pinned cells on one display run one after the other, never together.
#       Without N: take the first free display in DOSBOX_DISPLAY_RANGE
#       (default 300-399), skipping any display whose /tmp/.X<N>-lock names
#       a live X server we do not hold (one started outside this protocol),
#       and retrying until WAIT_S if all are busy.
#       On success sets DBXLOCK_DISPLAY=<N>, prints <N>, holds the flock on
#       fd 5 (inherited by DOSBox-X / Xvfb children, so it lives exactly as
#       long as they do); returns 1 on timeout.
#       CALL IT DIRECTLY, never inside $(...): a command substitution runs in
#       a subshell, whose fd -- and flock -- dies with it. Use the variable.
#   dbxlock_display_release          drop it (process exit does the same).
# Stale-safe: a flock belongs to the open file, so a holder that dies --
# however it dies -- releases its display; nothing to clean up.
# ---------------------------------------------------------------------------
DBXLOCK_DISPLAY=""

_dbxlock_x_alive() {    # $1 = display number: a live X server we did not start?
    local lk="/tmp/.X$1-lock" pid
    [[ -f "$lk" ]] || return 1
    pid="$(tr -d ' \n' < "$lk" 2>/dev/null)"
    [[ "$pid" =~ ^[0-9]+$ && -d "/proc/$pid" ]]
}

dbxlock_display_acquire() {
    local want="${1:-}" wait_s="${2:-${DOSBOX_LOCK_WAIT:-3600}}" range lo hi n deadline
    if [[ -n "$want" ]]; then
        [[ "$want" =~ ^[0-9]+$ ]] || return 2
        exec 5>>"/tmp/dos-port-display.$want.lock" || return 1
        if ! flock -n -x 5; then
            [[ "${DBXLOCK_QUIET:-0}" == "1" ]] || \
                echo "dosbox display: :$want is in use by another cell; waiting (up to ${wait_s}s)" >&2
            if ! flock -w "$wait_s" -x 5; then exec 5>&-; return 1; fi
        fi
        DBXLOCK_DISPLAY="$want"; echo "$want"; return 0
    fi
    range="${DOSBOX_DISPLAY_RANGE:-300-399}"
    lo="${range%-*}"; hi="${range#*-}"
    [[ "$lo" =~ ^[0-9]+$ && "$hi" =~ ^[0-9]+$ && "$lo" -le "$hi" ]] || return 2
    deadline=$(( $(date +%s) + wait_s ))
    while :; do
        for (( n = lo; n <= hi; n++ )); do
            exec 5>>"/tmp/dos-port-display.$n.lock" || continue
            if flock -n -x 5; then
                if _dbxlock_x_alive "$n"; then flock -u 5; exec 5>&-; continue; fi
                DBXLOCK_DISPLAY="$n"; echo "$n"; return 0
            fi
            exec 5>&-
        done
        (( $(date +%s) >= deadline )) && return 1
        sleep 1
    done
}

dbxlock_display_release() {
    if [[ -n "$DBXLOCK_DISPLAY" ]]; then flock -u 5 2>/dev/null; exec 5>&-; DBXLOCK_DISPLAY=""; fi
    return 0
}

# dbxlock_display_num DISPLAY-STRING -> the number (":231", ":231.0",
# "host:231" -> 231); empty when it has none.
dbxlock_display_num() {
    local d="${1##*:}"; d="${d%%.*}"
    [[ "$d" =~ ^[0-9]+$ ]] && echo "$d"
    return 0
}
