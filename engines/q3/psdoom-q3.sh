#!/bin/sh
# psdoom-q3 - OpenArena on ioquake3, with every process as a bot
#
#   psdoom-q3 [psDooM options] [+ioquake3 commands]
#   psdoom-q3 -psclassic +map oa_minia
#   psdoom-q3 -dedicated -pstest sleep          (the test harness)
#
# psDooM's options (-psclassic, -psuser NAME, -pstest GLOB, ...) go to the
# game as the ps_args cvar; Doom-only ones (-iwad, -warp, -nosound) are
# ignored, so tests/vm/doom-pstest.sh can drive this as it drives Doom.
#
# Environment: PSQ3_DEDICATED=1 (same as -dedicated), PSQ3_ENGINE (ioquake3 or ioq3ded binary), PSQ3_BASEPATH (the
# directory holding baseoa/ with OpenArena 0.8.8's paks), PSQ3_HOME (default
# ~/.psdoom-q3), PSQ3_MAP (default oa_minia).
#
# Copyright (C) 2026 psdoom-plus contributors
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
dedicated=${PSQ3_DEDICATED:-0}
psargs=
map=

while [ $# -gt 0 ]; do
	case $1 in
	+*) break ;;
	-dedicated) dedicated=1 ;;
	-iwad|-file) [ $# -ge 2 ] && shift ;;
	-warp) [ $# -ge 3 ] && shift 2 ;;
	-nosound) ;;
	*) psargs="$psargs $1" ;;
	esac
	shift
done
for a in "$@"; do
	case $a in +map|+devmap|+spmap) map=given ;; esac
done

# the qagame module: built next to this script, or installed
mod=
for d in "$here/psdoom" "$here/../lib/psdoom-plus/q3/psdoom"; do
	for f in "$d"/qagame*.so "$d"/qagame*.dylib; do
		[ -f "$f" ] && mod=$f && break 2
	done
done
[ -n "$mod" ] || { echo "psdoom-q3: no qagame module next to $0" >&2; exit 1; }

find_engine() {
	for e in "$@"; do
		for d in "" /usr/lib/ioquake3/ /usr/local/bin/ /usr/local/lib/ioquake3/ /usr/games/; do
			if [ -z "$d" ]; then
				p=$(command -v "$e" 2>/dev/null) && [ -x "$p" ] && { echo "$p"; return; }
			elif [ -x "$d$e" ]; then
				echo "$d$e"; return
			fi
		done
	done
}
if [ -n "${PSQ3_ENGINE:-}" ]; then
	engine=$PSQ3_ENGINE
elif [ "$dedicated" = 1 ]; then
	engine=$(find_engine ioq3ded ioq3ded.x86_64 ioquake3-server)
else
	engine=$(find_engine ioquake3 ioquake3.x86_64)
fi
[ -n "$engine" ] || { echo "psdoom-q3: no ioquake3 engine found (set PSQ3_ENGINE)" >&2; exit 1; }

base=${PSQ3_BASEPATH:-}
if [ -z "$base" ]; then
	for d in /usr/lib/openarena /usr/lib/openarena-server /usr/share/games/openarena \
		/usr/local/share/openarena /usr/local/share/games/openarena "$HOME/openarena"; do
		ls "$d"/baseoa/pak0.pk3 >/dev/null 2>&1 && base=$d && break
	done
fi
[ -n "$base" ] || { echo "psdoom-q3: no OpenArena data found (set PSQ3_BASEPATH)" >&2; exit 1; }

home=${PSQ3_HOME:-$HOME/.psdoom-q3}
mkdir -p "$home/psdoom" || exit 1
cp -f "$mod" "$home/psdoom/" || exit 1

set -- +set com_basegame baseoa +set fs_basepath "$base" +set fs_homepath "$home" \
	+set fs_game psdoom +set vm_game 0 +set sv_pure 0 \
	+set com_protocol 71 +set com_legacyprotocol 71 \
	+set sv_maxclients 24 +set bot_enable 1 +set g_gametype 0 \
	+set ps_args "${psargs# }" "$@"
[ -n "$map" ] || set -- "$@" +map "${PSQ3_MAP:-oa_minia}"
[ "$dedicated" = 1 ] && set -- +set dedicated 1 +set ttycon 0 "$@"
exec "$engine" "$@"
