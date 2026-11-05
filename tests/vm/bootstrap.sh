#!/bin/sh
# bootstrap.sh - install build dependencies inside a test guest
#
#   bootstrap.sh            toolchain + cmake (libpsd)
#   bootstrap.sh engines    ... plus SDL3 and Freedoom (the engines)
#   bootstrap.sh dos        ... plus DOSBox-X and Freedoom (psdoomd vs PSDOOM.EXE)
#
# Guests are reverted to a pristine install, so this runs (idempotently)
# before every test session. Copyright (C) 2026 psdoom-plus contributors
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu

need() { command -v "$1" >/dev/null 2>&1; }
want_engines=0
want_zdoom=0            # GZDoom/UZDoom: C++, GL, a virtual X display
want_woof=0             # Woof!: OpenAL, SDL3 >= 3.4 (built from git if older)
want_dsda=0             # DSDA-Doom: SDL2_mixer, libsndfile, libzip, GL/GLU
want_dos=0              # DOS: DOSBox-X runs PSDOOM.EXE against psdoomd
want_q3=0               # Quake 3: ioquake3's dedicated server + OpenArena 0.8.8
[ "${1:-}" = "engines" ] && want_engines=1
[ "${1:-}" = "dos" ] && want_dos=1
case "${2:-}" in
gzdoom|uzdoom) want_zdoom=1 ;;
woof) want_woof=1 ;;
dsda) want_dsda=1 ;;
q3) want_q3=1 ;;
esac

have_all() {
	need cmake && need cc || return 1
	if [ $want_dos -eq 1 ]; then
		need dosbox-x && [ -r /usr/share/games/doom/freedoom2.wad ] || return 1
	fi
	[ $want_engines -eq 0 ] && return 0
	need pkg-config || need pkgconf || return 1
	pkg-config --exists sdl3 sdl2 2>/dev/null || return 1
	if [ $want_woof -eq 1 ]; then
		need git && need c++ && pkg-config --exists openal 2>/dev/null || return 1
	fi
	if [ $want_q3 -eq 1 ]; then
		{ need ioq3ded || [ -x /usr/lib/ioquake3/ioq3ded ] || [ -x /usr/local/bin/ioq3ded ]; } &&
			ls /usr/lib/openarena-server/baseoa/pak0.pk3 /usr/local/share/openarena/baseoa/pak0.pk3 \
				2>/dev/null | grep -q . || return 1
	fi
	if [ $want_dsda -eq 1 ]; then
		need c++ && pkg-config --exists SDL2_mixer sndfile libzip zlib glu 2>/dev/null || return 1
	fi
	[ $want_zdoom -eq 0 ] && return 0
	need git && need c++ || return 1
	pkg-config --exists vpx glib-2.0 openal 2>/dev/null || return 1
	[ "$(uname -s)" = Haiku ] && return 0
	need Xvfb && pkg-config --exists x11 2>/dev/null
}

if have_all; then
	echo "bootstrap: dependencies present"
	exit 0
fi

os=$(uname -s)
case $os in
Linux)
	pkgs="build-essential cmake"
	[ $want_engines -eq 1 ] && pkgs="$pkgs pkg-config libsdl3-dev libsdl2-dev freedoom"
	[ $want_zdoom -eq 1 ] && pkgs="$pkgs git libvpx-dev libopenal-dev zlib1g-dev libbz2-dev libjpeg-dev xvfb libgl1-mesa-dri"
	[ $want_woof -eq 1 ] && pkgs="$pkgs git libopenal-dev libasound2-dev"
	[ $want_dsda -eq 1 ] && pkgs="$pkgs libsdl2-mixer-dev libsndfile1-dev libzip-dev zlib1g-dev libgl-dev libglu1-mesa-dev"
	[ $want_dos -eq 1 ] && pkgs="$pkgs dosbox-x freedoom"
	[ $want_q3 -eq 1 ] && pkgs="$pkgs openarena-server"
	sudo apt-get update -qq
	# shellcheck disable=SC2086
	sudo apt-get install -y -qq $pkgs >/dev/null
	# the package starts a server on the port the tests use
	[ $want_q3 -eq 1 ] && { sudo systemctl disable --now openarena-server >/dev/null 2>&1 || true; } ;;
FreeBSD)
	pkgs="cmake"
	[ $want_engines -eq 1 ] && pkgs="$pkgs pkgconf sdl3 sdl2"
	[ $want_zdoom -eq 1 ] && pkgs="$pkgs git glib libX11 libvpx openal-soft xorg-vfbserver mesa-dri"
	[ $want_woof -eq 1 ] && pkgs="$pkgs git openal-soft"
	[ $want_dsda -eq 1 ] && pkgs="$pkgs sdl2_mixer libsndfile libzip libglvnd libGLU"
	# shellcheck disable=SC2086
	sudo env ASSUME_ALWAYS_YES=yes pkg install -q $pkgs ;;
OpenBSD)
	pkgs="cmake"
	if [ $want_engines -eq 1 ]; then
		pkgs="$pkgs sdl3 sdl2"
		# SDL3 links against xcb from the X11 base set, which a minimal
		# install leaves out: add the xbase set (disposable guests only)
		if ! ls /usr/X11R6/lib/libxcb.so.* >/dev/null 2>&1; then
			v=$(uname -r | tr -d .)
			set_url="https://cdn.openbsd.org/pub/OpenBSD/$(uname -r)/$(uname -m)/xbase$v.tgz"
			echo "bootstrap: installing the X11 base set ($set_url)"
			ftp -o /tmp/xbase.tgz "$set_url"
			doas tar -C / -xzphf /tmp/xbase.tgz
			rm -f /tmp/xbase.tgz
			doas ldconfig -m /usr/X11R6/lib
		fi
	fi
	# shellcheck disable=SC2086
	doas pkg_add -I $pkgs ;;
NetBSD)
	pkgs="cmake"
	[ $want_engines -eq 1 ] && pkgs="$pkgs pkgconf SDL3 SDL2"
	# no pkgin in a minimal install: pkg_add from the binary package repo
	repo="https://cdn.NetBSD.org/pub/pkgsrc/packages/NetBSD/$(uname -m)/$(uname -r | cut -d_ -f1)/All"
	# shellcheck disable=SC2086
	sudo env PKG_PATH="$repo" /usr/sbin/pkg_add -U $pkgs ;;
Haiku)
	pkgs="cmake"
	[ $want_engines -eq 1 ] && pkgs="$pkgs libsdl3_devel libsdl2_devel"
	[ $want_zdoom -eq 1 ] && pkgs="$pkgs git libvpx_devel openal_devel mesa_devel glib2_devel"
	[ $want_woof -eq 1 ] && pkgs="$pkgs git openal_devel"
	[ $want_dsda -eq 1 ] && pkgs="$pkgs sdl2_mixer_devel libsndfile_devel libzip_devel zlib_devel mesa_devel glu_devel"
	# The tv-haiku image carries an inactive zlib_devel .hpkg in
	# /boot/system/packages, which makes any transaction that pulls it in
	# fail ("... already exists"): move such a stale file aside, retry once
	# shellcheck disable=SC2086
	pkgman install -y $pkgs 2>&1 | tee /tmp/bootstrap-pkgman.log
	stale=$(sed -n 's|.*Failed to move file "[^"]*/\([^/"]*\.hpkg\)" to "/boot/system/packages".*|\1|p' /tmp/bootstrap-pkgman.log | head -1)
	if [ -n "$stale" ] && [ -f "/boot/system/packages/$stale" ]; then
		echo "bootstrap: moving stale /boot/system/packages/$stale aside"
		mkdir -p "$HOME/stale-hpkg"
		mv "/boot/system/packages/$stale" "$HOME/stale-hpkg/"
		rm -rf /boot/system/packages/administrative/transaction-*
		# shellcheck disable=SC2086
		pkgman install -y $pkgs
	fi ;;
*)
	echo "bootstrap: don't know how to install on $os" >&2
	exit 1 ;;
esac
echo "bootstrap: done ($os)"
