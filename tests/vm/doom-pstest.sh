#!/bin/sh
# doom-pstest.sh - end-to-end psDooM check for a Doom engine, with REAL signals
#
#   doom-pstest.sh <psdoom-binary> <iwad> <outdir>
#
# Only run this inside a disposable test guest. It starts sacrificial
# processes, lets the game shoot them through its normal damage path
# (-pstest), and checks what actually happened to them.
#
# Copyright (C) 2026 psdoom-plus contributors
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

BIN=$1
IWAD=$2
OUT=$3
mkdir -p "$OUT"
work=$(mktemp -d)
SLEEP=$(command -v sleep)
# A decoy named like a display server: protected in modern mode, fair game in
# classic. (Not "sshd": in classic that glob could hit our own ssh session.)
cp "$SLEEP" "$work/Xorg"

# PSTEST_WRAP runs the game under e.g. xvfb-run (GL engines can't use
# SDL's dummy video driver)
if [ -n "${PSTEST_WRAP:-}" ]; then
	export SDL_AUDIODRIVER=dummy
else
	export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
fi
export HOME="$work/home" XDG_STATE_HOME="$work/state"
mkdir -p "$HOME"
# the game shows only our processes (non-root default); make it explicit
PSDOOMUSER=$(id -un)
export PSDOOMUSER

pass=0
fail=0
ok()  { echo "  ok   $*"; pass=$((pass + 1)); }
bad() { echo "  FAIL $*"; fail=$((fail + 1)); }

# exit status of a reaped child in $status: 128+9 = KILL, 128+15 = TERM,
# 999 = still running. (Not via $(...): a subshell can't wait for our children.)
is_zombie() {  # Haiku's ps has no -o: there, trust kill -0 alone
	ps -o stat= -p "$1" 2>/dev/null | grep -q Z
}

reap() {
	if kill -0 "$1" 2>/dev/null && ! is_zombie "$1"; then
		status=999
		return
	fi
	wait "$1" 2>/dev/null
	status=$?
}

game() {
	log=$OUT/$1.log
	shift
	# shellcheck disable=SC2086
	${PSTEST_WRAP:-} "$BIN" -iwad "$IWAD" -warp 1 1 -nosound "$@" >"$log" 2>&1
	grep '^PSTEST' "$log" || echo "  (no PSTEST line; see $log)"
}

expect() {  # name pid wanted-status
	reap "$2"
	if [ "$status" = "$3" ]; then ok "$1 (status $status)"; else bad "$1: wanted $3, got $status"; fi
}

echo "== classic: shoot the sleeps"
$SLEEP 600 & s1=$!
$SLEEP 600 & s2=$!
"$work/Xorg" 600 & x1=$!
sleep 1
game classic-sleep -psclassic -pstest sleep -pstestview -pstestshot "$OUT/classic.ppm"
expect "classic kills sleep #1 with SIGKILL" $s1 137
expect "classic kills sleep #2 with SIGKILL" $s2 137
expect "classic leaves the untargeted Xorg decoy" $x1 999

echo "== classic: shoot the Xorg decoy (nothing is protected)"
game classic-xorg -psclassic -pstest Xorg
expect "classic kills a process named Xorg" $x1 137

echo "== modern: Xorg is protected, TERM then KILL for the rest"
$SLEEP 600 & s3=$!
sh -c "trap '' TERM; exec $SLEEP 600" & s4=$!
"$work/Xorg" 600 & x2=$!
sleep 1
game modern-xorg -pstest Xorg
expect "modern does not touch the Xorg decoy" $x2 999
game modern-sleep -psgrace 1500 -pstest sleep -pstestview -pstestshot "$OUT/modern.ppm"
expect "modern stops a sleep with SIGTERM" $s3 143
expect "modern escalates to SIGKILL when TERM is ignored" $s4 137
kill $x2 2>/dev/null; wait $x2 2>/dev/null

echo "== audit log"
if grep -q 'action=kill sig=KILL pid='"$s1" "$XDG_STATE_HOME/psdoom/actions.log" &&
   grep -q 'action=escalate sig=KILL pid='"$s4" "$XDG_STATE_HOME/psdoom/actions.log"; then
	ok "kills and escalations are logged"
else
	bad "audit log is missing entries"
fi
cp "$XDG_STATE_HOME/psdoom/actions.log" "$OUT/actions.log" 2>/dev/null

rm -rf "$work"
echo "== $pass passed, $fail failed"
[ "$fail" -eq 0 ]
