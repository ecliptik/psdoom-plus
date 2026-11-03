#!/usr/bin/env python3
# apply-doom-hooks.py - insert psDooM's hooks into a Chocolate-family Doom tree
#
#   tools/apply-doom-hooks.py <src/doom dir>
#
# A one-time helper for creating an engine's psDooM patch (commit the result
# on the engine's psdoom-plus branch, then `tools/psdev export <engine>`).
# Every hook is a call into the shared glue in engines/common/psdoom/.
#
# Copyright (C) 2026 psdoom-plus contributors
# SPDX-License-Identifier: GPL-2.0-or-later
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
done = []


def edit(name, pattern, repl, count=1, flags=re.S):
    p = root / name
    s = p.read_text(encoding="latin-1")
    s2, n = re.subn(pattern, repl, s, count=count, flags=flags)
    if n == 0:
        sys.exit(f"{name}: hook site not found: {pattern[:70]!r}")
    p.write_text(s2, encoding="latin-1")
    done.append(name)


def include_after(name, header):
    edit(name, r'(#include "' + re.escape(header) + r'"[^\n]*\n)',
         r'\1#include "p_psdoom.h"\n')


# things and vissprites carry their process
edit("p_mobj.h", r"(    struct mobj_s\*\s+tracer;[^\n]*\n)",
     r"""\1
    // psdoom-plus: this monster is a process
    long		ps_pid;		// 0 = an ordinary thing
    char		ps_label[8];	// last 7 chars of the process name
    boolean		ps_draw;	// draw the floating pid/name label
    boolean		ps_removing;	// being removed because the process is gone
    boolean		ps_protected;	// modern mode: shown but invulnerable
""")
edit("r_defs.h", r"(\n[ \t]*int[ \t]+mobjflags;[^\n]*\n)",
     r"""\1
    // psdoom-plus: label for pid monsters
    long		ps_pid;
    char		ps_label[8];
    boolean		ps_draw;
""")

# r_things.c: psDooM's floating label
include_after("r_things.c", "r_local.h")
edit("r_things.c", r"(\tif \(dc_yl <= dc_yh\)\n\t\{\n)",
     r"""\1	    // psdoom-plus: where the label goes (top of what was drawn)
	    if (!ps_col_drawn || dc_yl < ps_col_top)
		ps_col_top = dc_yl;
	    ps_col_drawn = true;
""")
edit("r_things.c", r"(sprtopscreen = centeryfrac - FixedMul\(dc_texturemid, ?spryscale\);\n)",
     r"""\1    ps_col_drawn = false;
    ps_col_top = viewheight;
""")
edit("r_things.c", r"(\n\tR_DrawMaskedColumn \(column\);\n    \}\n\n    colfunc = basecolfunc;\n)",
     r"""\1
    // psdoom-plus: psDooM's floating "pid / name" label
    if (vis->ps_draw && ps_col_drawn)
	PS_DrawLabel (vis->ps_pid, vis->ps_label, vis->x1, vis->x2,
		      ps_col_top, vis->scale);
""")
edit("r_things.c", r"(\n    vis->mobjflags = thing->flags;\n)",
     r"""\1    vis->ps_pid = thing->ps_pid;
    vis->ps_draw = thing->ps_pid && thing->ps_draw;
    memcpy (vis->ps_label, thing->ps_label, sizeof vis->ps_label);
""")
edit("r_things.c", r"(\n    vis = &avis;\n)",
     r"""\1    vis->ps_draw = false;	// psdoom-plus: no label on the weapon
""")

# p_inter.c: damage and death
include_after("p_inter.c", "p_inter.h")
edit("p_inter.c", r"(    if \(target->health <= 0\)\n\treturn;\n\n)(    if \( target->flags & MF_SKULLFLY \))",
     r"""\1    // psdoom-plus: pid monsters only take damage from players (psDooM)
    if (!PS_DamageAllowed (target, inflictor, source))
	return;

\2""")
edit("p_inter.c", r"(\n    target->health -= damage;[ \t]*\n)(    if \(target->health <= 0\)\n)",
     r"""\1    PS_Damaged (target);	// psdoom-plus: renice
\2""")
edit("p_inter.c", r"(    target->flags &= ~\(MF_SHOOTABLE\|MF_FLOAT\|MF_SKULLFLY\);\n)",
     r"""\1
    PS_Killed (source, target);	// psdoom-plus: kill the process
""")
edit("p_inter.c", r"(\n    // Drop stuff\.\n)",
     r"""
    // psdoom-plus: a vanished process leaves nothing behind
    if (PS_NoDrop (target))
	return;
\1""")

# p_mobj.c: removal, -respawnitems
include_after("p_mobj.c", "p_local.h")
edit("p_mobj.c", r"(void P_RemoveMobj \(mobj_t\* mobj\)\n\{\n)",
     r"""\1    PS_MobjRemoved (mobj);	// psdoom-plus
""")
edit("p_mobj.c", r"(    // only respawn items in deathmatch\n(?:    //[^\n]*\n)*    if \()(deathmatch != 2[^)\n]*)(\))",
     r"\1(\2) && (!respawnitems || demorecording || demoplayback)\3")

# p_tick.c, p_setup.c
include_after("p_tick.c", "p_local.h")
edit("p_tick.c", r"(    P_RespawnSpecials \(\);\n)",
     r"""\1    PS_Ticker ();		// psdoom-plus: processes come and go
""")
include_after("p_setup.c", "p_local.h")
edit("p_setup.c", r"(    S_Start \(\);[ \t]*\n)",
     r"""\1    PS_LevelEnd ();		// psdoom-plus: forget the old level's monsters
""")
edit("p_setup.c", r"(\tR_PrecacheLevel \(\);\n)",
     r"""\1
    PS_LevelStart ();		// psdoom-plus: spawn the processes
""")

# p_saveg.c: pid monsters aren't saved; resync after a load
include_after("p_saveg.c", "p_local.h")
edit("p_saveg.c", r"(\tif \(th->function\.acp1 == \(actionf_p1\)P_MobjThinker\)\n\t\{\n)",
     r"""\1	    if (PS_SkipSave ((mobj_t *)th))
		continue;	// psdoom-plus: processes are re-read on load
""")
edit("p_saveg.c", r"(\t  case tc_end:\n)",
     r"""\1	    PS_SaveLoaded ();	// psdoom-plus
""")

# g_game.c: -nomonsters. persists across new games
include_after("g_game.c", "g_game.h")
edit("g_game.c", r"(\n    if \(skill == sk_nightmare \|\| respawnparm \))",
     r"""
    if (nomonstersperiod)
	nomonsters = true;	// psdoom-plus: -nomonsters. (psDooM)
\1""")

# d_main.c: the custom level and libpsd
include_after("d_main.c", "d_main.h")
edit("d_main.c", r"(\n    D_IdentifyVersion\(\);\n)",
     r"""\1    PS_AddCustomLevel ();	// psdoom-plus: psdoom1/2.wad
""")
edit("d_main.c", r"(\n    I_InitMusic\(\);\n)",
     r"""\1    PS_Init ();		// psdoom-plus: open libpsd
""")

# m_menu.c: the glue draws labels with the menu font
edit("m_menu.c", r"\nstatic void M_WriteText\(int x, int y, const char \*string\);",
     "\nvoid M_WriteText(int x, int y, const char *string);\t// psdoom-plus: labels")
edit("m_menu.c", r"\nstatic void M_StartMessage\(", "\nvoid M_StartMessage(", flags=0) \
    if "static void M_StartMessage(" in (root / "m_menu.c").read_text(encoding="latin-1") else None

print("hooked:", " ".join(sorted(set(done))))
