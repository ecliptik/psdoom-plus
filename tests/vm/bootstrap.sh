#!/bin/sh
# bootstrap.sh - install build dependencies inside a test guest
#
# Guests are reverted to a pristine install, so this runs (idempotently)
# before every test session. Copyright (C) 2026 psdoom-plus contributors
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu

need() { command -v "$1" >/dev/null 2>&1; }

if need cmake && need cc; then
	echo "bootstrap: cmake and cc present"
	exit 0
fi

os=$(uname -s)
case $os in
Linux)
	sudo DEBIAN_FRONTEND=noninteractive apt-get update -qq
	sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq build-essential cmake >/dev/null ;;
FreeBSD)
	sudo env ASSUME_ALWAYS_YES=yes pkg install -q cmake ;;
OpenBSD)
	doas pkg_add -I cmake ;;
NetBSD)
	sudo pkgin -y install cmake ;;
Haiku)
	pkgman install -y cmake_x86_64 gcc_x86_64 2>/dev/null || pkgman install -y cmake ;;
*)
	echo "bootstrap: don't know how to install on $os" >&2
	exit 1 ;;
esac
echo "bootstrap: done ($os)"
