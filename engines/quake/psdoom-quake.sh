#!/bin/sh
# psdoom-quake - Quake (QuakeSpasm) with every process as a monster
#
#   psdoom-quake [psDooM options] [QuakeSpasm options] [+commands]
#   psdoom-quake -psclassic +map e1m2
#   psdoom-quake -dedicated -pstest sleep          (the test harness)
#
# psDooM's options (-psclassic, -psuser NAME, -pstest GLOB, ...) work as in
# the Doom engines; Doom-only ones (-iwad, -warp, -file) are dropped, so
# tests/vm/doom-pstest.sh can drive this as it drives Doom. Without a +map,
# it starts E1M1 (registered Quake only: shareware Quake ignores +commands,
# so start a game from the menu or type `map e1m1` in the console).
#
# Environment: PSQUAKE_BASEDIR (the directory holding id1/pak0.pak),
# PSQUAKE_DEDICATED=1 (same as -dedicated), PSQUAKE_MAP (default e1m1).
#
# Copyright (C) 2026 psdoom-plus contributors
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
dedicated=${PSQUAKE_DEDICATED:-0}
map=

bin=
for b in "$here/quakespasm-psdoom" "$here/../lib/psdoom-plus/quake/quakespasm-psdoom"; do
	[ -x "$b" ] && bin=$b && break
done
[ -n "$bin" ] || { echo "psdoom-quake: no quakespasm-psdoom next to $0" >&2; exit 1; }

base=${PSQUAKE_BASEDIR:-}
if [ -z "$base" ]; then
	for d in "$here" "$HOME/.quakespasm" "$HOME/quake" /usr/share/games/quake \
		/usr/local/share/quake /usr/local/share/games/quake /usr/pkg/share/quake \
		/boot/system/data/quake /boot/home/config/non-packaged/data/quake; do
		[ -r "$d/id1/pak0.pak" ] && base=$d && break
	done
fi
[ -n "$base" ] || { echo "psdoom-quake: no Quake data (id1/pak0.pak) found (set PSQUAKE_BASEDIR)" >&2; exit 1; }

# keep everything but the Doom-only options, in order
n=$#
while [ "$n" -gt 0 ]; do
	a=$1; shift; n=$((n - 1))
	case $a in
	-iwad|-file) [ "$n" -gt 0 ] && { shift; n=$((n - 1)); } ;;
	-warp) [ "$n" -gt 1 ] && { shift 2; n=$((n - 2)); } ;;
	-dedicated) dedicated=1 ;;
	+map|+devmap) map=given; set -- "$@" "$a" ;;
	*) set -- "$@" "$a" ;;
	esac
done

# one slot keeps the server out of deathmatch, where QuakeC removes all
# monsters. (Shareware Quake ignores +commands, so a dedicated shareware
# server always runs the start map.)
[ "$dedicated" = 1 ] && set -- -dedicated 1 "$@"
[ -n "$map" ] || set -- "$@" +map "${PSQUAKE_MAP:-e1m1}"
exec "$bin" -basedir "$base" "$@"
