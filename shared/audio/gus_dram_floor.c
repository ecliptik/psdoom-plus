/*
 * gus_dram_floor.c -- see gus_dram_floor.h for the problem this solves and
 * the protocol every caller must follow.
 *
 * ASCII-only.
 */

#include "gus_dram_floor.h"

static uint32_t g_floor = 0;

uint32_t gus_dram_floor_get(void)
{
  return g_floor;
}

void gus_dram_floor_reserve(uint32_t through_addr)
{
  if (through_addr > g_floor)
    g_floor = through_addr;
}

void gus_dram_floor_reset(void)
{
  g_floor = 0;
}
