#!/bin/sh
# kubectl.sh - Kubernetes pods as psDooM monsters (example helper, kubedoom-style)
#
#   export PSDOOM_NG_PS_CMD="sh contrib/helpers/kubectl.sh ps"
#   export PSDOOM_NG_RENICE_CMD="true"
#   export PSDOOM_NG_KILL_CMD="sh contrib/helpers/kubectl.sh kill"
#   build/og/psdoom-og -psbackend cmd -psallusers
#
# Pods have no pid, so each gets a stable number from its namespace/name.
# The namespace is the "user" (so -psuser default limits to one namespace);
# kube-system pods are daemons. Killing the monster deletes the pod.
# NAMESPACES="default web" limits the namespaces (default: all).
# A sample, not a supported backend: for the real thing, see kubedoom.
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

pods() {  # "<namespace> <name>" per pod
	if [ -n "${NAMESPACES:-}" ]; then
		for ns in $NAMESPACES; do
			kubectl get pods -n "$ns" --no-headers -o custom-columns=NS:.metadata.namespace,NAME:.metadata.name
		done
	else
		kubectl get pods -A --no-headers -o custom-columns=NS:.metadata.namespace,NAME:.metadata.name
	fi
}

idof() {  # stable positive id for "<namespace>/<name>"
	printf '%s' "$1" | cksum | awk '{ print $1 % 2000000000 + 1 }'
}

case ${1:-} in
ps)
	pods | while read -r ns name; do
		daemon=0
		[ "$ns" = kube-system ] && daemon=1
		echo "$ns $(idof "$ns/$name") $name $daemon"
	done ;;
kill)
	pods | while read -r ns name; do
		if [ "$(idof "$ns/$name")" = "$2" ]; then
			kubectl delete pod -n "$ns" "$name" --wait=false >/dev/null
			break
		fi
	done ;;
*)
	echo "usage: $0 ps | kill <id>" >&2
	exit 1 ;;
esac
