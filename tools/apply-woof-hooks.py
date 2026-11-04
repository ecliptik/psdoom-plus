#!/usr/bin/env python3
# apply-woof-hooks.py - insert psDooM's hooks into a Woof! tree
#
#   tools/apply-woof-hooks.py <woof src/ dir>
#
# The Woof! (Boom/MBF) counterpart of apply-doom-hooks.py: a one-time helper
# for creating engines/woof's psDooM patch (commit the result on the
# psdoom-plus branch, then `tools/psdev export woof`). Woof! saves and
# rewinds with keyframes that hold the whole thing list, so pid monsters are
# kept in them (with their pid) and re-adopted by PS_Resync() afterwards,
# instead of being left out as in the Chocolate family.
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
    boolean             ps_draw;        // draw the floating pid/name label
    boolean             ps_removing;    // being removed because the process is gone
    boolean             ps_protected;   // modern mode: shown but invulnerable
""")
edit("r_defs.h", r"(\n[ \t]*int[ \t]+mobjflags;[^\n]*\n)",
     r"""\1
  // psdoom-plus: label for pid monsters
  long ps_pid;
  char ps_label[8];
  boolean ps_draw;
""")

# r_things.c: psDooM's floating label
include("r_things.c")
edit("r_things.c", r"(      if \(dc_yl <= dc_yh && dc_yh < viewheight\)\n        \{\n)",
     r"""\1          // psdoom-plus: where the label goes (top of what was drawn)
          if (!ps_col_drawn || dc_yl < ps_col_top)
            ps_col_top = dc_yl;
          ps_col_drawn = true;
""")
edit("r_things.c", r"(  sprtopscreen = centeryfrac - FixedMul\(dc_texturemid, ?spryscale\);\n)",
     r"""\1  ps_col_drawn = false;
  ps_col_top = viewheight;
""")
edit("r_things.c", r"(      R_DrawMaskedColumn \(column\);\n    \}\n)(  colfunc = R_DrawColumn;[^\n]*\n)",
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
edit("p_inter.c", r"(  if \(target->health <= 0\)\n    return;\n\n)(  if \(target->flags & MF_SKULLFLY\))",
     r"""\1  // psdoom-plus: pid monsters only take damage from players (psDooM)
  if (!PS_DamageAllowed(target, inflictor, source))
    return;

\2""")
edit("p_inter.c", r"(\n  target->health -= damage;\n)",
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
// psdoom-plus: the shared glue's two-argument P_KillMobj
void PS_P_KillMobj(mobj_t *source, mobj_t *target)
{
  P_KillMobj(source, NULL, target, MOD_None);
}
\1""")

# p_mobj.c: removal, -respawnitems
include("p_mobj.c")
edit("p_mobj.c", r"(void P_RemoveMobj \(mobj_t \*mobj\)\n\{\n)",
     r"""\1  PS_MobjRemoved(mobj);  // psdoom-plus
""")
edit("p_mobj.c", r"(  if \()(\(deathmatch != 2 && deathmatch != 3\))( \|\|  // only respawn items in deathmatch)",
     r"\1(\2 && (!respawnitems || demorecording || demoplayback))\3")

# p_tick.c, p_setup.c
include("p_tick.c")
edit("p_tick.c", r"(\n  P_RespawnSpecials\(\);\n)",
     r"""\1  PS_Ticker();  // psdoom-plus: processes come and go
""")
include("p_setup.c")
edit("p_setup.c", r"(\n  S_Start\(\);\n)",
     r"""\1  PS_LevelEnd();  // psdoom-plus: forget the old level's monsters
""")
edit("p_setup.c", r"(\n  if \(precache\)\n    R_PrecacheLevel\(\);\n)",
     r"""\1
  PS_LevelStart();  // psdoom-plus: spawn the processes
""")

# keyframes (saves): keep each monster's pid, re-adopt them after loading
edit("kf_file.c", r'(\n    JS_SetInt\(doc, obj, "frame", str->frame\);\n)',
     r"""\1    JS_SetInt(doc, obj, "ps_pid", str->ps_pid);  // psdoom-plus
""")
edit("kf_file.c", r'(\n    str->frame = JS_GetIntegerValue\(obj, "frame"\);\n)',
     r"""\1    str->ps_pid = JS_GetIntegerValue(obj, "ps_pid");  // psdoom-plus
""")
include("g_game.c")
edit("g_game.c", r"(\n    P_UnArchiveKeyframe\(root\);\n)",
     r"""\1    PS_Resync();  // psdoom-plus: re-adopt the pid monsters
""")
edit("g_game.c", r"(\n  P_UnArchiveMap\(\);[^\n]*\n  P_MapEnd\(\);\n)",
     r"""\1  PS_Resync();  // psdoom-plus (old savegames: no pid monsters)
""")
include("g_rewind.c")
edit("g_rewind.c", r"(\n        P_LoadKeyframe\(keyframe\);\n)",
     r"""\1        PS_Resync();  // psdoom-plus: re-adopt the pid monsters
""")

# g_game.c: -nomonsters. persists across new games
edit("g_game.c", r"(\n  nomonsters = clnomonsters;\n)",
     r"""\1  if (nomonstersperiod)
    nomonsters = true;  // psdoom-plus: -nomonsters. (psDooM)
""")

# d_main.c: the custom level and libpsd
include("d_main.c")
edit("d_main.c", r"(\n  IdentifyVersion\(\);\n)",
     r"""\1  PS_AddCustomLevel();  // psdoom-plus: psdoom1/2.wad
""")
edit("d_main.c", r"(\n  I_InitMusic\(\);\n)",
     r"""\1  PS_Init();  // psdoom-plus: open libpsd
""")

# mn_menu.c: the glue asks before killing root's processes
edit("mn_menu.c", r"\nstatic void M_StartMessage\(", "\nvoid M_StartMessage(", count=2, flags=0)

# params.h, m_argv.c: Woof! rejects options it doesn't know
edit("params.h", r"(static const char \*params\[\] = \{\n)",
     r"""\1// psdoom-plus
"-nomonsters.",
"-nopsact",
"-nopslev",
"-nopsmon",
"-nopssafety",
"-psallusers",
"-psclassic",
"-psnoact",
"-psshowprotected",
"-pstestview",
"-respawnitems",
""")
edit("params.h", r"(static const char \*params_with_args\[\] = \{\n)",
     r"""\1// psdoom-plus
"-psbackend",
"-psconf",
"-psgrace",
"-psinterval",
"-pslog",
"-psmax",
"-psmode",
"-psprotect",
"-pstest",
"-pstestshot",
""")
edit("m_argv.c", r"(\n    for \(i = 0; i < arrlen\(params_with_args\); \+\+i\)\n)",
     r"""
    // psdoom-plus: -psuser/-psnotuser take any number of user names
    if (!strcasecmp(myargv[p], "-psuser") || !strcasecmp(myargv[p], "-psnotuser"))
    {
      int check = CheckArgs(p);

      p = check ? check : p + 1;
      continue;
    }
\1""")

print("hooked:", " ".join(sorted(set(done))))
