// p_psdoom.c - psDooM for linuxdoom-1.10: processes as monsters, on libpsd
//
// The successor of psDooM's pr_process.c. libpsd decides which processes
// exist, which are protected and what a kill means; this file turns its
// events into Doom monsters and Doom damage back into renice/kill.
//
// Copyright (C) 1999 by Dennis Chao
// Copyright (C) 2000 by David Koppenhofer
// Copyright (C) 2026 psdoom-plus contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "doomdef.h"
#include "doomstat.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_random.h"
#ifndef PSE_DEFS
#include "p_local.h"
#endif
#include "p_mobj.h"
#include "r_main.h"
#include "info.h"

#include "p_psdoom.h"
#include "ps_engine.h"
#include "psd.h"

// An engine whose API differs (Boom/MBF descendants) names a header of
// overrides with -DPSE_DEFS="ps_defs.h"; the defaults fit linuxdoom,
// Chocolate and Crispy
#ifdef PSE_DEFS
#include PSE_DEFS
#endif
#ifndef PSE_KillMobj
void	P_KillMobj (mobj_t* source, mobj_t* target);	// p_inter.c
#define PSE_KillMobj(source, target)	P_KillMobj (source, target)
#endif
#ifndef PSE_TeleportMove
#define PSE_TeleportMove(mo, x, y)	P_TeleportMove (mo, x, y)
#endif
#ifndef PSE_Random
#define PSE_Random()			P_Random ()
#endif
#ifndef PSE_MapEnd			// after map checks outside the ticker
#define PSE_MapEnd()
#endif
#ifndef PSE_Say
#define PSE_Say(text)			(players[consoleplayer].message = (text))
#endif
#ifndef PSE_GameOK			// e.g. not Heretic/Hexen
#define PSE_GameOK()			1
#endif

boolean	ps_level_loaded;
boolean	respawnitems;
boolean	nomonstersperiod;

boolean	ps_col_drawn;
int	ps_col_top;

static psd_ctx*	ctx;
static boolean	no_safety;
static int	poll_interval;		// tics
static boolean	active;			// this level manages processes

// pid -> monster, for the current level only
typedef struct { long pid; mobj_t* mo; boolean seen; } psmon_t;
static psmon_t*	mons;
static int	nmons, capmons;

// -pstest: automated check used by the test harness
static char*	test_glob;
static char*	test_shot;
static boolean	test_view;		// -pstestview: look at a pid monster first
static int	test_finish_at;
static int	test_state;		// 0 off, 1 waiting for targets, 2 waiting for removal
static int	test_deadline;		// leveltime
static long	test_pids[64];
static int	test_ntargets, test_nremoved;
static long long	test_last_ms, test_max_gap;	// longest wall-clock gap between tics

static long long now_ms (void)
{
#if defined(__DJGPP__)
    // the clock SDL3-DOS uses too (no clock_gettime on DOS); uclock() can
    // step back a BIOS tick, so keep it monotonic
    static long long last;
    long long now = (long long)uclock () * 1000 / UCLOCKS_PER_SEC;

    if (now > last)
	last = now;
    return last;
#else
    struct timespec ts;

    clock_gettime (CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

static char	msgbuf[80];

// ------------------------------------------------------------------

static psmon_t* find_mon (long pid)
{
    int i;

    for (i = 0; i < nmons; i++)
	if (mons[i].pid == pid)
	    return &mons[i];
    return NULL;
}

static void add_mon (long pid, mobj_t* mo)
{
    if (nmons == capmons)
    {
	capmons = capmons ? capmons * 2 : 64;
	mons = realloc (mons, capmons * sizeof *mons);
	if (!mons)
	    I_Error ("PS: out of memory");
    }
    mons[nmons].pid = pid;
    mons[nmons].mo = mo;
    mons[nmons].seen = true;
    nmons++;
}

static void drop_mon (psmon_t* m)
{
    *m = mons[--nmons];
}

static void say (const char* text)
{
    snprintf (msgbuf, sizeof msgbuf, "%s", text);
    PSE_Say (msgbuf);
}

static boolean level_qualifies (void)
{
    // psDooM: only the first map, never in demos (or netgames: they'd desync)
    if (ctx == NULL || demoplayback || demorecording || netgame || !PSE_GameOK ())
	return false;
    if (gamemap != 1)
	return false;
    // psDooM: E1M1 or MAP01, but not TNT's or Plutonia's MAP01
    return gamemode == commercial ? PSE_IsDoom2 () : gameepisode == 1;
}

static psd_level level_kind (void)
{
    if (ps_level_loaded)
	return PSD_LEVEL_PSDOOM;
    return gamemode == commercial ? PSD_LEVEL_MAP01 : PSD_LEVEL_E1M1;
}

static void set_label (mobj_t* mo, const psd_proc* p)
{
    memcpy (mo->ps_label, p->label, PS_LABEL_LEN);
    mo->ps_label[PS_LABEL_LEN - 1] = 0;
}

// psDooM's add_new_process(): place it, nudging around collisions
static mobj_t* spawn_process (const psd_proc* p)
{
    mobjtype_t	type = p->is_daemon ? MT_SERGEANT : MT_SHOTGUY;
    int		radius = mobjinfo[type].radius >> FRACBITS;
    int		attempt, x, y, angle;
    mobj_t*	mo;

    for (attempt = 0; attempt < PSD_PLACE_ATTEMPTS; attempt++)
    {
	if (psd_place (level_kind (), p->pid, radius, attempt, &x, &y, &angle) != PSD_OK)
	    break;
	mo = P_SpawnMobj (x << FRACBITS, y << FRACBITS, ONFLOORZ, type);
	if (!P_CheckPosition (mo, mo->x, mo->y))
	{
	    P_RemoveMobj (mo);
	    continue;
	}
	if (mo->tics > 0)
	    mo->tics = 1 + (PSE_Random () % mo->tics);
	mo->angle = ANG45 * (angle / 45);
	mo->flags &= ~MF_COUNTKILL;	// not part of the level's kill count
	mo->ps_pid = p->pid;
	mo->ps_draw = true;
	mo->ps_protected = p->is_protected;
	set_label (mo, p);
	return mo;
    }
    return NULL;			// no room now: libpsd offers it again later
}

// The archvile's raise (A_VileChase), for a process that outlived its monster
static boolean raise_monster (mobj_t* mo)
{
    if (mo->info->raisestate == S_NULL)
	return false;
    mo->momx = mo->momy = 0;
    if (!P_CheckPosition (mo, mo->x, mo->y))
	return false;
    P_SetMobjState (mo, mo->info->raisestate);
    mo->height <<= 2;
    mo->flags = mo->info->flags & ~MF_COUNTKILL;
    mo->health = mo->info->spawnhealth;
    mo->target = NULL;
    mo->ps_draw = true;
    return true;
}

// process gone: remove the monster without a corpse or a dropped item
static void vanish (mobj_t* mo)
{
    if (mo->health > 0)
    {
	mo->ps_removing = true;
	PSE_KillMobj (NULL, mo);
    }
    mo->ps_removing = true;		// P_RemoveMobj: don't psd_forget
    P_RemoveMobj (mo);
}

#ifdef PSE_ForEachMobj
static boolean	resyncing;		// PS_Resync's poll: adopt, don't spawn
#endif

static void test_note_removed (long pid)
{
    int i;

    for (i = 0; i < test_ntargets; i++)
	if (test_pids[i] == pid)
	{
	    test_pids[i] = -1;
	    test_nremoved++;
	}
}

static int on_event (void* user, psd_event ev, const psd_proc* p)
{
    psmon_t*	m = find_mon (p->pid);
    mobj_t*	mo;
    (void)user;

    switch (ev)
    {
      case PSD_EV_SPAWN:
#ifdef PSE_ForEachMobj
	if (m && resyncing && m->mo->health > 0)
	{				// a loaded monster: still running
	    m->seen = true;
	    set_label (m->mo, p);
	    m->mo->ps_protected = p->is_protected;
	    m->mo->ps_draw = true;
	    return 1;
	}
#endif
	if (m)				// stale entry from an earlier life
	    drop_mon (m);
	mo = spawn_process (p);
	if (!mo)
	    return 0;
	add_mon (p->pid, mo);
	fprintf (stderr, "psdoom: %ld [%s] spawned\n", p->pid, p->label);
	return 1;

      case PSD_EV_UPDATE:
	if (m)
	    set_label (m->mo, p);
	return 1;

      case PSD_EV_RESURRECT:
	if (m && raise_monster (m->mo))
	{
	    fprintf (stderr, "psdoom: %ld [%s] resurrected\n", p->pid, p->label);
	    return 1;
	}
	if (m)				// can't raise: remove the body, respawn later
	{
	    mo = m->mo;
	    drop_mon (m);
	    mo->ps_removing = true;
	    P_RemoveMobj (mo);
	}
	return 0;

      case PSD_EV_REMOVE:
	test_note_removed (p->pid);
	if (m)
	{
	    mo = m->mo;
	    drop_mon (m);
	    if (mo->health > 0)
	    {
		fprintf (stderr, "psdoom: %ld [%s] gone\n", p->pid, p->label);
		vanish (mo);
	    }
	    else
		mo->ps_pid = 0;		// a corpse stays, as in psDooM
	}
	return 1;
    }
    return 0;
}

// ------------------------------------------------------------------

static const char* find_data (const char* name)
{
    static char	path[1024];
    const char*	dirs[5];
    int		i, n = 0;

    dirs[n++] = getenv ("PSDOOM_DATA");
    dirs[n++] = getenv ("DOOMWADDIR");
#ifdef PSDOOM_SHAREDIR
    dirs[n++] = PSDOOM_SHAREDIR;
#endif
#ifdef PSDOOM_DATADIR
    dirs[n++] = PSDOOM_DATADIR;
#endif
    dirs[n++] = ".";
    for (i = 0; i < n; i++)
    {
	if (!dirs[i] || !dirs[i][0])
	    continue;
	FILE*	f;
	snprintf (path, sizeof path, "%s/%s", dirs[i], name);
	if ((f = fopen (path, "rb")) != NULL)	// (unistd.h clashes with p_spec.h)
	{
	    fclose (f);
	    return path;
	}
    }
    return NULL;
}

// for engines that reject command-line options they don't know
boolean PS_IsOption (const char* arg)
{
    return !strncmp (arg, "-ps", 3) || !strncmp (arg, "-nops", 5)
	|| !strcmp (arg, "-nomonsters.") || !strcmp (arg, "-respawnitems");
}

void PS_AddCustomLevel (void)
{
    const char*	wad = NULL;

    nomonstersperiod = M_CheckParm ("-nomonsters.") != 0;
    if (nomonstersperiod)
	nomonsters = true;
    respawnitems = M_CheckParm ("-respawnitems") != 0;

    if (M_CheckParm ("-nopslev") || M_CheckParm ("-nopsmon"))
	return;
    // psdoom1.wad replaces E1M1 (registered/retail), psdoom2.wad MAP01
    if (PSE_IsDoom1 ())
	wad = find_data ("psdoom1.wad");
    else if (PSE_IsDoom2 ())
	wad = find_data ("psdoom2.wad");
    if (wad)
    {
	PSE_AddWad (wad);
	ps_level_loaded = true;
    }
}

void PS_Init (void)
{
    psd_config*	cfg = psd_config_new ();
    int		err, p;

    psd_config_load_default_file (cfg);
    psd_config_parse_args (cfg, myargc, myargv);
    no_safety = psd_config_no_safety (cfg);

    if (psd_config_disabled (cfg))
    {
	printf ("psdoom: process monitoring off (-nopsmon)\n");
	psd_config_free (cfg);
	return;
    }
    ctx = psd_open (cfg, &err);
    psd_config_free (cfg);
    if (!ctx)
    {
	printf ("psdoom: %s; playing plain Doom\n", psd_strerror (err));
	return;
    }

    // psDooM polled every 256 tics (~7 s); modern mode is livelier
    poll_interval = psd_mode_of (ctx) == PSD_MODE_CLASSIC ? 256 : 70;
    p = M_CheckParm ("-psinterval");
    if (p && p < myargc - 1)
	poll_interval = atoi (myargv[p + 1]);

    p = M_CheckParm ("-pstest");
    if (p && p < myargc - 1)
    {
	test_glob = myargv[p + 1];
	test_state = 1;
	if (poll_interval > 35)
	    poll_interval = 35;
	p = M_CheckParm ("-pstestshot");
	if (p && p < myargc - 1)
	    test_shot = myargv[p + 1];
	test_view = M_CheckParm ("-pstestview") != 0;
    }
    if (poll_interval < 1)
	poll_interval = 1;

    printf ("psdoom-plus %s: %s mode, %s backend, %s\n", PSD_VERSION,
	    psd_mode_of (ctx) == PSD_MODE_CLASSIC ? "classic" : "modern",
	    psd_backend_name (ctx),
	    ps_level_loaded ? "custom level" : "stock first map");
}

void PS_LevelEnd (void)
{
    nmons = 0;				// the zone is about to be freed
    active = false;
    if (ctx)
	psd_reset (ctx);
}

static void test_finish (int code);

void PS_LevelStart (void)
{
    active = level_qualifies ();
    if (active)
    {
	psd_poll (ctx, on_event, NULL);
	PSE_MapEnd ();
	if (test_state)
	    test_deadline = leveltime + 35 * 30;
    }
    else if (test_state && ctx && !demoplayback)
    {
	// -pstest on a level that gets no pid monsters would wait forever
	printf ("psdoom: -pstest: this level gets no process monsters "
		"(psDooM uses E1M1 or Doom II's MAP01)\n");
	test_finish (1);
    }
}

void PS_SaveLoaded (void)
{
    PS_LevelEnd ();
    PS_LevelStart ();
}

#ifdef PSE_ForEachMobj
// Engines whose saves and rewind keep pid monsters (with their ps_pid) call
// PS_Resync() after restoring the thing list: living pid monsters are
// adopted again, and those whose process has gone meanwhile vanish
static void adopt (mobj_t* mo)
{
    if (!mo->ps_pid)
	return;
    mo->ps_removing = false;
    if (!active || mo->health <= 0 || find_mon (mo->ps_pid))
    {
	mo->ps_pid = 0;			// a corpse stays, as in psDooM
	mo->ps_draw = false;
	return;
    }
    add_mon (mo->ps_pid, mo);
    mons[nmons - 1].seen = false;
}

void PS_Resync (void)
{
    int i;

    nmons = 0;				// the old pointers may be gone
    if (ctx)
	psd_reset (ctx);
    active = level_qualifies ();
    PSE_ForEachMobj (adopt);
    if (!active)
	return;
    resyncing = true;
    psd_poll (ctx, on_event, NULL);
    resyncing = false;
    for (i = nmons - 1; i >= 0; i--)
	if (!mons[i].seen)
	{
	    mobj_t* mo = mons[i].mo;
	    drop_mon (&mons[i]);
	    vanish (mo);
	}
    PSE_MapEnd ();
}
#endif

// ------------------------------------------------------------------
// -pstest

static void test_finish (int code)
{
    if (test_shot)
	PSE_Screenshot (test_shot);
    printf ("PSTEST mode=%s targeted=%d removed=%d result=%s maxtic=%lldms target=%s\n",
	    psd_mode_of (ctx) == PSD_MODE_CLASSIC ? "classic" : "modern",
	    test_ntargets, test_nremoved, code == 0 ? "ok" : "fail", test_max_gap, test_glob);
    fflush (stdout);
    psd_close (ctx);
    ctx = NULL;
    PSE_Shutdown ();
    exit (code);
}

// -pstestview: up to four living pid monsters (a daemon first) stand in a
// row across the view, facing the player, so every label is clear of the
// others, of the weapon and of the screen edges (where psDooM hides labels).
// Of sixteen directions around the first one, the picture keeps the one where
// most of them fit and can be seen. False if there is no living one.
// units to the left of the middle, from 240 away: the daemon (wider, its label
// starts further left) just right of the middle, two on the left, one on the right
static const int	ps_view_lat[4] = { -60, 155, 54, -155 };
static const int	ps_view_lat_even[4] = { -54, 155, 54, -155 };	// no daemon

static int line_up_at (mobj_t* shooter, mobj_t** row, int n, fixed_t cx, fixed_t cy, angle_t a, int d)
{
    int		f = a >> ANGLETOFINESHIFT, s = (a + ANG90) >> ANGLETOFINESHIFT;
    int		i, fit = 0, seen = 0, flags[4];
    fixed_t	px = cx - d * finecosine[f], py = cy - d * finesine[f];

    if (!P_CheckPosition (shooter, px, py))
	return -1;
    shooter->flags &= ~MF_PICKUP;		// trying spots picks nothing up
    // where they stood for the last try doesn't block this one
    for (i = 0; i < n; i++)
    {
	flags[i] = row[i]->flags;
	row[i]->flags &= ~(MF_SOLID | MF_SHOOTABLE);
    }
    for (i = 0; i < n; i++)
    {
	int	l = (row[0]->type == MT_SERGEANT ? ps_view_lat : ps_view_lat_even)[i] * d / 240;	// the same picture from nearer
	fixed_t x = cx + l * finecosine[s], y = cy + l * finesine[s];

	if (P_CheckPosition (row[i], x, y) && PSE_TeleportMove (row[i], x, y))
	    fit++;
	row[i]->z = row[i]->floorz;	// teleporting leaves z alone
	row[i]->angle = a + ANG180;
    }
    for (i = 0; i < n; i++)
	row[i]->flags = flags[i];
    PSE_TeleportMove (shooter, px, py);
    shooter->z = shooter->floorz;	// not falling into the picture
    shooter->momz = 0;
    if (shooter->player)
	shooter->player->viewz = shooter->z + shooter->player->viewheight;
    shooter->angle = a;
    // seen, and on the player's level (the labels sit right)
    for (i = 0; i < n; i++)
	seen += P_CheckSight (shooter, row[i]) && abs (row[i]->z - shooter->z) <= 24 * FRACUNIT;
    return fit * 8 + seen;
}

static boolean test_line_up (mobj_t* shooter)
{
    mobj_t*		row[4];
    int			n = 0, i, k, c, d, ncent = 0, score, best = -1, best_dir = 0, best_c = 0, best_d = 240;
    fixed_t		cent[64][2];
    angle_t		a0;

    // one daemon, then user processes, then more daemons if that's all there is
    for (k = 0; k < 3; k++)
	for (i = 0; i < nmons && n < (k == 0 ? 1 : 4); i++)
	{
	    mobj_t* mo = mons[i].mo;
	    boolean daemon = mo->type == MT_SERGEANT;

	    if (mo->health > 0 && (k == 1 ? !daemon : daemon) && (k != 2 || mo != row[0]))
		row[n++] = mo;
	}
    if (n == 0)
	return false;
    // any pid monster's spot may be the middle of the row
    for (i = 0; i < nmons && ncent < 64; i++)
	if (mons[i].mo->health > 0)
	{
	    cent[ncent][0] = mons[i].mo->x;
	    cent[ncent++][1] = mons[i].mo->y;
	}
    // the other pid monsters leave the picture (the game ends after it)
    for (i = 0; i < nmons; i++)
    {
	for (k = 0; k < n && row[k] != mons[i].mo; k++)
	    ;
	if (k == n)
	    P_UnsetThingPosition (mons[i].mo);
    }
    a0 = row[0]->angle;
    for (d = 240; d >= 160; d -= 40)		// the farthest that works
	for (c = 0; c < ncent; c++)
	    for (k = 0; k < 16; k++)
		if ((score = line_up_at (shooter, row, n, cent[c][0], cent[c][1],
					 a0 + (angle_t)k * (ANG45 / 2), d)) > best)
		{
		    best = score;
		    best_dir = k;
		    best_c = c;
		    best_d = d;
		}
    if (best < 0)
	return false;
    line_up_at (shooter, row, n, cent[best_c][0], cent[best_c][1],
		a0 + (angle_t)best_dir * (ANG45 / 2), best_d);
    shooter->flags |= MF_PICKUP;
    for (i = 0; i < n; i++)
    {
	P_SetMobjState (row[i], row[i]->info->spawnstate);
	row[i]->tics = -1;		// hold still for the picture
    }
    return true;
}

static void test_ticker (void)
{
    mobj_t*	shooter = players[consoleplayer].mo;
    int		i;

    if (test_state == 1 && leveltime > 35 * 2)
    {
	// shoot every pid monster whose process name matches, through the
	// normal damage path: a hit (renice), then a kill
	for (i = 0; i < nmons && test_ntargets < 64; i++)
	{
	    const psd_proc* p = psd_lookup (ctx, mons[i].pid);
	    mobj_t* mo = mons[i].mo;

	    if (!p || mo->health <= 0 || fnmatch (test_glob, p->comm, 0))
		continue;
	    test_pids[test_ntargets++] = p->pid;
	}
	// a crowded level may not have room for it yet: psDooM places it on a
	// later poll, so wait a while for a target before giving up
	if (test_ntargets == 0 && leveltime < 35 * 15)
	    return;
	for (i = 0; i < test_ntargets; i++)
	{
	    psmon_t* m = find_mon (test_pids[i]);
	    if (!m)
		continue;
	    P_DamageMobj (m->mo, shooter, shooter, 1);
	    if (m->mo->health > 0)
		P_DamageMobj (m->mo, shooter, shooter, 10000);
	}
	test_state = 2;
	test_deadline = leveltime + 35 * 12;
    }
    else if (test_state == 2)
    {
	if ((test_nremoved == test_ntargets && leveltime > 35 * 4)
	    || leveltime >= test_deadline)
	{
	    test_state = 3;
	    test_finish_at = leveltime;
	    if (test_view && test_line_up (shooter))
		test_finish_at = leveltime + 4;
	    else if (test_view)
	    {
		// stand 160 units from a living pid monster, facing it
		for (i = 0; i < nmons; i++)
		    if (mons[i].mo->health > 0)
		    {
			mobj_t* mo = mons[i].mo;
			fixed_t x = mo->x - 160 * finecosine[mo->angle >> ANGLETOFINESHIFT];
			fixed_t y = mo->y - 160 * finesine[mo->angle >> ANGLETOFINESHIFT];
			PSE_TeleportMove (shooter, x, y);
			shooter->angle = R_PointToAngle2 (x, y, mo->x, mo->y);
			mo->tics = -1;		// hold still for the picture
			test_finish_at = leveltime + 4;
			break;
		    }
	    }
	}
    }
    else if (test_state == 3 && leveltime >= test_finish_at)
	test_finish (test_nremoved == test_ntargets ? 0 : 2);
}

void PS_Ticker (void)
{
    if (!active)
	return;
    psd_tick (ctx);
    if (leveltime > 0 && leveltime % poll_interval == 0)
	psd_poll (ctx, on_event, NULL);
    if (test_state)
    {
	long long t = now_ms ();
	// a stalled game loop runs the missed tics back to back afterwards,
	// so the stall shows up as one long gap
	if (test_last_ms && leveltime > 35 && t - test_last_ms > test_max_gap)
	    test_max_gap = t - test_last_ms;
	test_last_ms = t;
	test_ticker ();
    }
}

// ------------------------------------------------------------------
// damage, death, removal

boolean PS_DamageAllowed (mobj_t* target, mobj_t* inflictor, mobj_t* source)
{
    (void)inflictor;
    if (!target->ps_pid)
	return true;
    if (target->ps_protected)
	return false;
    // psDooM: only players may hurt pid monsters, unless -nopssafety
    return no_safety || (source && source->player);
}

void PS_Damaged (mobj_t* target)
{
    if (target->ps_pid && ctx)
	psd_wound (ctx, target->ps_pid, target->health, target->info->spawnhealth);
}

static long	confirm_pid;

static void confirm_kill (int key)
{
    psmon_t*	m;

    if (!ctx || !confirm_pid)
	return;
    if (key == 'y')
	psd_kill (ctx, confirm_pid);
    else if ((m = find_mon (confirm_pid)) != NULL && !raise_monster (m->mo))
    {
	// spared, but there's no room to stand it up: respawn it later
	mobj_t* mo = m->mo;
	drop_mon (m);
	mo->ps_removing = true;
	P_RemoveMobj (mo);
	psd_forget (ctx, confirm_pid);
    }
    PSE_MapEnd ();
    confirm_pid = 0;
}

void PS_Killed (mobj_t* source, mobj_t* target)
{
    const psd_proc* p;
    int		rc;
    (void)source;

    if (!target->ps_pid || target->ps_removing || !ctx)
	return;
    target->ps_draw = false;
    p = psd_lookup (ctx, target->ps_pid);

    if (psd_needs_confirm (ctx, target->ps_pid) && !test_state && !confirm_pid && p)
    {
	static char q[160];
	confirm_pid = p->pid;
	snprintf (q, sizeof q, "kill root process %ld\n%s?\n\npress y or n.",
		  p->pid, p->label);
	PSE_StartMessage (q, confirm_kill);
	return;			// if not confirmed, it rises again next poll
    }

    rc = psd_kill (ctx, target->ps_pid);
    if (p)
    {
	char buf[64];
	snprintf (buf, sizeof buf, rc == PSD_OK ? "killed %ld %s" : "%ld %s survives",
		  p->pid, p->label);
	say (buf);
    }
}

boolean PS_NoDrop (mobj_t* target)
{
    return target->ps_pid && target->ps_removing;
}

void PS_MobjRemoved (mobj_t* mobj)
{
    psmon_t* m;

    if (!mobj->ps_pid)
	return;
    m = find_mon (mobj->ps_pid);
    if (m && m->mo == mobj)
    {
	drop_mon (m);
	if (!mobj->ps_removing && ctx)
	    psd_forget (ctx, mobj->ps_pid);	// the engine removed it: offer again
    }
}

boolean PS_SkipSave (mobj_t* mobj)
{
    return mobj->ps_pid != 0;
}
