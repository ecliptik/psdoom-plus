// p_psdoom.h - psDooM for linuxdoom-1.10: processes as monsters, on libpsd
//
// Copyright (C) 1999 by Dennis Chao
// Copyright (C) 2000 by David Koppenhofer
// Copyright (C) 2026 psdoom-plus contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef P_PSDOOM_H
#define P_PSDOOM_H

#include "doomtype.h"

struct mobj_s;

#define PS_LABEL_LEN 8

extern boolean	ps_level_loaded;	// psdoom1/2.wad was loaded
extern boolean	respawnitems;		// -respawnitems
extern boolean	nomonstersperiod;	// -nomonsters. (persists across new games)

void	PS_AddCustomLevel (void);	// after IdentifyVersion: psdoom1/2.wad unless -nopslev
void	PS_Init (void);			// open libpsd from psdoom.conf + the command line

void	PS_LevelEnd (void);		// start of P_SetupLevel, before the zone is freed
void	PS_LevelStart (void);		// end of P_SetupLevel
void	PS_Ticker (void);		// every tic, from P_Ticker
void	PS_SaveLoaded (void);		// after P_UnArchiveThinkers

boolean	PS_DamageAllowed (struct mobj_s* target, struct mobj_s* inflictor,
			  struct mobj_s* source);
void	PS_Damaged (struct mobj_s* target);	// after health was reduced
void	PS_Killed (struct mobj_s* source, struct mobj_s* target);
boolean	PS_NoDrop (struct mobj_s* target);	// skip the item drop
void	PS_MobjRemoved (struct mobj_s* mobj);
boolean	PS_SkipSave (struct mobj_s* mobj);	// pid monsters aren't saved

// floating pid/name labels (r_things.c); drawn by PSE_DrawLabel
extern boolean	ps_col_drawn;
extern int	ps_col_top;
#define PS_DrawLabel PSE_DrawLabel
#include "ps_engine.h"

#endif
