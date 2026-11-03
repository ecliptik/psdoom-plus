// ps_engine.c - Crispy Doom side of the shared psDooM glue
//
// Copyright (C) 2000 by David Koppenhofer
// Copyright (C) 2026 psdoom-plus contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <stdio.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "i_video.h"
#include "r_main.h"
#include "r_state.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"

#include "crispy.h"
#include "ps_engine.h"

void	M_WriteText (int x, int y, const char* string);		// m_menu.c
void	M_StartMessage (const char* string, void* routine, boolean input);

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
    W_AddFile (path);
}

void PSE_StartMessage (const char* text, void (*answer)(int key))
{
    M_StartMessage (text, (void*)answer, true);
}

// PPM from the 8-bit frame and PLAYPAL (gamma 0)
int PSE_Screenshot (const char* path)
{
    const byte*	pal = W_CacheLumpName ("PLAYPAL", PU_CACHE);
    FILE*	f = fopen (path, "wb");
    int		i;

    if (f == NULL)
	return -1;
    fprintf (f, "P6\n%d %d\n255\n", SCREENWIDTH, SCREENHEIGHT);
    for (i = 0; i < SCREENWIDTH * SCREENHEIGHT; i++)
	fwrite (pal + 3 * I_VideoBuffer[i], 1, 3, f);
    return fclose (f) == 0 ? 0 : -1;
}

void PSE_Shutdown (void)
{
}

// Crispy renders at (1 << hires) times the original resolution, possibly
// widescreen; the menu font draws in 320x200 units plus WIDESCREENDELTA.
void PSE_DrawLabel (long pid, const char* label, int x1, int x2, int top, int scale)
{
    char	buf[16];
    int		sx1 = (viewwindowx + (x1 << detailshift)) >> crispy->hires;
    int		sx2 = (viewwindowx + (x2 << detailshift)) >> crispy->hires;
    int		w = SCREENWIDTH >> crispy->hires;
    int		vh = viewheight >> crispy->hires;
    int		y = top >> crispy->hires;

    if ((scale >> crispy->hires) <= 18000 || sx1 <= w / 8 || sx2 >= w * 7 / 8)
	return;
    if (y + 13 > vh)
	y = vh - 13;
    if (y < 0)
	y = 0;
    y += viewwindowy >> crispy->hires;
    snprintf (buf, sizeof buf, "%ld", pid);
    M_WriteText (sx1 - WIDESCREENDELTA, y, buf);
    M_WriteText (sx1 - WIDESCREENDELTA, y + 6, label);
}
