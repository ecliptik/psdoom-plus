#!/bin/sh
# dos-net-pstest.sh - PSDOOM.EXE in DOSBox-X shoots this guest's processes
# through psdoomd, with REAL signals
#
#   dos-net-pstest.sh <bundle-dir> <psdoomd> <iwad> <outdir>
#
# Only run this inside a disposable test guest. DOSBox-X's NE2000 (slirp)
# with Crynwr's packet driver carries psdoom-plus's own UDP to psdoomd on
# the guest's loopback (slirp's 10.0.2.2); psdoomd uses the native backend.
# The bundle comes from `make -C dos bundle` (tools/psdev test dos ships it).
#
# Copyright (C) 2026 psdoom-plus contributors
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

BUNDLE=$1
PSDOOMD=$2
IWAD=$3
OUT=$4
here=$(cd "$(dirname "$0")" && pwd)
CONF=$here/../../.sdl-dos-ports/shared/tools/dosbox-x-fast.conf
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)          # dos() runs inside the C: directory
BUNDLE=$(cd "$BUNDLE" && pwd)
case $PSDOOMD in /*) ;; *) PSDOOMD=$PWD/$PSDOOMD ;; esac
work=$(mktemp -d)
SLEEP=$(command -v sleep)
cp "$SLEEP" "$work/Xorg"         # protected by name in modern mode
port=$((47100 + $$ % 800))
token=$(od -An -N12 -tx1 /dev/urandom | tr -d ' \n')
export HOME="$work/home" XDG_STATE_HOME="$work/state"
mkdir -p "$HOME"
PSDOOMUSER=$(id -un)
export PSDOOMUSER

pass=0
fail=0
ok()  { echo "  ok   $*"; pass=$((pass + 1)); }
bad() { echo "  FAIL $*"; fail=$((fail + 1)); }

reap() {  # exit status in $status; 999 = still running
	if kill -0 "$1" 2>/dev/null && ! ps -o stat= -p "$1" 2>/dev/null | grep -q Z; then
		status=999
		return
	fi
	wait "$1" 2>/dev/null
	status=$?
}
expect() {  # name pid wanted-status
	reap "$2"
	if [ "$status" = "$3" ]; then ok "$1 (status $status)"; else bad "$1: wanted $3, got $status"; fi
}

server() {  # [psdoomd options]: (re)start psdoomd on the loopback
	[ -n "${srv:-}" ] && kill "$srv" 2>/dev/null && wait "$srv" 2>/dev/null
	PSDOOMD_TOKEN=$token "$PSDOOMD" -l 127.0.0.1:$port -pslog "$OUT/psdoomd.log" "$@" \
		2>>"$OUT/psdoomd.err" &
	srv=$!
	sleep 1
}

# whatever happens, leave no sacrificial process (or psdoomd) behind: they
# would keep the caller's ssh session open
cleanup() {
	for p in ${s1:-} ${s2:-} ${x1:-} ${srv:-}; do kill "$p" 2>/dev/null; done
	rm -rf "$work"
}
trap cleanup EXIT INT TERM

stage=$work/c
mkdir -p "$stage"
cp "$BUNDLE"/* "$stage/"
cp "$IWAD" "$stage/DOOM2.WAD"

dos() {  # name [PSDOOM options]: one DOSBox-X run, its PSTEST line
	name=$1
	shift
	printf '@ECHO OFF\r\nNE2000 0x60 3 0x300 > NUL\r\nSET PSDOOM_IP=10.0.2.15\r\nSET PSDOOM_GATEWAY=10.0.2.2\r\nSET PSDOOM_NET_TOKEN=%s\r\nSET PSDOOM_STDERR=ERR.TXT\r\nPSDOOM -iwad DOOM2.WAD -warp 1 1 -nosound -psnet 10.0.2.2:%s %s > OUT.TXT\r\n' \
		"$token" "$port" "$*" > "$stage/RUN.BAT"
	rm -f "$stage/OUT.TXT" "$stage/ERR.TXT"
	(cd "$stage" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 240 dosbox-x -silent -exit \
		-conf "$CONF" -set "ne2000 ne2000=true" -set "ne2000 backend=slirp" \
		-set "ne2000 nicbase=300" -set "ne2000 nicirq=3" \
		-c "mount c ." -c "c:" -c "RUN.BAT" >"$OUT/$name.dosbox" 2>&1)
	tr -d '\r' < "$stage/OUT.TXT" > "$OUT/$name.log" 2>/dev/null
	tr -d '\r' < "$stage/ERR.TXT" > "$OUT/$name.err" 2>/dev/null
	grep '^PSTEST' "$OUT/$name.log" || echo "  (no PSTEST line; see $OUT/$name.log)"
}

echo "== modern psdoomd, modern DOS client: TERM the sleeps"
$SLEEP 600 & s1=$!
$SLEEP 600 & s2=$!
"$work/Xorg" 600 & x1=$!
server
dos modern-sleep -pstest sleep
expect "DOS kills remote sleep #1 with SIGTERM" $s1 143
expect "DOS kills remote sleep #2 with SIGTERM" $s2 143
expect "the untargeted Xorg decoy lives" $x1 999

echo "== modern psdoomd, classic DOS client: psdoomd's rules win"
dos classic-xorg-refused -psclassic -pstest Xorg
expect "psdoomd refuses to kill Xorg for a classic client" $x1 999

echo "== classic psdoomd, classic DOS client: nothing is protected"
server -psclassic
dos classic-xorg -psclassic -pstest Xorg
expect "DOS kills a remote Xorg with SIGKILL" $x1 137
kill "$srv" 2>/dev/null; wait "$srv" 2>/dev/null

echo "== audit log (psdoomd's)"
if [ -s "$OUT/psdoomd.log" ] &&
   grep -q "action=signal sig=TERM pid=$s1 .*result=ok" "$OUT/psdoomd.log" &&
   grep -q "action=signal sig=KILL pid=$x1 .*result=protected" "$OUT/psdoomd.log" &&
   grep -q "mode=classic .*action=signal sig=KILL pid=$x1 .*result=ok" "$OUT/psdoomd.log"; then
	ok "psdoomd logs the kills and the refusal"
else
	bad "psdoomd's audit log is missing entries"
fi

echo "== $pass passed, $fail failed"
[ "$fail" -eq 0 ]
