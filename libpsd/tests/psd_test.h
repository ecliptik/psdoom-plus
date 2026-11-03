/*
 * psd_test.h - a tiny test harness, no dependencies
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef PSD_TEST_H
#define PSD_TEST_H

#include <stdio.h>
#include <stdlib.h>

static int psd_test_failures;
static int psd_test_checks;

#define CHECK(cond) do { \
	psd_test_checks++; \
	if (!(cond)) { \
		psd_test_failures++; \
		fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #cond); \
	} \
} while (0)

#define CHECK_EQ(a, b) do { \
	long long _a = (long long)(a), _b = (long long)(b); \
	psd_test_checks++; \
	if (_a != _b) { \
		psd_test_failures++; \
		fprintf(stderr, "%s:%d: FAIL: %s == %s (%lld != %lld)\n", \
		        __FILE__, __LINE__, #a, #b, _a, _b); \
	} \
} while (0)

#define RUN(fn) do { \
	int _before = psd_test_failures; \
	fn(); \
	printf("%-44s %s\n", #fn, psd_test_failures == _before ? "ok" : "FAILED"); \
} while (0)

#define TEST_DONE() do { \
	printf("%d checks, %d failures\n", psd_test_checks, psd_test_failures); \
	return psd_test_failures ? 1 : 0; \
} while (0)

#endif
