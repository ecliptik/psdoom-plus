// ps_defs.h - DSDA-Doom's API for the shared psDooM glue (p_psdoom.c)
//
// DSDA-Doom (PrBoom+) has no p_local.h, M_CheckParm or myargv; its
// P_KillMobj is static, P_TeleportMove has a "boss" flag, P_Random a class,
// and messages go through the messenger. It also plays Heretic and Hexen,
// which psDooM leaves alone.
//
// Copyright (C) 2026 psdoom-plus contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS_DEFS_H
#define PS_DEFS_H

#include "lprintf.h"
#include "p_inter.h"
#include "p_map.h"
#include "p_maputl.h"
#include "p_mobj.h"
#include "p_tick.h"
#include "r_main.h"
#include "tables.h"
#include "dsda/messenger.h"

extern int	dsda_argc;
extern char**	dsda_argv;
#define myargc	dsda_argc
#define myargv	dsda_argv
int	PSE_CheckParm (const char* check);
#define M_CheckParm(check)		PSE_CheckParm (check)

void	PS_P_KillMobj (mobj_t* source, mobj_t* target);	// p_inter.c (patch)
#define PSE_KillMobj(source, target)	PS_P_KillMobj (source, target)
#define PSE_TeleportMove(mo, x, y)	P_TeleportMove (mo, x, y, false)
#define PSE_Random()			P_Random (pr_misc)
#define PSE_MapEnd()			P_MapEnd ()	// clears tmthing for the next P_MapStart
#define PSE_Say(text)			dsda_AddMessage (text)
#define PSE_GameOK()			(!raven)

// saves and key frames keep pid monsters: PS_Resync() re-adopts them
void	PSE_DsdaForEachMobj (void (*fn)(mobj_t* mo));
#define PSE_ForEachMobj(fn)		PSE_DsdaForEachMobj (fn)

#endif
