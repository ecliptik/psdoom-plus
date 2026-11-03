#!/bin/sh
# bootstrap.sh - install build dependencies inside a test guest
#
#   bootstrap.sh            toolchain + cmake (libpsd)
#   bootstrap.sh engines    ... plus SDL3 and Freedoom (the engines)
#
# Guests are reverted to a pristine install, so this runs (idempotently)
# before every test session. Copyright (C) 2026 psdoom-plus contributors
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu

need() { command -v "$1" >/dev/null 2>&1; }
want_engines=0
[ "${1:-}" = "engines" ] && want_engines=1

have_all() {
	need cmake && need cc || return 1
	[ $want_engines -eq 0 ] && return 0
	need pkg-config || need pkgconf || return 1
	pkg-config --exists sdl3 2>/dev/null
}

if have_all; then
	echo "bootstrap: dependencies present"
	exit 0
fi

os=$(uname -s)
case $os in
Linux)
	pkgs="build-essential cmake"
	[ $want_engines -eq 1 ] && pkgs="$pkgs pkg-config libsdl3-dev freedoom"
	sudo apt-get update -qq
	# shellcheck disable=SC2086
	sudo apt-get install -y -qq $pkgs >/dev/null ;;
FreeBSD)
	pkgs="cmake"
	[ $want_engines -eq 1 ] && pkgs="$pkgs pkgconf sdl3"
	# shellcheck disable=SC2086
	sudo env ASSUME_ALWAYS_YES=yes pkg install -q $pkgs ;;
OpenBSD)
	pkgs="cmake"
	if [ $want_engines -eq 1 ]; then
		pkgs="$pkgs sdl3"
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
	[ $want_engines -eq 1 ] && pkgs="$pkgs pkgconf SDL3"
	# no pkgin in a minimal install: pkg_add from the binary package repo
	repo="https://cdn.NetBSD.org/pub/pkgsrc/packages/NetBSD/$(uname -m)/$(uname -r | cut -d_ -f1)/All"
	# shellcheck disable=SC2086
	sudo env PKG_PATH="$repo" /usr/sbin/pkg_add -U $pkgs ;;
Haiku)
	pkgs="cmake"
	[ $want_engines -eq 1 ] && pkgs="$pkgs libsdl3_devel"
	# shellcheck disable=SC2086
	pkgman install -y $pkgs ;;
*)
	echo "bootstrap: don't know how to install on $os" >&2
	exit 1 ;;
esac
echo "bootstrap: done ($os)"
