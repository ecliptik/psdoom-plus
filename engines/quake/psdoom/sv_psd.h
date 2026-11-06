// sv_psd.h - psDooM for Quake (QuakeSpasm): processes as monsters
//
// Copyright (C) 2026 psdoom-plus contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef SV_PSD_H
#define SV_PSD_H

void     PSQ_MapEnd (void);             // SV_SpawnServer, before the old map goes
void     PSQ_MapLoaded (void);          // SV_SpawnServer, after ED_LoadFromFile
void     PSQ_Frame (void);              // Host_ServerFrame, after SV_Physics
void     PSQ_Shutdown (void);           // Host_Shutdown
void     PSQ_Freed (edict_t *ed);       // ED_Free

// PR_ExecuteProgram, at every QuakeC call: watches T_Damage and Killed.
// Nonzero means skip the call (a hit a process monster mustn't take).
qboolean PSQ_Call (dfunction_t *f);

#endif
