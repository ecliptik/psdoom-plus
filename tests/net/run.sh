#!/bin/sh
# tests/net/run.sh - psdoomd and the net backend, against a fake process table
#
#   tests/net/run.sh <psdoomd> [<engine binary> <iwad>]
#
# psdoomd runs over libpsd's cmd backend with a fake psdoom-ng helper, so
# nothing real is signalled; the protocol is driven by a small Python client,
# then (optionally) by an engine with -psnet -pstest.
# SPDX-License-Identifier: GPL-2.0-or-later
set -u
PSDOOMD=${1:?usage: run.sh <psdoomd> [<engine> <iwad>]}
ENGINE=${2:-}
IWAD=${3:-}
work=$(mktemp -d)
port=$((47000 + $$ % 900))
token=test-token-0123456789
pass=0 fail=0
ok()  { echo "  ok   $*"; pass=$((pass + 1)); }
bad() { echo "  FAIL $*"; fail=$((fail + 1)); }

cat > "$work/helper.sh" <<'F'
#!/bin/sh
lock() { while ! mkdir "$STATE.lck" 2>/dev/null; do sleep 0.01; done; }
case "$1" in
ps)     lock; cat "$STATE"; rmdir "$STATE.lck" ;;
renice) echo "renice $2 $PSDOOM_NICE" >> "$LOG" ;;
kill)   echo "kill $2 $PSDOOM_SIGNAL" >> "$LOG"
        lock; grep -v " $2 " "$STATE" > "$STATE.new"; mv "$STATE.new" "$STATE"; rmdir "$STATE.lck" ;;
esac
F
printf 'tester 4242 /bin/sleep 0\ntester 4243 /bin/sleep 0\ntester 4244 /usr/sbin/sshd 1\n' > "$work/state"
: > "$work/log"
export STATE="$work/state" LOG="$work/log" PSDOOMUSER=tester
export PSDOOM_NG_PS_CMD="sh $work/helper.sh ps" PSDOOM_NG_RENICE_CMD="sh $work/helper.sh renice"
export PSDOOM_NG_KILL_CMD="sh $work/helper.sh kill"
export HOME=$work/home XDG_STATE_HOME=$work/home/state XDG_CONFIG_HOME=$work/home/cfg

echo "== psdoomd refuses unsafe setups"
PSDOOMD_TOKEN=short "$PSDOOMD" -l 127.0.0.1:$port -psbackend cmd 2>/dev/null &&
	bad "accepted a short token" || ok "refuses a short token"
PSDOOMD_TOKEN=$token "$PSDOOMD" -psbackend cmd 2>/dev/null &&
	bad "listened without -l" || ok "refuses to start without -l ADDR:PORT"
PSDOOMD_TOKEN=$token "$PSDOOMD" -l 127.0.0.1:$port -psnet 10.0.0.1:1 2>/dev/null &&
	bad "relayed to another psdoomd" || ok "refuses to relay (-psnet)"

PSDOOMD_TOKEN=$token "$PSDOOMD" -l 127.0.0.1:$port -psbackend cmd 2>"$work/server.log" &
srv=$!
trap 'kill $srv 2>/dev/null; rm -rf "$work"' EXIT
sleep 1

talk() {  # talk <line>... : one connection, prints every answer line
	python3 -I - "$port" "$@" <<'PY'
import socket, sys
s = socket.create_connection(("127.0.0.1", int(sys.argv[1])), timeout=5)
f = s.makefile("rw", newline="\n")
for req in sys.argv[2:]:
    f.write(req + "\n"); f.flush()
    while True:
        line = f.readline()
        if not line:
            sys.exit(0)
        line = line.rstrip("\n")
        print(line)
        if req != "LIST" or line == ".":
            break
PY
}

echo "== protocol"
out=$(talk "HELLO 1 wrong-token-xyz" 2>/dev/null)
[ "$out" = "ERR auth" ] && ok "a wrong token gets ERR auth" || bad "wrong token: '$out'"
out=$(talk "HELLO 1 $token" LIST)
echo "$out" | grep -q "^OK psdoomd .* modern$" && ok "hello: OK, modern" || bad "hello: $out"
echo "$out" | grep -q "^4242 .* tester /bin/sleep$" && ok "LIST has the sleeps" || bad "LIST: $out"
echo "$out" | grep -q "^4244 .* dp .* /usr/sbin/sshd$" && ok "LIST flags sshd daemon+protected" || bad "LIST sshd: $out"
out=$(talk "HELLO 1 $token" LIST "RENICE 4243 7" "SIGNAL 4242 TERM" "SIGNAL 4244 KILL" "SIGNAL 9999 KILL" "NOPE" | tail -5 | tr '\n' ';')
[ "$out" = "OK;OK;ERR protected;ERR unknown;ERR syntax;" ] && ok "RENICE/SIGNAL answers" || bad "answers: $out"
sleep 1
grep -q "^renice 4243 7$" "$work/log" && ok "renice reached the helper" || bad "helper log: $(cat "$work/log")"
grep -q "^kill 4242 TERM$" "$work/log" && ok "TERM reached the helper" || bad "helper log: $(cat "$work/log")"
grep -q "4244" "$work/log" && bad "sshd reached the helper" || ok "protected sshd never reached the helper"
grep -q "action=signal sig=KILL pid=4244 .*result=protected" "$work/home/state/psdoom/actions.log" &&
	ok "psdoomd audits the refusal" || bad "audit log: $(cat "$work/home/state/psdoom/actions.log" 2>/dev/null)"

if [ -n "$ENGINE" ]; then
	echo "== $(basename "$ENGINE") -psnet -pstest sleep"
	printf 'tester 4242 /bin/sleep 0\ntester 4243 /bin/sleep 0\ntester 4244 /usr/sbin/sshd 1\n' > "$work/state"
	: > "$work/log"
	PSDOOM_NET_TOKEN=$token SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy timeout 120 "$ENGINE" \
		-iwad "$IWAD" -warp 1 1 -nosound -psnet 127.0.0.1:$port -pstest sleep > "$work/engine.log" 2>&1
	grep '^PSTEST' "$work/engine.log"
	grep -q "^PSTEST mode=modern targeted=2 removed=2 result=ok" "$work/engine.log" &&
		ok "the engine shot both sleeps through psdoomd" || bad "engine: $(tail -3 "$work/engine.log")"
fi
echo "== $pass passed, $fail failed"
[ "$fail" -eq 0 ]
