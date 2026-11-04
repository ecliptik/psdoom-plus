#!/usr/bin/env bash
# ratchet.sh -- run a port's deterministic DOSBox-X checks against recorded
# limits; any regression fails. Hub-owned, generic for every port (same
# ownership as dosbox-run.sh).
#
# A port lists its work in tests/ratchet.yaml: RUNS (named commands, each
# executed once per invocation -- a 5 Days or --sb-live cell costs minutes)
# and CHECKS (one number each, read from a run's result or from the check's
# own command, compared with a limit: eq / le / ge). A value worse than its
# limit is a FAIL. A value better than its limit is IMPROVED, and with
# --propose-lower the tool writes <file>.proposed with the tightened limits
# next to the original -- it never edits or commits the real file; lowering
# a limit is a commit someone reviews. With --controls, each check's control
# (a command that breaks what the check guards) is run too, and must make
# the check fail -- proof the check still has power.
#
# FILE FORMAT -- a strict, flat subset of YAML (valid YAML, so people and any
# YAML tool can read it; parsed here with awk so the tool needs nothing beyond
# bash + coreutils + util-linux):
#
#   version: 1
#   build: make probes              # optional: run once, before any run
#   build_timeout: 1800             # optional (default 1800)
#   runs:                           # optional: shared, run-once commands
#     - name: sbring                # [A-Za-z0-9_.-], unique among runs
#       kind: sb-live               # plain | sb-live | capture   (default plain)
#       command: tools/dosbox-run.sh --sb-live --fast --exe build/SBRING.EXE --stdout "$RATCHET_OUT/stdout.txt"
#       timeout: 300                # seconds (default 600)
#       display: 251                # capture runs only: Xvfb display number
#       lock: exclusive             # exclusive (default) | shared -- see LOCK MODE
#   checks:
#     - name: sb-overwrites         # [A-Za-z0-9_.-], unique among checks
#       run: sbring                 # EITHER a run's name ...
#       # command: ...              # ... OR the check's own command (then
#       #                           #     kind/timeout/display as for a run)
#       result: $RATCHET_OUT/stdout.txt  # file to read; "-" = the command's own
#                                   #     stdout+stderr (default)
#       match: 'storm\+drain:'      # optional ERE: only lines matching it count
#       key: overwrites             # the number after "overwrites=" ...
#       # regex: 'peak=([0-9]+)/'   # ... OR an ERE whose first group is it
#       pick: last                  # first | last matching line (default last)
#       compare: le                 # eq | le | ge
#       limit: 0
#       quarantine: false           # true = reported, never gating
#       control: tools/dosbox-run.sh --sb-live --fast --exe build/SBRBREAK.EXE --stdout "$RATCHET_OUT/stdout.txt"
#       control_expect: fail        # the only value: the control must FAIL the check
#       note: free text
#
# Rules: one "key: value" per line; an item starts with "  - name:", its other
# keys are indented four spaces; no nesting, anchors, flow style or
# multi-line values. Values may be bare, 'single-quoted' ('' for a literal
# quote) or "double-quoted" (no escapes); a " # comment" may follow any of
# them. A bare value loses a trailing " # comment", so quote any value that
# contains " #". Unknown keys, duplicate
# keys and duplicate names are errors (a typo must not silently disable a
# check). A check has exactly one of run: or command:; a check with run:
# takes kind/timeout/display/lock from its run and may not set them itself.
# A check's control runs with the check's lock mode.
#
# LOCK MODE (runs and own-command checks): exclusive by default. "lock:
# shared" is for units at FIXED DOSBox-X cycles only, whose input comes from
# inside the guest or that have none (tooling.md): such a unit
# takes the host lock SHARED, may overlap other shared runs, and skips the
# ps quiet-wait (waiting for no dosbox-x anywhere would serialise it anyway).
# Checked when the file is read (exit 3 on a contradiction):
#   --fast in the command -> error (dosbox-x-fast.conf is cycles=max);
#   xdotool in the command -> error (host-injected input: see tooling.md);
#   --conf/-conf PATH     -> PATH's last "cycles =" must be "fixed N" or a
#                            number, else error ("verified" when it is);
#                            a conf not found yet -> "unverified";
#   no conf in the command (a harness that picks its own) -> "asserted by
#   the author": the file's author vouches for fixed cycles.
# The verdict is shown in --list and in the report's RUN table.
# $RATCHET_OUT / ${RATCHET_OUT} in result: is the only expansion ratchet.sh
# does; it means the out dir of whatever produced the result (the run, the
# check's own command, or the control).
#
# A MISSING FIELD IS A FAILURE: a check whose result file is missing, was not
# written by this invocation, or has no line with the number is ERROR, and
# ERROR gates exactly like FAIL.
#
# EXECUTION: build (if any) once, with its own timeout, no lock; then the
# checks in file order. A check with run: triggers that run the first time
# any selected check needs it; later checks reuse its result. Every run, check
# command and control is a UNIT, executed ONE AT A TIME with:
#   - the host-wide DOSBox-X lock (EXCLUSIVE, or SHARED for a "lock: shared"
#     unit): flock on /tmp/dos-port-dosbox.lock
#     (--dosbox-lock; DOSBOX_LOCK_FILE) held for the whole unit, and
#     DOSBOX_LOCK_HELD=1 in the unit's environment so the dosbox-run.sh calls
#     inside it do not take the lock again (that would deadlock). dosbox-run.sh
#     takes the same lock (shared for plain runs, exclusive for --sb-live and
#     --interactive), so lock-honouring runs never overlap a unit. Taken
#     through the intent lock (dosbox-lock-lib.sh), so a stream of SHARED
#     runs cannot starve the unit; holders and waiting writers are listed in
#     <lock>.who.
#   - then, for EXCLUSIVE units, as a FALLBACK for launchers that do not
#     take the lock yet: wait
#     until no dosbox-x has run anywhere on the host for --quiet-for seconds in
#     a row (default 15); a capture watcher that finds its stage as "the"
#     dosbox-x in ps can be handed the wrong screen by any concurrent
#     instance. --no-host-wait drops this once every launcher honours the lock.
#     Lock wait + quiet-wait give up after --busy-wait seconds (default 900):
#     the unit is BLOCKED.
#   - kind -> display: plain and sb-live units get DOSBOX_DISPLAY and
#     DOSBOX_DISPLAY_NUM unset (sb-live: dosbox-run.sh uses its own private
#     `xvfb-run -a`); capture units get DOSBOX_DISPLAY_NUM=N and
#     DOSBOX_DISPLAY=:N (N = display, else --capture-display, default 251),
#     and an Xvfb is started on :N for the unit if no X server answers there.
#   - its own session (setsid) and its own watchdog: the timeout starts after
#     the waits; on expiry the whole session (DOSBox-X, xvfb-run, Xvfb, ...)
#     gets TERM, then KILL after 10 s. SIGINT/SIGTERM to ratchet.sh does the
#     same to the running unit and still writes the report.
#   - environment: RATCHET_OUT (the unit's own out dir), RATCHET_UNIT,
#     RATCHET_ROOT, DOSBOX_LOCK_HELD=1, DOSBOX_LOCK_FILE.
# A second ratchet.sh on the host exits 3 (flock on --lock, default
# /tmp/sdl-dos-ports-ratchet.lock; --lock-wait to wait instead).
#
# STATUS per check: PASS, IMPROVED (passes, strictly better than its limit),
# FAIL (breaks its limit), ERROR (no number: see above), TIMEOUT, BLOCKED
# (the lock or the host never came free; says nothing about the port),
# INTERRUPTED (SIGINT/SIGTERM). A quarantined check shows "(q)", never gates.
# CONTROL result per check (--controls only): OK (the control made the check
# FAIL, as required), NOPOWER (the control PASSED the check: the check cannot
# see the breakage), NONUMBER (the control produced no number: a broken
# control proves nothing), TIMEOUT, BLOCKED, INTERRUPTED. NOPOWER, NONUMBER
# and TIMEOUT count as failures of the check.
#
# EXIT CODES
#   0  every non-quarantined check PASS/IMPROVED (and every control OK)
#   1  a non-quarantined check FAIL/ERROR/TIMEOUT, a control NOPOWER/NONUMBER/
#      TIMEOUT, or the build failed
#   2  nothing failed, but something was BLOCKED or INTERRUPTED
#   3  ratchet.sh itself could not run (usage, config error, lock held)
# ERROR and TIMEOUT count as failures on purpose: a check that can no longer
# produce its number (the port hangs, its output format changed) has not
# shown it is within its limit, and a hang IS a regression.
#
# HISTORY and quarantine: every invocation appends one line per check to the
# state file (--state, default <root>/build/ratchet/state.tsv):
#   <utc-time> <run-id> <name> <status> <value>      (tab-separated)
# A check with at least 2 non-passing results (FAIL/ERROR/TIMEOUT) AND at
# least 1 pass among its last 10 recorded results is flagged "SHOULD
# QUARANTINE" in the report. BLOCKED and INTERRUPTED are not counted; control
# results are not recorded. The tool never sets quarantine itself; that is an
# edit to the ratchet file someone reviews.
#
# OUTPUT (--out, default <root>/build/ratchet/<run-id>/):
#   ratchet-report.txt   the full report (first line = the summary)
#   ratchet-summary.txt  one line, also printed on stdout
#   build/, run.<name>/, check.<name>/, control.<name>/   per unit:
#                        output.txt (stdout+stderr) and whatever it writes
#                        to $RATCHET_OUT
# <root>/build/ratchet/latest is a symlink to the newest run directory.
#
# USAGE
#   shared/tools/ratchet.sh [--root DIR] [--file PATH] [--out DIR]
#       [--state FILE] [--only NAME[,NAME...]] [--propose-lower] [--controls]
#       [--capture-display N] [--busy-wait SEC] [--quiet-for SEC]
#       [--no-host-wait] [--lock FILE] [--lock-wait SEC] [--dosbox-lock FILE]
#       [--no-build] [--no-record] [--list]
#   --root DIR     the port root; commands run from here (default: $PWD)
#   --file PATH    ratchet file, relative to --root (default tests/ratchet.yaml)
#   --only LIST    only these checks (and the runs they use)
#   --controls     also run every selected check's control (on demand/weekly)
#   --no-host-wait skip the ps quiet-wait; rely on the DOSBox-X lock alone
#   --no-build     skip the build: step
#   --list         parse the file, print runs and checks, run nothing
#   --no-record    do not append to the state file (for trying a config)
#
# Needs: bash 4+, coreutils, util-linux (flock, setsid), procps (ps/pgrep),
# awk. Xvfb + xdpyinfo only for capture units. ASCII only.

set -u

ROOT="$PWD"
RFILE="tests/ratchet.yaml"
OUT=""
STATE=""
ONLY=""
PROPOSE=0
CONTROLS=0
CAPTURE_DISPLAY="${RATCHET_CAPTURE_DISPLAY:-251}"
BUSY_WAIT=900
QUIET_FOR=15
HOST_WAIT=1
LOCK="/tmp/sdl-dos-ports-ratchet.lock"
LOCK_WAIT=0
DBX_LOCK="${DOSBOX_LOCK_FILE:-/tmp/dos-port-dosbox.lock}"
DO_BUILD=1
RECORD=1
LIST=0

usage() { sed -n '/^# USAGE/,/^# Needs:/p' "$0" | sed 's/^# \{0,1\}//' | sed '$d' >&2; exit 3; }
die() { echo "ratchet.sh: $*" >&2; exit 3; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --root) ROOT="$2"; shift 2 ;;
        --file) RFILE="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --state) STATE="$2"; shift 2 ;;
        --only) ONLY="$2"; shift 2 ;;
        --propose-lower) PROPOSE=1; shift ;;
        --controls) CONTROLS=1; shift ;;
        --capture-display) CAPTURE_DISPLAY="$2"; shift 2 ;;
        --busy-wait) BUSY_WAIT="$2"; shift 2 ;;
        --quiet-for) QUIET_FOR="$2"; shift 2 ;;
        --no-host-wait) HOST_WAIT=0; shift ;;
        --lock) LOCK="$2"; shift 2 ;;
        --lock-wait) LOCK_WAIT="$2"; shift 2 ;;
        --dosbox-lock) DBX_LOCK="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --no-record) RECORD=0; shift ;;
        --list) LIST=1; shift ;;
        -h|--help) usage ;;
        *) echo "ratchet.sh: unknown argument: $1" >&2; usage ;;
    esac
done

ROOT="$(cd "$ROOT" 2>/dev/null && pwd)" || die "--root not a directory"
[[ "$RFILE" = /* ]] || RFILE="$ROOT/$RFILE"
[[ -f "$RFILE" ]] || die "ratchet file not found: $RFILE"
for _v in BUSY_WAIT QUIET_FOR LOCK_WAIT CAPTURE_DISPLAY; do
    [[ "${!_v}" =~ ^[0-9]+$ ]] || die "--${_v,,} must be a whole number"
done

# the host-wide DOSBox-X lock (intent lock for writer priority, .who); the
# real path, since a port may reach this script through a tools/ symlink
# shellcheck source=./dosbox-lock-lib.sh
source "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/dosbox-lock-lib.sh" || die "cannot load dosbox-lock-lib.sh"
DBXLOCK_FILE="$DBX_LOCK"

# ---------------------------------------------------------------- parse ---
# awk emits "SEC<TAB>IDX<TAB>KEY<TAB>VALUE" (SEC = top | run | check),
# "check<TAB>IDX<TAB>@limit_line<TAB>N" for each check's limit line, or
# "ERR<TAB>line N: message" and stops.
PARSED="$(LC_ALL=C awk '
function trim(s) { sub(/^[ \t]+/, "", s); sub(/[ \t]+$/, "", s); return s }
function tail_ok(t) {                       # after a closing quote: nothing, or a comment
    return (t ~ /^[ \t]*$/ || t ~ /^[ \t]+#/)
}
function val(s,   j, c, out) {
    s = trim(s)
    if (s ~ /^\x27/) {                       # single-quoted, '' = a literal quote
        out = ""
        for (j = 2; j <= length(s); j++) {
            c = substr(s, j, 1)
            if (c == "\x27") {
                if (substr(s, j + 1, 1) == "\x27") { out = out c; j++; continue }
                return tail_ok(substr(s, j + 1)) ? out : "\001bad"
            }
            out = out c
        }
        return "\001bad"
    }
    if (s ~ /^"/) {                          # double-quoted, no escapes
        j = index(substr(s, 2), "\"")
        if (j == 0) return "\001bad"
        return tail_ok(substr(s, j + 2)) ? substr(s, 2, j - 1) : "\001bad"
    }
    sub(/[ \t]+#.*$/, "", s)
    return trim(s)
}
function err(m) { print "ERR\tline " NR ": " m; exit }
BEGIN {
    sec = ""; ri = -1; ci = -1
    split("version build build_timeout", a, " "); for (j in a) okt[a[j]] = 1
    split("name kind command timeout display lock note", a, " "); for (j in a) okr[a[j]] = 1
    split("name run command kind timeout display lock result match key regex pick compare limit quarantine control control_expect note", a, " ")
    for (j in a) okc[a[j]] = 1
}
{
    line = $0; sub(/\r$/, "", line)
    if (line ~ /[^\x00-\x7f]/) err("non-ASCII character")
    if (line ~ /^[ \t]*$/ || line ~ /^[ \t]*#/) next
    if (line ~ /^runs:[ \t]*$/)   { if (seen_sec["runs"]++) err("second runs: section");   sec = "run"; next }
    if (line ~ /^checks:[ \t]*$/) { if (seen_sec["checks"]++) err("second checks: section"); sec = "check"; next }
    if (line ~ /^[a-z_]+:/) {                                   # top-level scalar
        k = line; sub(/:.*/, "", k); rest = line; sub(/^[a-z_]+:/, "", rest)
        if (!(k in okt)) err("unknown top-level key \"" k "\"")
        if (k in seent) err("duplicate top-level key \"" k "\"")
        seent[k] = 1; v = val(rest); if (v == "\001bad") err("unterminated quote")
        if (k == "version" && v != "1") err("unsupported version " v)
        print "top\t0\t" k "\t" v; next
    }
    if (sec == "") err("expected version:, build:, runs: or checks:")
    if (line ~ /^  - [a-z_]+:/) { if (sec == "run") ri++; else ci++; line = "    " substr(line, 5) }
    if (line !~ /^    [a-z_]+:/) err("not a \"key: value\" line of an item (indent 4, or \"  - \" to start one)")
    idx = (sec == "run") ? ri : ci
    if (idx < 0) err("setting before the first \"  - name:\"")
    k = line; sub(/^    /, "", k); sub(/:.*/, "", k)
    rest = line; sub(/^    [a-z_]+:/, "", rest)
    if (sec == "run" && !(k in okr)) err("unknown key \"" k "\" in a run")
    if (sec == "check" && !(k in okc)) err("unknown key \"" k "\" in a check")
    if ((sec SUBSEP idx SUBSEP k) in seen) err("duplicate key \"" k "\" in one item")
    seen[sec SUBSEP idx SUBSEP k] = 1
    v = val(rest); if (v == "\001bad") err("unterminated quote")
    print sec "\t" idx "\t" k "\t" v
    if (sec == "check" && k == "limit") print "check\t" idx "\t@limit_line\t" NR
}
' "$RFILE")"

declare -A T=() R=() C=() RUNIDX=()
NRUN=0; NCHECK=0
while IFS=$'\t' read -r sec i k v; do
    [[ -z "$sec" ]] && continue
    case "$sec" in
        ERR) die "$RFILE: $i" ;;
        top) T[$k]="$v" ;;
        run) R["$i.$k"]="$v"; (( i + 1 > NRUN )) && NRUN=$(( i + 1 )) ;;
        check) C["$i.$k"]="$v"; (( i + 1 > NCHECK )) && NCHECK=$(( i + 1 )) ;;
    esac
done <<< "$PARSED"
(( NCHECK > 0 )) || die "$RFILE: no checks"
T[build_timeout]="${T[build_timeout]:-1800}"
[[ "${T[build_timeout]}" =~ ^[0-9]+$ && "${T[build_timeout]}" -gt 0 ]] || die "build_timeout must be whole seconds > 0"

is_num() { [[ "$1" =~ ^-?[0-9]+(\.[0-9]+)?$ ]]; }
valid_kind_timeout_display() {   # $1 label, $2 kind, $3 timeout, $4 display
    [[ "$2" =~ ^(plain|sb-live|capture)$ ]] || die "$1: kind must be plain, sb-live or capture"
    [[ "$3" =~ ^[0-9]+$ && "$3" -gt 0 ]] || die "$1: timeout must be whole seconds > 0"
    [[ -z "$4" || "$4" =~ ^[0-9]+$ ]] || die "$1: display must be a number"
}
# check_shared LABEL MODE COMMAND -> sets LOCKCHECK to how a SHARED unit's fixed cycles
# were established; dies on a contradiction. The fixed-cycles rule
# (tooling.md): only a run at FIXED cycles is unaffected by other DOSBox-X
# instances on the host, so only such a run may share the lock.
#   --fast in the command        -> error (dosbox-x-fast.conf is cycles=max)
#   --conf/-conf PATH, readable  -> its last "cycles =" line must be
#                                   "fixed N" or a plain number; else error
#   --conf PATH not readable yet -> "unverified (conf not found: PATH)"
#   no conf on the command line  -> "asserted by the author (no conf to check)"
check_shared() {
    local label="$1" mode="$2" cmd="$3" conf cyc
    [[ "$mode" =~ ^(shared|exclusive)$ ]] || die "$label: lock must be shared or exclusive"
    [[ "$mode" == "shared" ]] || { LOCKCHECK="exclusive"; return 0; }
    [[ " $cmd " =~ [[:space:]]--fast[[:space:]] ]] && die "$label: lock: shared, but the command uses --fast (cycles=max); shared is for FIXED-cycles runs only"
    [[ "$cmd" == *xdotool* ]] && die "$label: lock: shared, but the command drives input with xdotool; host-injected input lands on a different emulated tick under load, so it takes EXCLUSIVE"
    conf=""
    if [[ " $cmd " =~ [[:space:]]-?-conf[[:space:]=]+([^[:space:]]+) ]]; then conf="${BASH_REMATCH[1]}"; conf="${conf//\"/}"; conf="${conf//\'/}"; fi
    if [[ -z "$conf" ]]; then LOCKCHECK="shared, asserted by the author (no conf on the command line to check)"; return 0; fi
    [[ "$conf" = /* ]] || conf="$ROOT/$conf"
    if [[ ! -r "$conf" ]]; then LOCKCHECK="shared, unverified (conf not found: $conf)"; return 0; fi
    cyc="$(LC_ALL=C grep -iE '^[[:space:]]*cycles[[:space:]]*=' "$conf" | tail -n 1 | sed -E 's/^[^=]*=[[:space:]]*//; s/[[:space:]]*(#.*)?$//')"
    if [[ "$cyc" =~ ^([Ff][Ii][Xx][Ee][Dd][[:space:]]+)?[0-9]+$ ]]; then
        LOCKCHECK="shared, verified (cycles=$cyc in $conf)"; return 0
    fi
    die "$label: lock: shared, but $conf sets cycles=${cyc:-<unset>}, not fixed"
}

for (( r = 0; r < NRUN; r++ )); do
    n="${R[$r.name]:-}"
    [[ -n "$n" ]] || die "run #$((r + 1)): missing name"
    [[ "$n" =~ ^[A-Za-z0-9_.-]+$ ]] || die "run $n: name must match [A-Za-z0-9_.-]+"
    [[ -z "${RUNIDX[$n]:-}" ]] || die "run $n: duplicate name"
    RUNIDX[$n]="$r"
    [[ -n "${R[$r.command]:-}" ]] || die "run $n: missing command"
    R[$r.kind]="${R[$r.kind]:-plain}"; R[$r.timeout]="${R[$r.timeout]:-600}"; R[$r.lock]="${R[$r.lock]:-exclusive}"
    valid_kind_timeout_display "run $n" "${R[$r.kind]}" "${R[$r.timeout]}" "${R[$r.display]:-}"
    check_shared "run $n" "${R[$r.lock]}" "${R[$r.command]}"; R[$r.lockcheck]="$LOCKCHECK"
done
declare -A NAMES=()
for (( i = 0; i < NCHECK; i++ )); do
    n="${C[$i.name]:-}"
    [[ -n "$n" ]] || die "check #$((i + 1)): missing name"
    [[ "$n" =~ ^[A-Za-z0-9_.-]+$ ]] || die "check $n: name must match [A-Za-z0-9_.-]+"
    [[ -z "${NAMES[$n]:-}" ]] || die "check $n: duplicate name"
    NAMES[$n]=1
    if [[ -n "${C[$i.run]:-}" && -n "${C[$i.command]:-}" ]] || [[ -z "${C[$i.run]:-}" && -z "${C[$i.command]:-}" ]]; then
        die "check $n: give exactly one of run or command"
    fi
    if [[ -n "${C[$i.run]:-}" ]]; then
        [[ -n "${RUNIDX[${C[$i.run]}]:-}" ]] || die "check $n: no run named ${C[$i.run]}"
        for kk in kind timeout display lock; do
            [[ -z "${C[$i.$kk]:-}" ]] || die "check $n: $kk comes from run ${C[$i.run]}; do not set it on the check"
        done
        r="${RUNIDX[${C[$i.run]}]}"
        C[$i.kind]="${R[$r.kind]}"; C[$i.timeout]="${R[$r.timeout]}"; C[$i.display]="${R[$r.display]:-}"
        C[$i.lock]="${R[$r.lock]}"
    else
        C[$i.kind]="${C[$i.kind]:-plain}"; C[$i.timeout]="${C[$i.timeout]:-600}"; C[$i.lock]="${C[$i.lock]:-exclusive}"
        valid_kind_timeout_display "check $n" "${C[$i.kind]}" "${C[$i.timeout]}" "${C[$i.display]:-}"
        check_shared "check $n" "${C[$i.lock]}" "${C[$i.command]}"; C[$i.lockcheck]="$LOCKCHECK"
    fi
    C[$i.result]="${C[$i.result]:--}"
    if [[ -n "${C[$i.key]:-}" && -n "${C[$i.regex]:-}" ]] || [[ -z "${C[$i.key]:-}" && -z "${C[$i.regex]:-}" ]]; then
        die "check $n: give exactly one of key or regex"
    fi
    [[ -z "${C[$i.key]:-}" || "${C[$i.key]}" =~ ^[A-Za-z0-9_.+-]+$ ]] || die "check $n: key must be a plain name"
    C[$i.pick]="${C[$i.pick]:-last}"
    [[ "${C[$i.pick]}" =~ ^(first|last)$ ]] || die "check $n: pick must be first or last"
    [[ "${C[$i.compare]:-}" =~ ^(eq|le|ge)$ ]] || die "check $n: compare must be eq, le or ge"
    is_num "${C[$i.limit]:-}" || die "check $n: limit must be a number"
    C[$i.quarantine]="${C[$i.quarantine]:-false}"
    [[ "${C[$i.quarantine]}" =~ ^(true|false)$ ]] || die "check $n: quarantine must be true or false"
    if [[ -n "${C[$i.control_expect]:-}" && -z "${C[$i.control]:-}" ]]; then
        die "check $n: control_expect without control"
    fi
    if [[ -n "${C[$i.control]:-}" ]]; then
        check_shared "check $n control" "${C[$i.lock]}" "${C[$i.control]}"; C[$i.ctllockcheck]="$LOCKCHECK"
        C[$i.control_expect]="${C[$i.control_expect]:-fail}"
        [[ "${C[$i.control_expect]}" == "fail" ]] || die "check $n: control_expect must be fail"
    fi
    # probe the ERE syntax now, not at 3 a.m.
    for re in "${C[$i.match]:-}" "${C[$i.regex]:-}"; do
        [[ -z "$re" ]] && continue
        [[ "x" =~ $re ]]; (( $? == 2 )) && die "check $n: invalid regular expression: $re"
    done
done
if [[ -n "$ONLY" ]]; then
    IFS=',' read -r -a _only <<< "$ONLY"
    for o in "${_only[@]}"; do [[ -n "${NAMES[$o]:-}" ]] || die "--only: no check named $o"; done
fi
selected() {
    [[ -z "$ONLY" ]] && return 0
    local o; for o in "${_only[@]}"; do [[ "$o" == "$1" ]] && return 0; done; return 1
}

if (( LIST )); then
    [[ -n "${T[build]:-}" ]] && echo "build: ${T[build]}  (timeout ${T[build_timeout]}s)"
    for (( r = 0; r < NRUN; r++ )); do
        printf 'run   %-30s %-8s timeout=%ss  lock=%s\n' "${R[$r.name]}" "${R[$r.kind]}" "${R[$r.timeout]}" "${R[$r.lockcheck]}"
    done
    for (( i = 0; i < NCHECK; i++ )); do
        src="${C[$i.run]:+run=${C[$i.run]}}"; [[ -z "$src" ]] && src="own-command"
        printf 'check %-30s %-8s %-18s %s %s %s%s%s\n' "${C[$i.name]}" "${C[$i.kind]}" "$src" \
            "${C[$i.key]:+key=${C[$i.key]}}${C[$i.regex]:+regex}" "${C[$i.compare]}" "${C[$i.limit]}" \
            "$([[ ${C[$i.quarantine]} == true ]] && echo ' (quarantined)')" \
            "$([[ -n ${C[$i.control]:-} ]] && echo ' +control')"
    done
    exit 0
fi

# ----------------------------------------------------------------- lock ---
exec 9>>"$LOCK" || die "cannot open lock file $LOCK"
if ! flock -w "$LOCK_WAIT" 9; then
    die "another ratchet run holds $LOCK"
fi

RUN_ID="$(date -u +%Y%m%dT%H%M%SZ)"
[[ -n "$OUT" ]] || OUT="$ROOT/build/ratchet/$RUN_ID"
[[ -n "$STATE" ]] || STATE="$ROOT/build/ratchet/state.tsv"
mkdir -p "$OUT" "$(dirname "$STATE")" || die "cannot create $OUT"
OUT="$(cd "$OUT" && pwd)"
if [[ "$OUT" == "$ROOT/build/ratchet/"* ]]; then
    ln -sfn "$OUT" "$ROOT/build/ratchet/latest" 2>/dev/null || true
fi

# ------------------------------------------------------------- helpers ---
CUR_SID=""
XVFB_PID=""
kill_session() {   # $1 = session id: TERM, wait, KILL everything in it
    local sid="$1" pids
    pids="$(ps -eo pid=,sid= | awk -v s="$sid" '$2 == s { print $1 }')"
    [[ -z "$pids" ]] && return 0
    kill -TERM $pids 2>/dev/null
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        sleep 1
        pids="$(ps -eo pid=,sid= | awk -v s="$sid" '$2 == s { print $1 }')"
        [[ -z "$pids" ]] && return 0
    done
    kill -KILL $pids 2>/dev/null
    sleep 1
}
stop_xvfb() {
    if [[ -n "$XVFB_PID" ]]; then kill "$XVFB_PID" 2>/dev/null; wait "$XVFB_PID" 2>/dev/null; XVFB_PID=""; fi
}
INTERRUPTED=0
on_signal() {
    INTERRUPTED=1
    [[ -n "$CUR_SID" ]] && kill_session "$CUR_SID"
    stop_xvfb
}
trap on_signal INT TERM

WAIT_DEADLINE=0
host_quiet() {     # 0 = no dosbox-x on the host for QUIET_FOR s in a row
    (( HOST_WAIT )) || return 0
    local calm=0
    while :; do
        (( INTERRUPTED )) && return 1
        if pgrep -x dosbox-x >/dev/null 2>&1; then
            calm=0
            (( $(date +%s) >= WAIT_DEADLINE )) && return 1
        else
            (( calm >= QUIET_FOR )) && return 0
            calm=$(( calm + 1 ))
        fi
        sleep 1
    done
}

# run_unit LABEL KIND TIMEOUT DISPLAY COMMAND OUTDIR [nolock]
#   Sets U_STATUS (OK | TIMEOUT | BLOCKED | INTERRUPTED), U_RC, U_T0 (start of
#   the command, after the waits), U_WAIT, U_SECS. OUTDIR/output.txt gets the
#   command's stdout+stderr.
run_unit() {
    local label="$1" kind="$2" tmo="$3" disp="$4" cmd="$5" dir="$6" lockmode="${7:-exclusive}"
    local w0 cpid timed_out=0 dnum
    U_STATUS=""; U_RC=""; U_T0=0; U_WAIT=0; U_SECS=0
    mkdir -p "$dir"; : > "$dir/output.txt"
    (( INTERRUPTED )) && { U_STATUS="INTERRUPTED"; return; }
    w0=$(date +%s); WAIT_DEADLINE=$(( w0 + BUSY_WAIT ))
    if [[ "$lockmode" != "none" ]]; then
        # through the intent lock (writer priority): see dosbox-lock-lib.sh.
        # Holds fd 8 on success.
        if ! DBXLOCK_QUIET=1 dbxlock_acquire "$lockmode" "$BUSY_WAIT" "tool=ratchet.sh unit=$label"; then
            U_WAIT=$(( $(date +%s) - w0 ))
            if (( INTERRUPTED )); then U_STATUS="INTERRUPTED"; return; fi
            U_STATUS="BLOCKED"
            { echo "ratchet.sh: $label BLOCKED: DOSBox-X lock $DBX_LOCK busy for ${BUSY_WAIT}s; recorded:"
              dbxlock_describe; } >> "$dir/output.txt"
            return
        fi
        # the ps quiet-wait (fallback for lock-less launchers) only for
        # EXCLUSIVE units: a SHARED unit is by definition fine beside other
        # DOSBox-X instances, and waiting for none would serialise it anyway
        if [[ "$lockmode" == "exclusive" ]] && ! host_quiet; then
            dbxlock_release
            U_WAIT=$(( $(date +%s) - w0 ))
            if (( INTERRUPTED )); then U_STATUS="INTERRUPTED"; return; fi
            U_STATUS="BLOCKED"
            echo "ratchet.sh: $label BLOCKED: a dosbox-x kept running on the host (a lock-less launcher) for ${BUSY_WAIT}s" >> "$dir/output.txt"
            return
        fi
    fi
    U_WAIT=$(( $(date +%s) - w0 ))
    U_T0=$(date +%s)                  # the timeout starts here, after the waits
    local envs=(RATCHET_OUT="$dir" RATCHET_UNIT="$label" RATCHET_ROOT="$ROOT"
                DOSBOX_LOCK_HELD=1 DOSBOX_LOCK_FILE="$DBX_LOCK")
    local unset_vars=(-u DOSBOX_DISPLAY -u DOSBOX_DISPLAY_NUM)
    if [[ "$kind" == "capture" ]]; then
        dnum="${disp:-$CAPTURE_DISPLAY}"
        envs+=(DOSBOX_DISPLAY_NUM="$dnum" DOSBOX_DISPLAY=":$dnum")
        unset_vars=()
        if ! xdpyinfo -display ":$dnum" >/dev/null 2>&1; then
            # every lock fd closed: an orphaned Xvfb must never hold the host
            # lock (fd 8) or the ratchet's own run lock (fd 9)
            Xvfb ":$dnum" -screen 0 1280x1024x24 -nolisten tcp 5>&- 6>&- 7>&- 8>&- 9>&- >/dev/null 2>&1 &
            XVFB_PID=$!
            for _ in 1 2 3 4 5 6 7 8 9 10; do xdpyinfo -display ":$dnum" >/dev/null 2>&1 && break; sleep 0.5; done
        fi
    fi
    # own session, own watchdog; the unit does not inherit the lock fds
    ( cd "$ROOT" && exec env "${unset_vars[@]}" "${envs[@]}" setsid bash -c "$cmd" ) \
        >> "$dir/output.txt" 2>&1 < /dev/null 8>&- 9>&- &
    cpid=$!
    # no job control in a script, so the background subshell is not a group
    # leader and setsid does not fork: the exec'd setsid IS $cpid, and its new
    # session id is $cpid.
    CUR_SID="$cpid"
    while kill -0 "$cpid" 2>/dev/null; do
        if (( $(date +%s) >= U_T0 + tmo )); then timed_out=1; break; fi
        (( INTERRUPTED )) && break
        sleep 1
    done
    (( timed_out || INTERRUPTED )) && kill_session "$CUR_SID"
    wait "$cpid" 2>/dev/null; U_RC=$?
    # nothing from this unit's session may survive it
    [[ -n "$(ps -eo pid=,sid= | awk -v s="$CUR_SID" '$2 == s { print $1 }')" ]] && kill_session "$CUR_SID"
    CUR_SID=""
    stop_xvfb
    [[ "$lockmode" != "none" ]] && dbxlock_release   # release the DOSBox-X lock
    U_SECS=$(( $(date +%s) - U_T0 ))
    if (( timed_out )); then
        U_STATUS="TIMEOUT"; echo "ratchet.sh: $label TIMEOUT after ${tmo}s; session killed" >> "$dir/output.txt"
    elif (( INTERRUPTED )); then
        U_STATUS="INTERRUPTED"
    else
        U_STATUS="OK"
    fi
}

# resolve_result CHECK-IDX UNIT-DIR -> path of the file to read
resolve_result() {
    local f="${C[$1.result]}" dir="$2"
    if [[ "$f" == "-" ]]; then echo "$dir/output.txt"; return; fi
    f="${f//\$\{RATCHET_OUT\}/$dir}"; f="${f//\$RATCHET_OUT/$dir}"
    [[ "$f" = /* ]] || f="$ROOT/$f"
    echo "$f"
}

extract() {        # $1 = check index, $2 = file -> prints number or nothing
    local i="$1" f="$2" line v="" re m="${C[$1.match]:-}" key="${C[$1.key]:-}" rx="${C[$1.regex]:-}"
    [[ -f "$f" ]] || return 0
    local keyre="(^|[^A-Za-z0-9_])${key//./\\.}=(-?[0-9]+(\.[0-9]+)?)"
    while IFS= read -r line || [[ -n "$line" ]]; do
        line="${line%$'\r'}"
        if [[ -n "$m" ]] && ! [[ "$line" =~ $m ]]; then continue; fi
        if [[ -n "$key" ]]; then
            [[ "$line" =~ $keyre ]] || continue
            re="$line"; local got=""
            while [[ "$re" =~ $keyre ]]; do got="${BASH_REMATCH[2]}"; re="${re#*"${BASH_REMATCH[0]}"}"; done
            v="$got"                   # last key=value on the line wins
        else
            [[ "$line" =~ $rx ]] || continue
            v="${BASH_REMATCH[1]:-}"
            is_num "$v" || { v=""; continue; }
        fi
        [[ "${C[$i.pick]}" == "first" && -n "$v" ]] && break
    done < "$f"
    printf '%s' "$v"
}

compare_ok() {     # $1 value $2 op $3 limit
    awk -v a="$1" -v op="$2" -v b="$3" 'BEGIN {
        if (op == "eq") exit !(a + 0 == b + 0)
        if (op == "le") exit !(a + 0 <= b + 0)
        exit !(a + 0 >= b + 0) }'
}
better() {         # value strictly better than limit (le: lower, ge: higher)
    awk -v a="$1" -v op="$2" -v b="$3" 'BEGIN {
        if (op == "le") exit !(a + 0 < b + 0)
        if (op == "ge") exit !(a + 0 > b + 0)
        exit 1 }'
}

# judge CHECK-IDX UNIT-DIR UNIT-T0 LOGFILE -> J_STATUS (PASS IMPROVED FAIL ERROR), J_VAL
judge() {
    local i="$1" dir="$2" t0="$3" log="$4" f v
    f="$(resolve_result "$i" "$dir")"
    if [[ -f "$f" ]] && (( $(stat -c %Y "$f") < t0 )); then
        v=""; echo "ratchet.sh: ${C[$i.name]}: $f is older than this invocation's unit (not written by it)" >> "$log"
    else
        v="$(extract "$i" "$f")"
    fi
    J_VAL="$v"
    if [[ -z "$v" ]]; then
        J_STATUS="ERROR"
        echo "ratchet.sh: ${C[$i.name]} ERROR: no number (missing field) in $f" >> "$log"
    elif compare_ok "$v" "${C[$i.compare]}" "${C[$i.limit]}"; then
        if better "$v" "${C[$i.compare]}" "${C[$i.limit]}"; then J_STATUS="IMPROVED"; else J_STATUS="PASS"; fi
    else
        J_STATUS="FAIL"
    fi
}

# ---------------------------------------------------------------- build ---
BUILD_STATUS="none"; BUILD_SECS=0
if [[ -n "${T[build]:-}" ]] && (( DO_BUILD )); then
    run_unit "build" plain "${T[build_timeout]}" "" "${T[build]}" "$OUT/build" none
    if [[ "$U_STATUS" == "OK" && "$U_RC" == "0" ]]; then BUILD_STATUS="OK"
    elif [[ "$U_STATUS" == "OK" ]]; then BUILD_STATUS="FAILED(rc=$U_RC)"
    else BUILD_STATUS="$U_STATUS"; fi
    BUILD_SECS="$U_SECS"
fi

# ----------------------------------------------------------------- run ---
declare -A ST=() VAL=() SECS=() WAITS=() LOGF=() SRC=() RS=() RRC=() RT0=() RWAIT=() RSECS=() CT=() CVAL=()
for (( i = 0; i < NCHECK; i++ )); do
    n="${C[$i.name]}"
    selected "$n" || continue
    if [[ "$BUILD_STATUS" != "none" && "$BUILD_STATUS" != "OK" ]]; then
        ST[$i]="ERROR"; VAL[$i]=""; SECS[$i]=0; WAITS[$i]=0; SRC[$i]="build"; LOGF[$i]="$OUT/build/output.txt"
        continue
    fi
    if [[ -n "${C[$i.run]:-}" ]]; then
        r="${RUNIDX[${C[$i.run]}]}"; rn="${C[$i.run]}"
        if [[ -z "${RS[$r]:-}" ]]; then
            run_unit "run.$rn" "${R[$r.kind]}" "${R[$r.timeout]}" "${R[$r.display]:-}" "${R[$r.command]}" "$OUT/run.$rn" "${R[$r.lock]}"
            RS[$r]="$U_STATUS"; RRC[$r]="$U_RC"; RT0[$r]="$U_T0"; RWAIT[$r]="$U_WAIT"; RSECS[$r]="$U_SECS"
            WAITS[$i]="$U_WAIT"; SECS[$i]="$U_SECS"
        else
            WAITS[$i]=0; SECS[$i]=0     # shared: its time is on the first user
        fi
        dir="$OUT/run.$rn"; status="${RS[$r]}"; t0="${RT0[$r]}"; SRC[$i]="run.$rn"
    else
        run_unit "check.$n" "${C[$i.kind]}" "${C[$i.timeout]}" "${C[$i.display]:-}" "${C[$i.command]}" "$OUT/check.$n" "${C[$i.lock]}"
        dir="$OUT/check.$n"; status="$U_STATUS"; t0="$U_T0"; SRC[$i]="check.$n"
        WAITS[$i]="$U_WAIT"; SECS[$i]="$U_SECS"
    fi
    LOGF[$i]="$dir/output.txt"
    if [[ "$status" != "OK" ]]; then ST[$i]="$status"; VAL[$i]=""; continue; fi
    judge "$i" "$dir" "$t0" "${LOGF[$i]}"
    ST[$i]="$J_STATUS"; VAL[$i]="$J_VAL"
done

# ------------------------------------------------------------- controls ---
if (( CONTROLS )); then
    for (( i = 0; i < NCHECK; i++ )); do
        n="${C[$i.name]}"
        selected "$n" || continue
        [[ -n "${C[$i.control]:-}" ]] || continue
        run_unit "control.$n" "${C[$i.kind]}" "${C[$i.timeout]}" "${C[$i.display]:-}" "${C[$i.control]}" "$OUT/control.$n" "${C[$i.lock]}"
        if [[ "$U_STATUS" != "OK" ]]; then CT[$i]="$U_STATUS"; CVAL[$i]=""; continue; fi
        judge "$i" "$OUT/control.$n" "$U_T0" "$OUT/control.$n/output.txt"
        CVAL[$i]="$J_VAL"
        case "$J_STATUS" in
            FAIL) CT[$i]="OK" ;;          # the check caught the breakage
            ERROR) CT[$i]="NONUMBER" ;;   # a broken control proves nothing
            *) CT[$i]="NOPOWER" ;;        # the check passed a broken build
        esac
    done
fi
trap - INT TERM

# ------------------------------------------------------------- history ---
NOW="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
if (( RECORD )); then
    for (( i = 0; i < NCHECK; i++ )); do
        [[ -n "${ST[$i]:-}" ]] || continue
        printf '%s\t%s\t%s\t%s\t%s\n' "$NOW" "$RUN_ID" "${C[$i.name]}" "${ST[$i]}" "${VAL[$i]:--}" >> "$STATE"
    done
fi
hist() {           # $1 name -> last 10 statuses as letters, oldest first
    [[ -f "$STATE" ]] || return 0
    awk -F'\t' -v n="$1" '$3 == n && $4 != "BLOCKED" && $4 != "INTERRUPTED" { s[++c] = $4 }
        END { for (j = (c > 10 ? c - 9 : 1); j <= c; j++) printf "%s", substr(s[j], 1, 1) }' "$STATE"
}

# -------------------------------------------------------------- report ---
REPORT="$OUT/ratchet-report.txt"
npass=0; nfail=0; nblock=0; nq=0; nimp=0; failed=(); blocked=(); flagged=(); ctlbad=()
declare -A PROP=()
{
    echo "ratchet.sh report"
    echo "run:    $RUN_ID"
    echo "root:   $ROOT"
    echo "file:   $RFILE"
    echo "host:   $(hostname)"
    echo "state:  $STATE$( (( RECORD )) || echo ' (not recorded: --no-record)')"
    echo "lock:   $DBX_LOCK, exclusive per unit$( (( HOST_WAIT )) && echo "; ps quiet-wait ${QUIET_FOR}s" || echo '; no ps quiet-wait')"
    [[ "$BUILD_STATUS" != "none" ]] && echo "build:  $BUILD_STATUS (${BUILD_SECS}s): ${T[build]}"
    echo
    if (( ${#RS[@]} > 0 )); then
        printf '%-30s %-8s %-12s %4s %6s %6s  %s\n' RUN KIND STATUS RC WAIT SECS LOCK
        for (( r = 0; r < NRUN; r++ )); do
            [[ -n "${RS[$r]:-}" ]] || continue
            printf '%-30s %-8s %-12s %4s %6s %6s  %s\n' "${R[$r.name]}" "${R[$r.kind]}" "${RS[$r]}" "${RRC[$r]:--}" "${RWAIT[$r]}" "${RSECS[$r]}" "${R[$r.lockcheck]}"
        done
        echo
    fi
    printf '%-34s %-8s %-12s %-12s %-5s %-10s %-11s %6s %6s  %s\n' CHECK KIND STATUS VALUE CMP LIMIT HISTORY WAIT SECS NOTE
    for (( i = 0; i < NCHECK; i++ )); do
        [[ -n "${ST[$i]:-}" ]] || continue
        n="${C[$i.name]}"; s="${ST[$i]}"; q="${C[$i.quarantine]}"
        h="$( (( RECORD )) && hist "$n")"
        nonpass=$(printf '%s' "$h" | tr -cd 'FET' | wc -c)
        passes=$(printf '%s' "$h" | tr -cd 'PI' | wc -c)
        flag=""
        [[ -n "${C[$i.run]:-}" ]] && flag="run=${C[$i.run]}"
        [[ -z "${C[$i.run]:-}" && "${C[$i.lock]}" == "shared" ]] && flag="lock ${C[$i.lockcheck]}"
        if [[ "$q" != "true" ]] && (( nonpass >= 2 && passes >= 1 )); then
            flag+="${flag:+ }SHOULD QUARANTINE (flaky: $nonpass non-pass in last ${#h})"; flagged+=("$n")
        fi
        shown="$s"; [[ "$q" == "true" ]] && shown="$s(q)"
        printf '%-34s %-8s %-12s %-12s %-5s %-10s %-11s %6s %6s  %s\n' "$n" "${C[$i.kind]}" "$shown" "${VAL[$i]:--}" \
            "${C[$i.compare]}" "${C[$i.limit]}" "${h:--}" "${WAITS[$i]:-0}" "${SECS[$i]:-0}" "$flag"
        if [[ "$q" == "true" ]]; then nq=$(( nq + 1 )); continue; fi
        case "$s" in
            PASS) npass=$(( npass + 1 )) ;;
            IMPROVED) npass=$(( npass + 1 )); nimp=$(( nimp + 1 )); PROP[$i]="${VAL[$i]}" ;;
            BLOCKED|INTERRUPTED) nblock=$(( nblock + 1 )); blocked+=("$n:$s") ;;
            *) nfail=$(( nfail + 1 )); failed+=("$n:$s") ;;
        esac
    done
    echo
    echo "HISTORY letters, oldest first, last 10 recorded results (BLOCKED/INTERRUPTED not counted):"
    echo "  P pass  I improved  F fail  E error  T timeout.  (q) = quarantined, never gating."
    if (( CONTROLS )); then
        echo
        printf '%-34s %-11s %-12s %s\n' CONTROL RESULT VALUE MEANING
        for (( i = 0; i < NCHECK; i++ )); do
            [[ -n "${CT[$i]:-}" ]] || continue
            n="${C[$i.name]}"; c="${CT[$i]}"
            case "$c" in
                OK) m="the control made the check FAIL, as required" ;;
                NOPOWER) m="the control PASSED the check: the check cannot see the breakage" ;;
                NONUMBER) m="the control produced no number: it proves nothing" ;;
                *) m="the control did not complete" ;;
            esac
            printf '%-34s %-11s %-12s %s\n' "$n" "$c" "${CVAL[$i]:--}" "$m"
            [[ "${C[$i.quarantine]}" == "true" ]] && continue
            case "$c" in
                OK) ;;
                BLOCKED|INTERRUPTED) nblock=$(( nblock + 1 )); blocked+=("control.$n:$c") ;;
                *) nfail=$(( nfail + 1 )); ctlbad+=("control.$n:$c") ;;
            esac
        done
    fi
    if [[ "$BUILD_STATUS" != "none" && "$BUILD_STATUS" != "OK" ]]; then
        failed+=("build:$BUILD_STATUS")
    fi
    echo
    for (( i = 0; i < NCHECK; i++ )); do
        [[ "${ST[$i]:-}" =~ ^(FAIL|ERROR|TIMEOUT|BLOCKED|INTERRUPTED)$ ]] || continue
        echo "--- ${C[$i.name]}: ${ST[$i]} (value ${VAL[$i]:-none}, ${C[$i.compare]} ${C[$i.limit]}; from ${SRC[$i]:-?})"
        echo "    log:     ${LOGF[$i]:-}"
        [[ -n "${LOGF[$i]:-}" ]] && tail -n 8 "${LOGF[$i]}" 2>/dev/null | sed 's/^/    | /'
    done
} > "$REPORT"

# ------------------------------------------------------ propose-lower ---
if (( PROPOSE )) && (( ${#PROP[@]} > 0 )); then
    PFILE="$RFILE.proposed"
    args=()
    for i in "${!PROP[@]}"; do args+=("${C[$i.@limit_line]}=${PROP[$i]}"); done
    LC_ALL=C awk -v spec="${args[*]}" '
        BEGIN { n = split(spec, a, " "); for (j = 1; j <= n; j++) { split(a[j], kv, "="); nl[kv[1]] = kv[2] } }
        (NR in nl) { match($0, /^[ \t]*limit:[ \t]*/); pre = substr($0, 1, RLENGTH)
                     rest = substr($0, RLENGTH + 1)
                     c = ""; if (match(rest, /[ \t]+#.*$/)) c = substr(rest, RSTART)
                     print pre nl[NR] c; next }
        { print }' "$RFILE" > "$PFILE"
    {
        echo
        echo "PROPOSED tightened limits written to $PFILE (NOT applied, NOT committed):"
        diff -u "$RFILE" "$PFILE" | sed 's/^/  /'
    } >> "$REPORT"
elif (( nimp > 0 )); then
    echo >> "$REPORT"
    echo "$nimp check(s) IMPROVED on their limit; rerun with --propose-lower to write a proposed file." >> "$REPORT"
fi

# ------------------------------------------------------------- summary ---
if (( nfail > 0 )) || [[ "$BUILD_STATUS" != "none" && "$BUILD_STATUS" != "OK" ]]; then verdict="FAIL"; rc=1
elif (( nblock > 0 )); then verdict="BLOCKED"; rc=2
else verdict="PASS"; rc=0; fi
summary="ratchet $RUN_ID $verdict: $npass pass ($nimp improved), $nfail fail, $nblock blocked, $nq quarantined"
(( CONTROLS )) && summary+=", controls run"
(( ${#failed[@]} )) && summary+=" | failed: ${failed[*]}"
(( ${#ctlbad[@]} )) && summary+=" | controls failed: ${ctlbad[*]}"
(( ${#blocked[@]} )) && summary+=" | blocked: ${blocked[*]}"
(( ${#flagged[@]} )) && summary+=" | should quarantine: ${flagged[*]}"
(( INTERRUPTED )) && summary+=" | INTERRUPTED"
echo "$summary" > "$OUT/ratchet-summary.txt"
{ echo "$summary"; echo; cat "$REPORT"; } > "$REPORT.tmp" && mv "$REPORT.tmp" "$REPORT"
echo "$summary"
echo "report: $REPORT"
exit "$rc"
