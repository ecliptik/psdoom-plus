#!/usr/bin/env python3
# apply-dsda-hooks.py - insert psDooM's hooks into a DSDA-Doom tree
#
#   tools/apply-dsda-hooks.py <dsda-doom prboom2/src dir>
#
# The DSDA-Doom (PrBoom+) counterpart of apply-doom-hooks.py: a one-time
# helper for creating engines/dsda's psDooM patch (commit the result on the
# psdoom-plus branch, then `tools/psdev export dsda`). DSDA-Doom saves and
# key frames copy whole mobj_t structs, so pid monsters stay in them and
# PS_Resync() re-adopts them after a restore. Labels are drawn by the
# software renderer only.
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


def include(name):
    # after the file's first local include
    edit(name, r'(\n#include "[^"]+"[^\n]*\n)', r'\1#include "p_psdoom.h"\n')


# things and vissprites carry their process
edit("p_mobj.h", r"(    struct mobj_s\*\s+tracer;[^\n]*\n)",
     r"""\1
    // psdoom-plus: this monster is a process
    long                ps_pid;         // 0 = an ordinary thing
    char                ps_label[8];    // last 7 chars of the process name
    dboolean            ps_draw;        // draw the floating pid/name label
    dboolean            ps_removing;    // being removed because the process is gone
    dboolean            ps_protected;   // modern mode: shown but invulnerable
""")
edit("r_defs.h", r"(\n[ \t]*uint64_t[ \t]+mobjflags;[^\n]*\n)",
     r"""\1
  // psdoom-plus: label for pid monsters
  long ps_pid;
  char ps_label[8];
  dboolean ps_draw;
""")

# r_things.c: psDooM's floating label (software renderer)
include("r_things.c")
edit("r_things.c", r"(      if \(dcvars->yl >= 0 && dcvars->yl <= dcvars->yh && dcvars->yh < viewheight\)\n        \{\n)",
     r"""\1          // psdoom-plus: where the label goes (top of what was drawn)
          if (!ps_col_drawn || dcvars->yl < ps_col_top)
            ps_col_top = dcvars->yl;
          ps_col_drawn = true;
""")
edit("r_things.c", r"(  sprtopscreen = centeryfrac - FixedMul\(dcvars\.texturemid,spryscale\);\n)",
     r"""\1  ps_col_drawn = false;
  ps_col_top = viewheight;
""")
edit("r_things.c", r"(        R_GetPatchColumnClamped\(patch, texturecolumn\+1\)\n      \);\n    \}\n)(\})",
     r"""\1
  // psdoom-plus: psDooM's floating "pid / name" label
  if (vis->ps_draw && ps_col_drawn)
    PS_DrawLabel(vis->ps_pid, vis->ps_label, vis->x1, vis->x2,
                 ps_col_top, vis->scale);
\2""")
edit("r_things.c", r"(\n  vis->mobjflags = thing->flags;\n)",
     r"""\1  vis->ps_pid = thing->ps_pid;
  vis->ps_draw = thing->ps_pid && thing->ps_draw;
  memcpy(vis->ps_label, thing->ps_label, sizeof vis->ps_label);
""")
edit("r_things.c", r"(\n  vis = &avis;\n)",
     r"""\1  vis->ps_draw = false;  // psdoom-plus: no label on the weapon
""")

# p_inter.c: damage and death
include("p_inter.c")
edit("p_inter.c", r"(\n)(  if \(target->flags & MF_SKULLFLY\)\n  \{\n    if \(heretic && target->type == HERETIC_MT_MINOTAUR\) return;)",
     r"""\1  // psdoom-plus: pid monsters only take damage from players (psDooM)
  if (!PS_DamageAllowed(target, inflictor, source))
    return;

\2""")
edit("p_inter.c", r"(\n  // do the damage\n  target->health -= damage;\n)",
     r"""\1  PS_Damaged(target);  // psdoom-plus: renice
""")
edit("p_inter.c", r"(  target->flags &= ~\(MF_SHOOTABLE\|MF_FLOAT\|MF_SKULLFLY\);\n)",
     r"""\1
  PS_Killed(source, target);  // psdoom-plus: kill the process
""")
edit("p_inter.c", r"(\n  // Drop stuff\.\n)",
     r"""
  // psdoom-plus: a vanished process leaves nothing behind
  if (PS_NoDrop(target))
    return;
\1""")
edit("p_inter.c", r"(\n//\n// P_DamageMobj\n)",
     r"""
// psdoom-plus: P_KillMobj for the shared glue
void PS_P_KillMobj(mobj_t *source, mobj_t *target)
{
  P_KillMobj(source, target);
}
\1""")

# p_mobj.c: removal, -respawnitems
include("p_mobj.c")
edit("p_mobj.c", r"(void P_RemoveMobj \(mobj_t\* mobj\)\n\{\n)",
     r"""\1  PS_MobjRemoved(mobj);  // psdoom-plus
""")
edit("p_mobj.c", r"(  // only respawn items in deathmatch\n\n  if \()(deathmatch != 2)(\))",
     r"\1\2 && (!respawnitems || demorecording || demoplayback)\3")

# p_tick.c, p_setup.c
include("p_tick.c")
edit("p_tick.c", r"(\n    P_RespawnSpecials\(\);\n)",
     r"""\1    PS_Ticker();  // psdoom-plus: processes come and go
""")
include("p_setup.c")
edit("p_setup.c", r"(\n  S_Start\(\);\n)",
     r"""\1  PS_LevelEnd();  // psdoom-plus: forget the old level's monsters
""")
edit("p_setup.c", r"(\n  R_PrecacheLevel\(\);\n)",
     r"""\1
  PS_LevelStart();  // psdoom-plus: spawn the processes
""")

# dsda/save.c: saves and key frames keep pid monsters; re-adopt them
include("dsda/save.c")
edit("dsda/save.c", r"(\n  P_UnArchiveMap\(\);\n  P_MapEnd\(\);\n)",
     r"""\1  PS_Resync();  // psdoom-plus: re-adopt the pid monsters
""")

# g_game.c: -nomonsters. persists across new games
include("g_game.c")
edit("g_game.c", r"(\n  nomonsters = clnomonsters;\n)",
     r"""\1  if (nomonstersperiod)
    nomonsters = true;  // psdoom-plus: -nomonsters. (psDooM)
""")

# d_main.c: the custom level and libpsd
include("d_main.c")
edit("d_main.c", r"(\n  IdentifyVersion\(\); // Get IWAD\n)",
     r"""\1  PS_AddCustomLevel();  // psdoom-plus: psdoom1/2.wad
""")
edit("d_main.c", r"(\n  S_Init\(\);\n)",
     r"""\1  PS_Init();  // psdoom-plus: open libpsd
""")

# m_menu.c: the glue draws labels with the menu font and asks before
# killing root's processes
edit("m_menu.c", r"\nstatic void M_WriteText( ?\()", r"\nvoid M_WriteText\1", count=2, flags=0)
edit("m_menu.c", r"\nstatic void M_StartMessage( ?\()", r"\nvoid M_StartMessage\1", count=2, flags=0)

# dsda/args.c: DSDA-Doom rejects options it doesn't know
include("dsda/args.c")
edit("dsda/args.c", r"(    if \(dsda_argv\[argv_i\]\[0\] == '-' && !is_integer\) \{\n)",
     r"""\1      if (PS_IsOption(dsda_argv[argv_i]))
        continue;  // psdoom-plus: libpsd and the glue read these

""")

print("hooked:", " ".join(sorted(set(done))))
