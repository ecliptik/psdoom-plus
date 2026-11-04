// ps_engine.c - DSDA-Doom side of the shared psDooM glue
//
// Copyright (C) 2000 by David Koppenhofer
// Copyright (C) 2026 psdoom-plus contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "d_main.h"
#include "doomdef.h"
#include "doomstat.h"
#include "p_mobj.h"
#include "p_tick.h"
#include "r_main.h"
#include "v_video.h"
#include "w_wad.h"
#include "dsda/stretch.h"

#include "ps_engine.h"
#include "ps_defs.h"

void	M_WriteText (int x, int y, const char* string, int cm);	// m_menu.c (patch)
void	M_StartMessage (const char* string, void* routine, dboolean input);

int PSE_CheckParm (const char* check)
{
    int i;

    for (i = 1; i < dsda_argc; i++)
	if (!strcasecmp (dsda_argv[i], check))
	    return i;
    return 0;
}

boolean PSE_IsDoom1 (void)
{
    return gamemode == registered || gamemode == retail;
}

boolean PSE_IsDoom2 (void)
{
    return gamemode == commercial && gamemission == doom2;
}

void PSE_AddWad (const char* path)
{
    D_AddFile (path, source_pwad);
}

void PSE_StartMessage (const char* text, void (*answer)(int key))
{
    M_StartMessage (text, (void*)answer, true);
}

// PPM from the 8-bit software frame and PLAYPAL (gamma 0)
int PSE_Screenshot (const char* path)
{
    const byte*	pal = W_LumpByName ("PLAYPAL");
    FILE*	f = fopen (path, "wb");
    int		x, y;

    if (f == NULL)
	return -1;
    fprintf (f, "P6\n%d %d\n255\n", SCREENWIDTH, SCREENHEIGHT);
    for (y = 0; y < SCREENHEIGHT; y++)
	for (x = 0; x < SCREENWIDTH; x++)
	    fwrite (pal + 3 * screens[0].data[y * screens[0].pitch + x], 1, 3, f);
    return fclose (f) == 0 ? 0 : -1;
}

void PSE_Shutdown (void)
{
}

void PSE_DsdaForEachMobj (void (*fn)(mobj_t* mo))
{
    thinker_t*	th;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
	if (th->function == P_MobjThinker)
	    fn ((mobj_t*)th);
}

// The view starts at the top left of the screen; the menu font draws in
// 320x200 units through the VPT_STRETCH parameters
void PSE_DrawLabel (long pid, const char* label, int x1, int x2, int top, int scale)
{
    stretch_param_t*	sp = dsda_StretchParams (VPT_STRETCH);
    char	buf[16];
    int		vw = sp->video->width, vh = sp->video->height;
    int		sx1 = (x1 - sp->deltax1) * 320 / vw;
    int		sx2 = (x2 - sp->deltax1) * 320 / vw;
    int		bottom = (viewheight - sp->deltay1) * 200 / vh;
    int		y = (top - sp->deltay1) * 200 / vh;
    // the sprite's scale at 200 lines, as psDooM measured it
    long long	s = (long long)scale * 200 / SCREENHEIGHT;

    if (s <= 18000 || sx1 <= 320 / 8 || sx2 >= 320 * 7 / 8)
	return;
    if (y + 13 > bottom)
	y = bottom - 13;
    if (y < 0)
	y = 0;
    snprintf (buf, sizeof buf, "%ld", pid);
    M_WriteText (sx1, y, buf, CR_DEFAULT);
    M_WriteText (sx1, y + 6, label, CR_DEFAULT);
}
