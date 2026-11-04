#!/usr/bin/env python3
# apply-zdoom-hooks.py - insert psDooM's hooks into a GZDoom/UZDoom tree
#
#   tools/apply-zdoom-hooks.py <engine source root>
#
# One-time helper for creating the engine's psDooM patch. The ZScript and
# the natives live in engines/common/zdoom/ (psdoom-plus tree); the patch
# only includes them, registers the handler and loads the custom level.
#
# Copyright (C) 2026 psdoom-plus contributors
# SPDX-License-Identifier: GPL-2.0-or-later
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])


def edit(name, pattern, repl):
    p = root / name
    s = p.read_text(encoding="utf-8")
    s2, n = re.subn(pattern, repl, s, count=1, flags=re.S)
    if n == 0:
        sys.exit(f"{name}: hook site not found: {pattern[:70]!r}")
    p.write_text(s2, encoding="utf-8")


edit("wadsrc/static/zscript.txt", r'(#include "zscript/events.zs"\n)',
     r'\1#include "zscript/psdoom.zs"\t// psdoom-plus: processes as monsters\n')
edit("wadsrc/static/mapinfo/doomcommon.txt", r"(\ngameinfo\n\{\n)",
     r'\1\tAddEventHandlers = "PsdoomHandler"\t// psdoom-plus\n')
# UZDoom's D_AddFile takes an extra "optional" flag; GZDoom 4.14's doesn't
src = (root / "src/d_main.cpp").read_text(encoding="utf-8")
extra = ", true" if "D_AddFile (allwads, lightswad, true, -1, GameConfig, true)" in src else ""
edit("src/d_main.cpp", r"(\n\t\t\tlastpos = len;\n\t\t\}\n\t\}\n)(\}\n)",
     r"""\1
	// psdoom-plus: psDooM's custom process level (psdoom1/2.wad)
	if (const char* pslevel = PS_CustomLevel(autoname))
		D_AddFile(allwads, pslevel, true, -1, GameConfig""" + extra + r""");
\2""")
edit("src/d_main.cpp", r"(\n//==========================================================================\n//\n// AddAutoloadFiles\n)",
     r"\nconst char* PS_CustomLevel(const char* autoname);\t// psdoom-plus: psdoom_zs.cpp\n\1")
print("hooked", root)
