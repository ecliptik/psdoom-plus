#!/bin/sh
# xwrap.sh <command...> - run a command on a private virtual X display
#
# For the GL engines (GZDoom/UZDoom), which can't use SDL's dummy video
# driver. Portable stand-in for Debian's xvfb-run; uses Mesa's software GL.
# SPDX-License-Identifier: GPL-2.0-or-later
set -u
if [ "$(uname -s)" = Haiku ]; then
	exec "$@"			# app_server is always there
fi
n=99
while [ -e "/tmp/.X$n-lock" ]; do n=$((n + 1)); done
Xvfb ":$n" -screen 0 1024x768x24 -nolisten tcp >/dev/null 2>&1 &
xpid=$!
i=0
while [ ! -e "/tmp/.X11-unix/X$n" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done
DISPLAY=":$n" LIBGL_ALWAYS_SOFTWARE=1 "$@"
rc=$?
kill "$xpid" 2>/dev/null
wait "$xpid" 2>/dev/null
exit $rc
