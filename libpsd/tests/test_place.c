/*
 * test_place.c - psDooM's spawn geometry
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "psd.h"
#include "psd_test.h"

static void custom_level_boxes(void)
{
	int x, y, a;

	/* pid 4242: box 4242%3=0, x=(4242%16)*64=128, y=(4242%15)*64=768 */
	CHECK_EQ(psd_place(PSD_LEVEL_PSDOOM, 4242, 20, 0, &x, &y, &a), PSD_OK);
	CHECK_EQ(x, 128); CHECK_EQ(y, 768); CHECK_EQ(a, 315);
	/* pid 4243: box 1 -> x = 2048 + (4243%16)*64 = 2048+192, angle 270 */
	psd_place(PSD_LEVEL_PSDOOM, 4243, 20, 0, &x, &y, &a);
	CHECK_EQ(x, 2048 + 192); CHECK_EQ(a, 270);
	/* pid 30: box 0, x=(30%16)*64=896, y=(30%15)*64=0 -> clipped to radius */
	psd_place(PSD_LEVEL_PSDOOM, 30, 20, 0, &x, &y, &a);
	CHECK_EQ(x, 896); CHECK_EQ(y, 20);
	/* pid 47: box 2, x=4096+(47%16)*64=4096+960, inside the box */
	psd_place(PSD_LEVEL_PSDOOM, 47, 20, 0, &x, &y, &a);
	CHECK_EQ(x, 4096 + 960);
	/* radius 100 pushes it back to 4096+1024-100 */
	psd_place(PSD_LEVEL_PSDOOM, 47, 100, 0, &x, &y, &a);
	CHECK_EQ(x, 4096 + 924);
}

static void courtyards(void)
{
	int x, y, a;

	psd_place(PSD_LEVEL_E1M1, 17, 20, 0, &x, &y, &a);
	CHECK_EQ(x, 1800 + 40); CHECK_EQ(y, -3600 + 7 * 40); CHECK_EQ(a, 0);
	psd_place(PSD_LEVEL_MAP01, 17, 20, 0, &x, &y, &a);
	CHECK_EQ(x, 300 + 40); CHECK_EQ(y, -300 + 7 * 40);
}

static void retries_follow_psdoom_order(void)
{
	int x0, y0, x, y, a, n, seen_centre = 0;

	psd_place(PSD_LEVEL_E1M1, 17, 20, 0, &x0, &y0, &a);
	/* attempt 1: i=0 j=0 k=0 -> (-r, -r) */
	psd_place(PSD_LEVEL_E1M1, 17, 20, 1, &x, &y, &a);
	CHECK_EQ(x, x0 - 20); CHECK_EQ(y, y0 - 20);
	/* attempt 5: i=0, rest 4 -> skip centre -> j=1 k=2 -> (+r, 0) */
	psd_place(PSD_LEVEL_E1M1, 17, 20, 5, &x, &y, &a);
	CHECK_EQ(x, x0 + 20); CHECK_EQ(y, y0);
	/* attempt 16: i=1 j=2 k=2 -> (+2r, +2r) */
	psd_place(PSD_LEVEL_E1M1, 17, 20, 16, &x, &y, &a);
	CHECK_EQ(x, x0 + 40); CHECK_EQ(y, y0 + 40);
	for (n = 1; n < PSD_PLACE_ATTEMPTS; n++) {
		psd_place(PSD_LEVEL_E1M1, 17, 20, n, &x, &y, &a);
		if (x == x0 && y == y0)
			seen_centre = 1;
	}
	CHECK(!seen_centre);
	CHECK_EQ(psd_place(PSD_LEVEL_E1M1, 17, 20, PSD_PLACE_ATTEMPTS, &x, &y, &a), PSD_ERR);
}

int main(void)
{
	RUN(custom_level_boxes);
	RUN(courtyards);
	RUN(retries_follow_psdoom_order);
	TEST_DONE();
}
