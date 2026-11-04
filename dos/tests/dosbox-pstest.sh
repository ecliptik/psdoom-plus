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

game='PSDOOM -iwad DOOM2.WAD -warp 1 1 -nosound'
# shellcheck disable=SC2028
cat > "$work/TEST.BAT" <<EOF
@ECHO OFF
TSR
TSR
HOOKTSR
PSDPS > PS0.TXT
$game -psclassic -pstest TSR -pstestview -pstestshot CLASSIC.PPM > C1.TXT
PSDPS > PS1.TXT
$game -psclassic -pstest HOOKTSR > C2.TXT
PSDPS > PS2.TXT
TSR
TSR
HOOKTSR
PSDPS > PS3.TXT
$game -pstest TSR > M1.TXT
PSDPS > PS4.TXT
$game -pstest HOOKTSR > M2.TXT
PSDPS > PS5.TXT
$game -pstest COMMAND > M3.TXT
PSDPS > PS6.TXT
EOF
sed -i 's/$/\r/' "$work/TEST.BAT"

echo "== DOSBox-X: TSRs, classic then modern"
CWSDPMI=$here/vendor/cwsdpmi/cwsdpmi.exe "$hub/dosbox-run.sh" --fast \
	--exe "$work/TEST.BAT" --keep-stage --stage-file "$work/stage" \
	--include "$work/PSDOOM.EXE" --include "$work/PSDPS.EXE" \
	--include "$work/TSR.COM" --include "$work/HOOKTSR.COM" \
	--include "$work/DOOM2.WAD" --include "$work/PSDOOM2.WAD" \
	--stdout "$work/stdout.txt" >/dev/null 2>&1 || true
stage=$(cat "$work/stage")
rm -rf "$out"
mkdir -p "$out"
for f in "$stage"/*.TXT "$stage"/*.PPM "$stage"/PSD.LOG; do
	[ -f "$f" ] && cp "$f" "$out/"
done
"$hub/dosbox-stage.sh" copied "$stage" >/dev/null 2>&1 || true
rm -rf "$stage"

pass=0 fail=0
ok()  { echo "  ok   $1"; pass=$((pass + 1)); }
bad() { echo "  FAIL $1"; fail=$((fail + 1)); }
count() { grep -c " $2 " "$out/$1.TXT" 2>/dev/null || true; }   # residents named $2
pstest() { tr -d '\r' < "$out/$1.TXT" 2>/dev/null | grep '^PSTEST' || echo "(no PSTEST line in $1)"; }

echo "== classic: shoot the TSRs"
pstest C1
[ "$(count PS0 TSR)" = 2 ] && [ "$(count PS0 HOOKTSR)" = 1 ] && ok "two TSRs and HOOKTSR resident before" || bad "TSRs not resident before (PS0)"
pstest C1 | grep -q "targeted=2 removed=2 result=ok" && ok "classic -pstest TSR: 2 shot, 2 gone" || bad "classic -pstest TSR"
[ "$(count PS1 TSR)" = 0 ] && ok "classic frees both TSRs (MCB chain)" || bad "TSRs still resident (PS1)"
[ "$(count PS1 HOOKTSR)" = 1 ] && ok "classic leaves the untargeted HOOKTSR" || bad "HOOKTSR gone too early (PS1)"
echo "== classic: shoot the hooking TSR (nothing is protected)"
pstest C2
[ "$(count PS2 HOOKTSR)" = 0 ] && ok "classic frees HOOKTSR although INT 60h points into it" || bad "HOOKTSR still resident (PS2)"
echo "== modern: hooked blocks and our own chain are protected"
pstest M1
[ "$(count PS3 TSR)" = 2 ] && [ "$(count PS3 HOOKTSR)" = 1 ] && ok "TSRs reloaded" || bad "TSRs not reloaded (PS3)"
[ "$(count PS4 TSR)" = 0 ] && ok "modern frees the plain TSRs" || bad "modern left TSRs (PS4)"
pstest M2
[ "$(count PS5 HOOKTSR)" = 1 ] && ok "modern does not free HOOKTSR (INT 60h points into it)" || bad "modern freed HOOKTSR (PS5)"
pstest M3
[ "$(count PS6 COMMAND)" -ge 1 ] && ok "modern does not free COMMAND.COM (our parent)" || bad "COMMAND gone (PS6)"
echo "== audit log"
# a protected monster is invulnerable, so modern never even asks to kill it
if [ "$(grep -c "mode=classic .*action=kill .*name=TSR result=ok" "$out/PSD.LOG" 2>/dev/null)" = 2 ] &&
   grep -q "mode=modern .*action=kill .*name=TSR result=ok" "$out/PSD.LOG" &&
   ! grep -q "mode=modern .*action=kill .*name=HOOKTSR" "$out/PSD.LOG"; then
	ok "kills are logged, none for protected HOOKTSR (PSD.LOG)"
else
	bad "audit log (PSD.LOG)"
fi
echo "== $pass passed, $fail failed   (outputs: $out)"
[ "$fail" -eq 0 ]
