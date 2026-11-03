// ps_engine.h - what each Doom engine provides to the shared psDooM glue
//
// The glue (p_psdoom.c) is the same for og, Chocolate and Crispy; these are
// the few things that differ. Each engine implements them in
// engines/<engine>/psdoom/ps_engine.c.
//
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PS_ENGINE_H
#define PS_ENGINE_H

#include "doomtype.h"

boolean	PSE_IsDoom1 (void);		// registered/retail: psdoom1.wad replaces E1M1
boolean	PSE_IsDoom2 (void);		// Doom II proper (not TNT/Plutonia): psdoom2.wad
void	PSE_AddWad (const char* path);	// add a PWAD before the WADs are merged
void	PSE_StartMessage (const char* text, void (*answer)(int key));
int	PSE_Screenshot (const char* path);	// binary PPM of the last frame
void	PSE_Shutdown (void);		// before exit() at the end of -pstest

// psDooM's floating "pid / name" label for a sprite: vis->x1..x2 in view
// columns, top in view rows, scale = vis->scale
void	PSE_DrawLabel (long pid, const char* label, int x1, int x2, int top, int scale);

#endif
