#!/bin/sh
# tests/helpers/run.sh - contrib helpers against fake docker/kubectl commands
#
# Checks the psdoom-ng protocol output and the kill/renice mapping; nothing
# real is touched (the fakes log what they were asked to do).
# SPDX-License-Identifier: GPL-2.0-or-later
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d)
log=$work/log
pass=0 fail=0
ok()  { echo "  ok   $*"; pass=$((pass + 1)); }
bad() { echo "  FAIL $*"; fail=$((fail + 1)); }
expect() { if [ "$2" = "$3" ]; then ok "$1"; else bad "$1: wanted '$3', got '$2'"; fi; }

mkdir "$work/bin"
cat > "$work/bin/docker" <<'F'
#!/bin/sh
echo "docker $*" >> "$FAKELOG"
case $1 in
ps) printf 'aaa111\nbbb222\n' ;;
inspect)
	shift; [ "$1" = -f ] && shift 2
	for id in "$@"; do
		case $id in aaa111) echo "4100 /web" ;; bbb222) echo "4200 /db" ;; esac
	done | { case "$*" in *aaa111*bbb222*) cat ;; *) cut -d' ' -f1 ;; esac; } ;;
esac
F
cat > "$work/bin/kubectl" <<'F'
#!/bin/sh
echo "kubectl $*" >> "$FAKELOG"
case $1 in
get) printf 'default nginx-1\nkube-system coredns-x\n' ;;
esac
F
chmod +x "$work/bin/docker" "$work/bin/kubectl"
export PATH="$work/bin:$PATH" FAKELOG="$log"

echo "== docker"
out=$(sh "$ROOT/contrib/helpers/docker.sh" ps)
expect "ps lists containers by main pid" "$out" "docker 4100 web 1
docker 4200 db 1"
PSDOOM_SIGNAL=TERM sh "$ROOT/contrib/helpers/docker.sh" kill 4200
grep -q "docker kill --signal TERM bbb222" "$log" && ok "kill maps pid 4200 to its container" || bad "kill mapping"
sh "$ROOT/contrib/helpers/docker.sh" renice 4100
grep -q "docker update --cpu-shares 128 aaa111" "$log" && ok "renice lowers cpu shares" || bad "renice mapping"

echo "== kubectl"
out=$(sh "$ROOT/contrib/helpers/kubectl.sh" ps)
n=$(echo "$out" | wc -l | tr -d ' ')
expect "ps lists two pods" "$n" 2
echo "$out" | grep -q "^kube-system [0-9]* coredns-x 1$" && ok "kube-system pods are daemons" || bad "daemon flag: $out"
id=$(echo "$out" | awk '$3 == "nginx-1" { print $2 }')
again=$(sh "$ROOT/contrib/helpers/kubectl.sh" ps | awk '$3 == "nginx-1" { print $2 }')
expect "pod ids are stable" "$again" "$id"
sh "$ROOT/contrib/helpers/kubectl.sh" kill "$id"
grep -q "kubectl delete pod -n default nginx-1 --wait=false" "$log" && ok "kill deletes the pod" || bad "kill mapping"

rm -rf "$work"
echo "== $pass passed, $fail failed"
[ "$fail" -eq 0 ]
