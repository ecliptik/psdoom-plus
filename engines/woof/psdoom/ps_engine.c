// ps_engine.c - Woof! side of the shared psDooM glue
//
// Copyright (C) 2000 by David Koppenhofer
// Copyright (C) 2026 psdoom-plus contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <stdio.h>
#include <string.h>

#include "d_main.h"
#include "doomdef.h"
#include "doomstat.h"
#include "mn_menu.h"
#include "p_mobj.h"
#include "p_tick.h"
#include "r_main.h"
#include "r_state.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"

#include "ps_engine.h"
#include "ps_defs.h"

void	M_StartMessage (const char* string, void (*routine)(int), boolean input);	// mn_menu.c (patch)

boolean PSE_IsDoom1 (void)
{
    return gamemode == registered || gamemode == retail;
}

// Doom II or Freedoom: Phase 2 (Woof! calls its mission pack_freedoom), not
// TNT or Plutonia
boolean PSE_IsDoom2 (void)
{
    return gamemode == commercial
	&& (gamemission == doom2 || gamemission == pack_freedoom);
}

void PSE_AddWad (const char* path)
{
    D_AddFile (path);
}

void PSE_StartMessage (const char* text, void (*answer)(int key))
{
    M_StartMessage (text, answer, true);
}

// PPM from the 8-bit frame and PLAYPAL (gamma 0)
int PSE_Screenshot (const char* path)
{
    const byte*	pal = W_CacheLumpName ("PLAYPAL", PU_CACHE);
    FILE*	f = fopen (path, "wb");
    int		i;

    if (f == NULL)
	return -1;
    fprintf (f, "P6\n%d %d\n255\n", video.width, video.height);
    for (i = 0; i < video.width * video.height; i++)
	fwrite (pal + 3 * I_VideoBuffer[i], 1, 3, f);
    return fclose (f) == 0 ? 0 : -1;
}

void PSE_Shutdown (void)
{
}

void PSE_WoofForEachMobj (void (*fn)(mobj_t* mo))
{
    thinker_t*	th;
    thinker_t*	next;

    for (th = thinkercap.next; th != &thinkercap; th = next)
    {
	next = th->next;
	if (th->function.p1 == P_MobjThinker)
	    fn ((mobj_t*)th);
    }
}

// Woof! renders at video.width x video.height; the menu font draws in
// 320x200 units, widescreen ones offset by video.deltaw
void PSE_DrawLabel (long pid, const char* label, int x1, int x2, int top, int scale)
{
    char	buf[16];
    int		sx1 = (viewwindowx + x1) * video.unscaledw / video.width - video.deltaw;
    int		sx2 = (viewwindowx + x2) * video.unscaledw / video.width - video.deltaw;
    int		vh = viewheight * SCREENHEIGHT / video.height;
    int		y = top * SCREENHEIGHT / video.height;
    // the sprite's scale at 200 lines, as psDooM measured it
    long long	s = (long long)scale * SCREENHEIGHT / video.height;

    if (s <= 18000 || sx1 <= -video.deltaw + video.unscaledw / 8
	|| sx2 >= video.unscaledw * 7 / 8 - video.deltaw)
	return;
    if (y + 13 > vh)
	y = vh - 13;
    if (y < 0)
	y = 0;
    y += viewwindowy * SCREENHEIGHT / video.height;
    snprintf (buf, sizeof buf, "%ld", pid);
    MN_DrawString (sx1, y, CR_NONE, buf);
    MN_DrawString (sx1, y + 6, CR_NONE, label);
}
