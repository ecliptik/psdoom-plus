/*
 * psd_place.c - where pid monsters appear (psDooM's add_new_process)
 *
 * Copyright (C) 1999 Dennis Chao
 * Copyright (C) 2000 David Koppenhofer
 * Copyright (C) 2026 psdoom-plus contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "psd_internal.h"

/* keep the monster at least its radius inside its 1024x1024 box */
static void clip_to_box(int *x, int *y, int radius, int box_min_x)
{
	int min_x = box_min_x + radius, max_x = box_min_x + 1024 - radius;
	int min_y = radius, max_y = 1024 - radius;

	if (*x < min_x) *x = min_x; else if (*x > max_x) *x = max_x;
	if (*y < min_y) *y = min_y; else if (*y > max_y) *y = max_y;
}

int psd_place(psd_level level, long pid, int radius, int attempt,
              int *x, int *y, int *angle)
{
	int ox, oy, box = -1;

	if (attempt < 0 || attempt >= PSD_PLACE_ATTEMPTS || pid < 0)
		return PSD_ERR;

	switch (level) {
	case PSD_LEVEL_PSDOOM:
		box = (int)(pid % 3);
		ox = 2048 * box + (int)(pid % 16) * 64;
		oy = (int)(pid % 15) * 64;
		*angle = 315 - 45 * box;
		clip_to_box(&ox, &oy, radius, box * 2048);
		break;
	case PSD_LEVEL_MAP01:
		ox = 300 + (int)(pid % 16) * 40;
		oy = -300 + (int)(pid % 10) * 40;
		*angle = 0;
		break;
	case PSD_LEVEL_E1M1:
	default:
		ox = 1800 + (int)(pid % 16) * 40;
		oy = -3600 + (int)(pid % 10) * 40;
		*angle = 0;
		break;
	}

	if (attempt == 0) {
		*x = ox;
		*y = oy;
		return PSD_OK;
	}

	/* psDooM's retry order: for i in 0..1, j in 0..2, k in 0..2, skipping the centre */
	{
		int n = attempt - 1;             /* 0..15 */
		int i = n / 8, rest = n % 8, j, k;

		if (rest >= 4)
			rest++;                  /* skip j == 1 && k == 1 */
		j = rest / 3;
		k = rest % 3;
		*x = ox + (i + 1) * radius * (k - 1);
		*y = oy + (i + 1) * radius * (j - 1);
		if (box >= 0)
			clip_to_box(x, y, radius, box * 2048);
	}
	return PSD_OK;
}
