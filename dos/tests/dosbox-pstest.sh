#!/bin/sh
# dosbox-pstest.sh - psdoom-plus on DOS, end to end in DOSBox-X: load test
# TSRs, let PSDOOM.EXE -pstest shoot them, and check the MCB chain after
# each run with PSDPS.EXE (libpsd's own view of DOS memory).
#
#   dos/tests/dosbox-pstest.sh [iwad]     (from anywhere; `make check` runs it)
#
# Needs: make game tests cwsdpmi, DOSBox-X, an IWAD (default: Freedoom 2).
# Everything runs inside the emulator: nothing on the host is touched.
#
# Copyright (C) 2026 psdoom-plus contributors
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
top=$(cd "$here/.." && pwd)
hub=$top/.sdl-dos-ports/shared/tools
build=$here/build
iwad=${1:-/usr/share/games/doom/freedoom2.wad}
out=${PSTEST_OUT:-$build/pstest}

for f in "$build/psdoom.exe" "$build/psdps.exe" "$build/tsr.com" "$build/hooktsr.com" \
         "$here/vendor/cwsdpmi/cwsdpmi.exe" "$iwad" "$top/data/psdoom2.wad"; do
	[ -f "$f" ] || { echo "missing $f (make game tests cwsdpmi?)" >&2; exit 2; }
done

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp "$build/psdoom.exe" "$work/PSDOOM.EXE"
cp "$build/psdps.exe" "$work/PSDPS.EXE"
cp "$build/tsr.com" "$work/TSR.COM"
cp "$build/hooktsr.com" "$work/HOOKTSR.COM"
cp "$iwad" "$work/DOOM2.WAD"
cp "$top/data/psdoom2.wad" "$work/PSDOOM2.WAD"

cp "$here/tests/MCBTEST.BAT" "$work/MCBTEST.BAT"
printf '@ECHO OFF\r\nCALL MCBTEST DOOM2.WAD -nosound\r\n' > "$work/TEST.BAT"

echo "== DOSBox-X: TSRs, classic then modern"
CWSDPMI=$here/vendor/cwsdpmi/cwsdpmi.exe "$hub/dosbox-run.sh" --fast \
	--exe "$work/TEST.BAT" --keep-stage --stage-file "$work/stage" \
	--include "$work/PSDOOM.EXE" --include "$work/PSDPS.EXE" \
	--include "$work/TSR.COM" --include "$work/HOOKTSR.COM" \
	--include "$work/DOOM2.WAD" --include "$work/PSDOOM2.WAD" --include "$work/MCBTEST.BAT" \
	--stdout "$work/stdout.txt" >/dev/null 2>&1 || true
stage=$(cat "$work/stage")
rm -rf "$out"
mkdir -p "$out"
for f in "$stage"/*.TXT "$stage"/*.PPM "$stage"/PSD.LOG; do
	[ -f "$f" ] && cp "$f" "$out/"
done
"$hub/dosbox-stage.sh" copied "$stage" >/dev/null 2>&1 || true
rm -rf "$stage"

exec "$here/tests/check-mcb.sh" "$out"
