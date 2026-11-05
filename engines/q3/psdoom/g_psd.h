// g_psd.h - psDooM for Quake 3 (OpenArena game code): processes as bots
//
// Copyright (C) 2026 psdoom-plus contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef G_PSD_H
#define G_PSD_H

void     PSQ3_Init( int restart );                  // G_InitGame, after the bots
void     PSQ3_Shutdown( int restart );              // G_ShutdownGame
void     PSQ3_Frame( void );                        // G_RunFrame

// G_Damage: process bots are hurt only by human players (psDooM's rule)
qboolean PSQ3_DamageAllowed( gentity_t *targ, gentity_t *attacker, int mod );
void     PSQ3_Damaged( gentity_t *targ );           // health went down: renice
void     PSQ3_Killed( gentity_t *self, gentity_t *attacker );   // player_die: kill
qboolean PSQ3_HoldRespawn( gentity_t *ent );        // dead, waiting for libpsd

qboolean PSQ3_StaleBot( const char *userinfo );     // a process bot from the last map
void     PSQ3_ClientGone( int clientNum );          // ClientDisconnect

#endif
