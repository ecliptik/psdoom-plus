#!/usr/bin/env bash
# dosbox-stage.sh -- bookkeeping and garbage collection for dosbox-run.sh's
# stage directories ($TMPDIR/dos-port-dosbox.XXXXXX, default TMPDIR=/tmp).
#
# Why: a kept stage (--keep-stage) holds the staged exe and game package
# plus everything the run wrote, and nothing ever deleted one. A port's
# harness that keeps every stage so it can read results out of it filled
# the host disk (2026-09-25: ~2,150 stages, ~55 GB, a build failed on
# ENOSPC). Deleting a stage the moment it is read is wrong too: a crashed
# copy-out or a copy race is only recoverable while the stage still exists
# (a lab's run-cells stage race was recovered that way the same day). So a
# stage is deleted only after a grace period, the newest few per caller are
# always kept, and anything in use is never touched.
#
# Every stage dosbox-run.sh creates gets a sidecar "<stage>.meta" (outside
# the DOS-visible mount), key=value lines, last value wins:
#   caller=<repo>/<script>  pid=<dosbox-run.sh pid>  pstart=<its start time>
#   created=<epoch>  keep=0|1  exe=<basename>
#   exit=<rc> ended=<epoch>          (appended when dosbox-run.sh exits)
#   copied=1 copied_at=<epoch>       (appended by "dosbox-stage.sh copied")
#
# Usage:
#   dosbox-stage.sh copied STAGE...
#       Mark a stage's results as copied out: it may go after the grace.
#       A caller that never marks still gets the max-age rule.
#   dosbox-stage.sh gc [--apply] [--dir DIR] [--caller NAME] [--grace DUR]
#                      [--keep N] [--orphan-age DUR] [--max-age DUR]
#                      [--budget SIZE] [--quiet] [--verbose]
#       Dry-run unless --apply. DUR is N, Ns, Nm, Nh or Nd; SIZE is N (MB),
#       NM, NG. Defaults: --dir ${TMPDIR:-/tmp}, --grace 2h, --keep 20,
#       --orphan-age 1h, --max-age 48h, no budget.
#   dosbox-stage.sh caller
#       Print the caller name dosbox-run.sh would record from here.
#
# gc rules. Never delete a stage that is IN USE:
#   - a process has its cwd or an open fd inside it, or
#   - a live process's command line has "MOUNT C <stage>" (DOSBox-X), or
#   - its meta pid is alive with the recorded pstart (the run is going).
# Otherwise group by caller (a stage with no meta is caller "?<exe>", its
# apparent caller from RUN.BAT), newest first by the stage's latest
# timestamp (created/ended/copied_at, the dir and its top-level entries):
#   1. the newest --keep stages of each caller are kept;
#   2. copied=1 and older than --grace: delete;
#   3. keep=0 (its run died before cleaning up) and older than
#      --orphan-age: delete;
#   4. older than --max-age, copied or not: delete.
# --budget SIZE then deletes the oldest remaining stages (keep-N or not)
# until the total fits, but never one in use or younger than --grace.
# --caller restricts all of this to one caller; dosbox-run.sh's automatic
# low-disk pass uses it, so a run only ever collects its own caller's
# stages. Collecting across callers is a person's (or an owner's) explicit
# "dosbox-stage.sh gc".
#
# Host tools only (bash, awk, coreutils, flock). ASCII only.

# ---------------------------------------------------------------------------
# Library part (dosbox-run.sh sources this file; no side effects on source)

_dbxstage_pstart() { # field 22 of /proc/<pid>/stat
    local s
    s="$(cat "/proc/$1/stat" 2>/dev/null)" || return 1
    s="${s##*) }"
    # shellcheck disable=SC2086
    set -- $s
    echo "${20}"
}

# dbxstage_caller [PID]: the caller to record for a stage, "<repo>/<script>".
# Walks up from PID (default: our parent) past plain wrappers (timeout, env,
# setsid, nice, flock, xargs, stdbuf, ionice) and "-c" or interactive
# shells to the first interpreter (sh, bash, python, perl, node, ruby)
# running a script file -- its first non-option argument, an existing file
# -- and names it by its git toplevel's basename plus the script's
# basename. The first other binary on the way up is named by itself.
# DOSBOX_STAGE_CALLER overrides (a lab worker can name itself); "unknown"
# if nothing sensible is found.
dbxstage_caller() {
    if [[ -n "${DOSBOX_STAGE_CALLER:-}" ]]; then echo "$DOSBOX_STAGE_CALLER"; return; fi
    local pid="${1:-$PPID}" n=0 comm args=() a script cwd top ppid
    while [[ -n "$pid" && "$pid" != "0" && "$pid" != "1" && $n -lt 16 ]]; do
        n=$((n + 1))
        comm="$(cat "/proc/$pid/comm" 2>/dev/null)" || break
        ppid="$(awk '{print $4}' "/proc/$pid/stat" 2>/dev/null || true)"
        mapfile -d '' -t args < "/proc/$pid/cmdline" 2>/dev/null || args=()
        cwd="$(readlink "/proc/$pid/cwd" 2>/dev/null || true)"
        case "$comm" in
            timeout|env|setsid|nice|flock|xargs|stdbuf|ionice) pid="$ppid"; continue ;;
        esac
        script=""
        case "$(basename "${args[0]:-x}")" in
            bash|sh|dash|zsh|ksh|python*|perl*|node|ruby) ;;
            *) echo "$comm"; return ;;   # a binary: named by itself
        esac
        for a in "${args[@]:1}"; do
            [[ "$a" == -c ]] && break
            [[ "$a" == -* ]] && continue
            [[ "$a" != /* && -n "$cwd" ]] && a="$cwd/$a"
            [[ -f "$a" ]] && script="$a"
            break
        done
        if [[ -z "$script" ]]; then   # "-c" or interactive: go up
            pid="$ppid"; continue
        fi
        top="$(git -C "$(dirname "$script")" rev-parse --show-toplevel 2>/dev/null || true)"
        if [[ -n "$top" ]]; then echo "$(basename "$top")/$(basename "$script")"
        else echo "$(basename "$script")"; fi
        return
    done
    echo unknown
}

# dbxstage_meta_add STAGE KEY=VALUE...: append lines to STAGE.meta
dbxstage_meta_add() {
    local stage="$1"; shift
    local kv
    for kv in "$@"; do printf '%s\n' "$kv"; done >> "$stage.meta" 2>/dev/null || true
}

# ---------------------------------------------------------------------------
# CLI part

_die() { echo "dosbox-stage.sh: $*" >&2; exit 2; }

_dur() { # DUR -> seconds
    local v="$1"
    [[ "$v" =~ ^([0-9]+)([smhd]?)$ ]] || _die "bad duration: $v (N, Ns, Nm, Nh or Nd)"
    local n="${BASH_REMATCH[1]}"
    case "${BASH_REMATCH[2]}" in
        ''|s) echo "$n" ;; m) echo $((n * 60)) ;; h) echo $((n * 3600)) ;; d) echo $((n * 86400)) ;;
    esac
}
_size_kb() { # SIZE -> KiB
    local v="$1"
    [[ "$v" =~ ^([0-9]+)([MmGg]?)$ ]] || _die "bad size: $v (N MB, NM or NG)"
    local n="${BASH_REMATCH[1]}"
    case "${BASH_REMATCH[2]}" in ''|M|m) echo $((n * 1024)) ;; G|g) echo $((n * 1024 * 1024)) ;; esac
}

_mb() { awk -v k="$1" 'BEGIN { printf "%.1fM", k / 1024 }'; }

_is_stage_dir() { # path: a dosbox-run.sh stage (mktemp XXXXXX), a directory
    [[ -d "$1" && ! -L "$1" && "$(basename "$1")" =~ ^dos-port-dosbox\.[A-Za-z0-9]{6}$ ]]
}

cmd_copied() {
    [[ $# -gt 0 ]] || _die "copied: which stage?"
    local s now; now="$(date +%s)"
    for s in "$@"; do
        s="${s%/}"
        _is_stage_dir "$s" || _die "copied: not a stage directory: $s"
        dbxstage_meta_add "$s" copied=1 "copied_at=$now"
        echo "dosbox-stage.sh: marked copied: $s"
    done
}

# _in_use_set DIR: print every stage path under DIR that is in use by some
# process (cwd, open fd, or a "MOUNT C <stage>" command line).
_in_use_set() {
    local dir="$1" f
    # cwd and fds (unreadable /proc entries of other users are skipped)
    find /proc/[0-9]*/cwd /proc/[0-9]*/fd -maxdepth 1 -lname "$dir/dos-port-dosbox.*" -printf '%l\n' 2>/dev/null |
        sed -E 's#^(.*/dos-port-dosbox\.[A-Za-z0-9]{6}).*#\1#'
    for f in /proc/[0-9]*/cmdline; do
        tr '\0' ' ' 2>/dev/null < "$f"; echo
    done | grep -aoE 'MOUNT C [^ ]*/dos-port-dosbox\.[A-Za-z0-9]{6}' | sed 's/^MOUNT C //'
}

cmd_gc() {
    local apply=0 dir="${TMPDIR:-/tmp}" only="" grace=7200 keep=20 orphan=3600 maxage=172800
    local budget_kb="" quiet=0 verbose=0
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --apply) apply=1; shift ;;
            --dir) dir="${2%/}"; shift 2 ;;
            --caller) only="$2"; shift 2 ;;
            --grace) grace="$(_dur "$2")"; shift 2 ;;
            --keep) [[ "$2" =~ ^[0-9]+$ ]] || _die "--keep wants a count"; keep="$2"; shift 2 ;;
            --orphan-age) orphan="$(_dur "$2")"; shift 2 ;;
            --max-age) maxage="$(_dur "$2")"; shift 2 ;;
            --budget) budget_kb="$(_size_kb "$2")"; shift 2 ;;
            --quiet) quiet=1; shift ;;
            --verbose) verbose=1; shift ;;
            *) _die "gc: unknown arg: $1" ;;
        esac
    done
    [[ -d "$dir" ]] || _die "gc: no such directory: $dir"
    dir="$(cd "$dir" && pwd -P)"

    # one collector at a time
    exec {gcfd}>"$dir/dos-port-stage-gc.lock"
    flock -w 120 "$gcfd" || _die "gc: another gc holds $dir/dos-port-stage-gc.lock"

    local now; now="$(date +%s)"
    local inuse; inuse="$(_in_use_set "$dir" | sort -u)"

    # one record per stage: ref<TAB>caller<TAB>flags<TAB>path
    # flags: I (in use), C (copied), O (keep=0, i.e. orphan), M (has meta)
    local recs="" s meta caller ref t flags pid pst keepv
    for s in "$dir"/dos-port-dosbox.??????; do
        _is_stage_dir "$s" || continue
        meta="$s.meta"; flags=""; caller=""; ref=0
        t="$(stat -c %Y "$s" 2>/dev/null)" || continue
        ref="$t"
        while read -r t; do (( t > ref )) && ref="$t"; done < <(find "$s" -mindepth 1 -maxdepth 1 -printf '%T@\n' 2>/dev/null | cut -d. -f1)
        if [[ -f "$meta" ]]; then
            flags+="M"
            caller="$(awk -F= '$1=="caller"{v=substr($0,8)} END{print v}' "$meta")"
            for t in $(awk -F= '$1=="created"||$1=="ended"||$1=="copied_at"{print $2}' "$meta"); do
                [[ "$t" =~ ^[0-9]+$ ]] && (( t > ref )) && ref="$t"
            done
            [[ "$(awk -F= '$1=="copied"{v=$2} END{print v}' "$meta")" == "1" ]] && flags+="C"
            keepv="$(awk -F= '$1=="keep"{v=$2} END{print v}' "$meta")"
            pid="$(awk -F= '$1=="pid"{v=$2} END{print v}' "$meta")"
            pst="$(awk -F= '$1=="pstart"{v=$2} END{print v}' "$meta")"
            if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null && [[ "$(_dbxstage_pstart "$pid")" == "$pst" ]]; then
                flags+="I"
            elif [[ "$keepv" == "0" ]]; then
                flags+="O"
            fi
        fi
        if [[ -z "$caller" ]]; then
            t="$(grep -aoE '^[^@ ]+\.(EXE|exe|BAT|bat)' "$s/RUN.BAT" 2>/dev/null | head -1)"
            [[ -z "$t" ]] && t="$(grep -aoE '[A-Za-z0-9_~-]+\.(EXE|exe)' "$s/RUN.BAT" 2>/dev/null | head -1)"
            caller="?$(echo "${t:-norunbat}" | tr '[:upper:]' '[:lower:]')"
        fi
        [[ -n "$only" && "$caller" != "$only" ]] && continue
        grep -qxF "$s" <<<"$inuse" && flags+="I"
        recs+="$ref"$'\t'"$caller"$'\t'"${flags:--}"$'\t'"$s"$'\n'
    done

    # decide: rules per caller, newest first
    local plan
    plan="$(printf '%s' "$recs" | sort -t$'\t' -k2,2 -k1,1nr | awk -F'\t' -v now="$now" \
        -v keep="$keep" -v grace="$grace" -v orphan="$orphan" -v maxage="$maxage" '
        NF < 4 { next }
        {
            ref = $1; caller = $2; flags = $3; path = $4; age = now - ref
            if ($2 != last) { rank = 0; last = $2 }
            rank++
            if (flags ~ /I/)                          { act = "keep"; why = "in-use" }
            else if (rank <= keep)                    { act = "keep"; why = "newest-" keep }
            else if (flags ~ /C/ && age >= grace)     { act = "delete"; why = "copied" }
            else if (flags ~ /O/ && age >= orphan)    { act = "delete"; why = "orphan" }
            else if (age >= maxage)                   { act = "delete"; why = "max-age" }
            else                                      { act = "keep"; why = "young" }
            printf "%s\t%s\t%s\t%s\t%s\t%s\n", act, why, age, caller, flags, path
        }')"

    # budget: oldest remaining first, never in use or younger than grace
    if [[ -n "$budget_kb" ]]; then
        local total=0 kb line act why age fl p newplan=""
        while IFS=$'\t' read -r act why age caller fl p; do
            [[ "$act" == "keep" ]] || continue
            kb="$(du -sk "$p" 2>/dev/null | cut -f1)"; total=$((total + ${kb:-0}))
        done <<<"$plan"
        if (( total > budget_kb )); then
            while IFS=$'\t' read -r act why age caller fl p; do
                [[ -z "$p" ]] && continue
                if [[ "$act" == "keep" && "$fl" != *I* ]] && (( age >= grace && total > budget_kb )); then
                    kb="$(du -sk "$p" 2>/dev/null | cut -f1)"; total=$((total - ${kb:-0}))
                    act=delete; why=budget
                fi
                newplan+="$act"$'\t'"$why"$'\t'"$age"$'\t'"$caller"$'\t'"$fl"$'\t'"$p"$'\n'
            done < <(printf '%s\n' "$plan" | sort -t$'\t' -k3,3nr)
            plan="$newplan"
        fi
    fi

    # re-check in-use right before deleting (a run may have started since)
    [[ "$apply" == "1" ]] && inuse="$(_in_use_set "$dir" | sort -u)"

    local nd=0 nk=0 freed=0 kb act why age fl p verb
    local -A c_n c_d c_kb
    verb="would delete"; [[ "$apply" == "1" ]] && verb="deleted"
    while IFS=$'\t' read -r act why age caller fl p; do
        [[ -z "$p" ]] && continue
        c_n[$caller]=$(( ${c_n[$caller]:-0} + 1 ))
        if [[ "$act" == "delete" ]] && [[ "$apply" == "1" ]] && grep -qxF "$p" <<<"$inuse"; then
            act=keep; why=in-use-now
        fi
        if [[ "$act" == "delete" ]]; then
            kb="$(du -sk "$p" 2>/dev/null | cut -f1)"; kb="${kb:-0}"
            if [[ "$apply" == "1" ]]; then rm -rf -- "$p" "$p.meta"; fi
            nd=$((nd + 1)); freed=$((freed + kb))
            c_d[$caller]=$(( ${c_d[$caller]:-0} + 1 )); c_kb[$caller]=$(( ${c_kb[$caller]:-0} + kb ))
            [[ "$quiet" == "1" ]] || printf '%s %s  caller=%s age=%dh size=%s reason=%s\n' \
                "$verb" "$p" "$caller" $((age / 3600)) "$(_mb "$kb")" "$why"
        else
            nk=$((nk + 1))
            [[ "$verbose" == "1" ]] && printf 'keep %s  caller=%s age=%dh reason=%s\n' \
                "$p" "$caller" $((age / 3600)) "$why"
        fi
    done <<<"$plan"
    if [[ "$quiet" != "1" ]]; then
        for caller in $(printf '%s\n' "${!c_n[@]}" | sort); do
            printf 'caller %s: %d stages, %s %d (%s)\n' "$caller" "${c_n[$caller]}" "$verb" \
                "${c_d[$caller]:-0}" "$(_mb "${c_kb[$caller]:-0}")"
        done
    fi
    printf 'dosbox-stage.sh gc%s: %s %d stage(s), %s; kept %d (%s%s)\n' \
        "$([[ "$apply" == "1" ]] || echo ' (dry run)')" "$verb" "$nd" "$(_mb "$freed")" "$nk" \
        "$dir" "${only:+, caller $only}"
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    set -uo pipefail   # no -e: every failure below is handled where it happens
    case "${1:-}" in
        copied) shift; cmd_copied "$@" ;;
        gc) shift; cmd_gc "$@" ;;
        caller) dbxstage_caller "$PPID" ;;
        -h|--help|"") sed -n '2,/^# Host tools only/p' "$0" | sed 's/^# \{0,1\}//' ;;
        *) _die "unknown command: $1 (copied, gc, caller)" ;;
    esac
fi
