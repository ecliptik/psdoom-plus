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
		for d in "" /usr/lib/ioquake3/ /usr/local/bin/ /usr/local/lib/ioquake3/ /usr/pkg/lib/ioquake3/ /usr/games/; do
			if [ -z "$d" ]; then
				p=$(command -v "$e" 2>/dev/null) && [ -x "$p" ] && { echo "$p"; return; }
			elif [ -x "$d$e" ]; then
				echo "$d$e"; return
			fi
		done
	done
}
# FreeBSD's ioquake3 is 1.36, which can only run baseq3; FreeBSD's and
# OpenBSD's OpenArena packages ship OpenArena's own ioquake3-based engine
# (NetBSD's run a newer ioquake3, as Debian's do)
case $(uname -s) in
*BSD) ded="oa_ded openarena-server ioq3ded ioq3ded.x86_64" cli="openarena ioquake3 ioquake3.x86_64" ;;
*)    ded="ioq3ded ioq3ded.x86_64 oa_ded" cli="ioquake3 ioquake3.x86_64" ;;
esac
if [ -n "${PSQ3_ENGINE:-}" ]; then
	engine=$PSQ3_ENGINE
elif [ "$dedicated" = 1 ]; then
	# shellcheck disable=SC2086
	engine=$(find_engine $ded)
else
	# shellcheck disable=SC2086
	engine=$(find_engine $cli)
fi
[ -n "$engine" ] || { echo "psdoom-q3: no ioquake3 engine found (set PSQ3_ENGINE)" >&2; exit 1; }

base=${PSQ3_BASEPATH:-}
if [ -z "$base" ]; then
	for d in /usr/lib/openarena /usr/lib/openarena-server /usr/share/games/openarena \
		/usr/local/share/openarena /usr/local/share/games/openarena /usr/pkg/lib/openarena \
		"$HOME/openarena"; do
		ls "$d"/baseoa/pak0.pk3 >/dev/null 2>&1 && base=$d && break
	done
fi
[ -n "$base" ] || { echo "psdoom-q3: no OpenArena data found (set PSQ3_BASEPATH)" >&2; exit 1; }

home=${PSQ3_HOME:-$HOME/.psdoom-q3}
mkdir -p "$home/psdoom" "$home/base/baseoa" || exit 1
cp -f "$mod" "$home/psdoom/" || exit 1
# ioquake3 names amd64 x86_64; OpenArena's engine (the BSDs) says amd64
case ${mod##*/} in
qagamex86_64.so) cp -f "$mod" "$home/psdoom/qagameamd64.so" ;;
qagamearm64.so)  cp -f "$mod" "$home/psdoom/qagameaarch64.so" ;;
esac

# ioquake3 takes a pak0.pk3 in the base game for id's Quake 3 data and
# checks it against id's checksums (only some builds, e.g. Debian's, know
# better). Link OpenArena's paks with pak0 renamed, in the same load order:
# ioquake3 then runs OpenArena as the standalone game it is.
rm -f "$home/base/baseoa/"*.pk3
for f in "$base"/baseoa/*.pk3; do
	n=${f##*/}
	[ "$n" = pak0.pk3 ] && n=pak0-oa.pk3
	ln -s "$f" "$home/base/baseoa/$n" || exit 1
done

set -- +set com_basegame baseoa +set fs_basepath "$home/base" +set fs_homepath "$home" \
	+set fs_game psdoom +set vm_game 0 +set sv_pure 0 \
	+set com_protocol 71 +set com_legacyprotocol 71 \
	+set sv_maxclients 24 +set bot_enable 1 +set g_gametype 0 \
	+set ps_args "${psargs# }" "$@"
[ -n "$map" ] || set -- "$@" +map "${PSQ3_MAP:-oa_minia}"
[ "$dedicated" = 1 ] && set -- +set dedicated 1 +set ttycon 0 "$@"
exec "$engine" "$@"
