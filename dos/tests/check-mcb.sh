#!/bin/sh
# check-mcb.sh <dir> - judge MCBTEST.BAT's outputs (PS?.TXT C?.TXT M?.TXT PSD.LOG)
# from DOSBox-X (dosbox-pstest.sh) or a real-hardware run (files fetched back).
# SPDX-License-Identifier: GPL-2.0-or-later
set -u
out=${1:?usage: check-mcb.sh <dir>}
pass=0 fail=0
ok()  { echo "  ok   $1"; pass=$((pass + 1)); }
bad() { echo "  FAIL $1"; fail=$((fail + 1)); }
f() { for c in "$out/$1" "$out/$(echo "$1" | tr 'A-Z' 'a-z')"; do [ -f "$c" ] && { echo "$c"; return; }; done; echo "$out/$1"; }
count() { tr -d '\r' < "$(f "$1.TXT")" 2>/dev/null | grep -c " $2 " || true; }   # residents named $2
pstest() { tr -d '\r' < "$(f "$1.TXT")" 2>/dev/null | grep '^PSTEST' || echo "(no PSTEST line in $1)"; }
log=$(f PSD.LOG)

echo "== classic: shoot the TSRs"
pstest C1
[ "$(count PS0 TSR)" = 2 ] && [ "$(count PS0 HOOKTSR)" = 1 ] && ok "two TSRs and HOOKTSR resident before" || bad "TSRs not resident before (PS0)"
pstest C1 | grep -q "targeted=2 removed=2 result=ok" && ok "classic -pstest TSR: 2 shot, 2 gone" || bad "classic -pstest TSR"
[ "$(count PS1 TSR)" = 0 ] && ok "classic frees both TSRs (MCB chain)" || bad "TSRs still resident (PS1)"
[ "$(count PS1 HOOKTSR)" = 1 ] && ok "classic leaves the untargeted HOOKTSR" || bad "HOOKTSR gone too early (PS1)"
echo "== classic: shoot the hooking TSR (nothing is protected)"
pstest C2
[ "$(count PS2 HOOKTSR)" = 0 ] && ok "classic frees HOOKTSR although INT F1h points into it" || bad "HOOKTSR still resident (PS2)"
echo "== modern: hooked blocks and our own chain are protected"
pstest M1
[ "$(count PS3 TSR)" = 2 ] && [ "$(count PS3 HOOKTSR)" = 1 ] && ok "TSRs reloaded" || bad "TSRs not reloaded (PS3)"
[ "$(count PS4 TSR)" = 0 ] && ok "modern frees the plain TSRs" || bad "modern left TSRs (PS4)"
pstest M2
[ "$(count PS5 HOOKTSR)" = 1 ] && ok "modern does not free HOOKTSR (INT F1h points into it)" || bad "modern freed HOOKTSR (PS5)"
pstest M3
[ "$(count PS6 COMMAND)" -ge 1 ] && ok "modern does not free COMMAND.COM (our parent)" || bad "COMMAND gone (PS6)"
echo "== modern: a TSR further down an interrupt chain is protected"
pstest M4
[ "$(count PS7 CHAINTSR)" = 1 ] && tr -d '\r' < "$(f PS7.TXT)" | grep " HOOKTSR " | grep -q protected &&
	ok "HOOKTSR behind CHAINTSR is still protected (PS7)" || bad "HOOKTSR behind CHAINTSR not protected (PS7)"
[ "$(count PS8 HOOKTSR)" = 1 ] && ok "modern does not free HOOKTSR from the middle of the chain (PS8)" || bad "modern freed a mid-chain HOOKTSR (PS8)"
echo "== audit log"
# a protected monster is invulnerable, so modern never even asks to kill it
if [ "$(tr -d '\r' < "$log" 2>/dev/null | grep -c "mode=classic .*action=kill .*name=TSR result=ok")" = 2 ] &&
   tr -d '\r' < "$log" | grep -q "mode=modern .*action=kill .*name=TSR result=ok" &&
   ! tr -d '\r' < "$log" | grep -q "mode=modern .*action=kill .*name=HOOKTSR"; then
	ok "kills are logged, none for protected HOOKTSR (PSD.LOG)"
else
	bad "audit log (PSD.LOG)"
fi
echo "== resident programs left unprotected in modern mode (PS0, informational)"
tr -d '\r' < "$(f PS0.TXT)" 2>/dev/null | awk '$5 == "program" && $6 == "-" && $3 != "TSR" {print}'
echo "== drivers seen (PS0, informational)"
tr -d '\r' < "$(f PS0.TXT)" 2>/dev/null | grep " driver " || echo "  (none: DOSBox-X loads no CONFIG.SYS drivers)"
echo "== $pass passed, $fail failed   ($out)"
[ "$fail" -eq 0 ]
