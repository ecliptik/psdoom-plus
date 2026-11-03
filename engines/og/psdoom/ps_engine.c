// ps_engine.c - og (linuxdoom-1.10) side of the shared psDooM glue
//
// Copyright (C) 2000 by David Koppenhofer
// Copyright (C) 2026 psdoom-plus contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "d_main.h"
#include "i_sound.h"
#include "i_video.h"
#include "r_main.h"

#include "i_platform.h"
#include "ps_engine.h"

void	M_WriteText (int x, int y, char* string);		// m_menu.c
void	M_StartMessage (char* string, void* routine, boolean input);

boolean PSE_IsDoom1 (void)
{
    return gamemode == registered || gamemode == retail;
}

boolean PSE_IsDoom2 (void)
{
    return gamemode == commercial;	// linuxdoom can't tell TNT/Plutonia apart
}

void PSE_AddWad (const char* path)
{
    D_AddFile (strdup (path));
}

void PSE_StartMessage (const char* text, void (*answer)(int key))
{
    M_StartMessage ((char*)text, (void*)answer, true);
}

int PSE_Screenshot (const char* path)
{
    return I_WriteScreenshotPPM (path);
}

void PSE_Shutdown (void)
{
    I_ShutdownSound ();
    I_ShutdownGraphics ();
}

// psDooM drew the pid and the last 7 chars of the name above each pid
// monster, when it was near enough and not at the edge of the screen.
void PSE_DrawLabel (long pid, const char* label, int x1, int x2, int top, int scale)
{
    char	buf[16];
    int		sx1 = x1 << detailshift;
    int		sx2 = x2 << detailshift;

    if (scale <= 18000 || sx1 <= SCREENWIDTH / 8 || sx2 >= SCREENWIDTH * 7 / 8)
	return;
    if (top + 13 > viewheight)
	top = viewheight - 13;
    if (top < 0)
	top = 0;
    snprintf (buf, sizeof buf, "%ld", pid);
    M_WriteText (viewwindowx + sx1, viewwindowy + top, buf);
    M_WriteText (viewwindowx + sx1, viewwindowy + top + 6, (char*)label);
}
