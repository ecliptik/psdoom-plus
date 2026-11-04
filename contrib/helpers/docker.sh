#!/bin/sh
# docker.sh - Docker/Podman containers as psDooM monsters (example helper)
#
# Speaks psdoom-ng's hook protocol, so any psdoom-plus engine (and psdoom-ng)
# can use it:
#
#   export PSDOOM_NG_PS_CMD="sh contrib/helpers/docker.sh ps"
#   export PSDOOM_NG_RENICE_CMD="sh contrib/helpers/docker.sh renice"
#   export PSDOOM_NG_KILL_CMD="sh contrib/helpers/docker.sh kill"
#   build/og/psdoom-og -psbackend cmd -psallusers
#
# A container is shown by its main process id (so labels carry a real pid)
# and its name; containers are daemons (Demons). Killing the monster stops
# the container (docker kill; PSDOOM_SIGNAL=TERM in modern mode, KILL in
# classic). Wounding lowers its CPU shares.
#
# DOCKER=podman to use Podman. A sample, not a supported backend.
# SPDX-License-Identifier: GPL-2.0-or-later
set -u
DOCKER=${DOCKER:-docker}

# id of the running container whose main process is $1
container_of() {
	$DOCKER ps -q | while read -r id; do
		[ "$($DOCKER inspect -f '{{.State.Pid}}' "$id")" = "$1" ] && echo "$id" && break
	done
}

case ${1:-} in
ps)
	# <user> <pid> <name> <is_daemon>
	$DOCKER ps -q | xargs -r $DOCKER inspect -f '{{.State.Pid}} {{.Name}}' |
		while read -r pid name; do
			echo "docker $pid ${name#/} 1"
		done ;;
renice)
	id=$(container_of "$2")
	[ -n "$id" ] && $DOCKER update --cpu-shares 128 "$id" >/dev/null ;;
kill)
	id=$(container_of "$2")
	[ -n "$id" ] && $DOCKER kill --signal "${PSDOOM_SIGNAL:-KILL}" "$id" >/dev/null ;;
*)
	echo "usage: $0 ps | renice <pid> | kill <pid>" >&2
	exit 1 ;;
esac
