// ps_defs.h - Woof!'s API for the shared psDooM glue (p_psdoom.c)
//
// Woof! has no p_local.h, its P_KillMobj is static and takes an inflictor
// and a method, P_TeleportMove has a "boss" flag and P_Random a class.
//
// Copyright (C) 2026 psdoom-plus contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS_DEFS_H
#define PS_DEFS_H

#include "p_inter.h"
#include "p_map.h"
#include "p_maputl.h"
#include "p_mobj.h"
#include "p_tick.h"
#include "r_main.h"
#include "tables.h"

void	PS_P_KillMobj (mobj_t* source, mobj_t* target);	// p_inter.c (patch)
#define PSE_KillMobj(source, target)	PS_P_KillMobj (source, target)
#define PSE_TeleportMove(mo, x, y)	P_TeleportMove (mo, x, y, false)
#define PSE_Random()			P_Random (pr_misc)
#define PSE_MapEnd()			P_MapEnd ()	// clears tmthing for the next P_MapStart

// saves and rewind keep pid monsters: PS_Resync() re-adopts them
void	PSE_WoofForEachMobj (void (*fn)(mobj_t* mo));
#define PSE_ForEachMobj(fn)		PSE_WoofForEachMobj (fn)

#endif
