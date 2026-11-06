// sv_psd.c - psDooM for Quake (QuakeSpasm): processes as monsters
//
// Every process is a monster: Grunts for user processes, Fiends for daemons
// (psDooM's Demons), and Zombies for processes modern mode protects (Quake's
// zombies don't die anyway). Look at one and its "pid:command" shows in the
// middle of the screen. Hurting it renices the process, killing it kills the
// process. libpsd decides which processes exist, which are protected and
// what a kill means, as for the Doom engines.
//
// No QuakeC changes: the monsters come from the game's own spawn functions,
// and PR_ExecuteProgram reports every call to T_Damage and Killed, so this
// works with id's progs.dat (shareware or registered) and compatible mods.
//
//   psDooM's options on the command line: -psclassic, -psuser NAME, ...
//   ps_monsters 16             at most this many process monsters on a map
//   ps_monster_user  monster_army
//   ps_monster_daemon monster_demon1
//   ps_monster_protected monster_zombie
//
// Copyright (C) 1999 Dennis Chao
// Copyright (C) 2000 David Koppenhofer
// Copyright (C) 2026 psdoom-plus contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "quakedef.h"
#include "sv_psd.h"
#include "psd.h"

static cvar_t	ps_monsters = {"ps_monsters", "16", CVAR_ARCHIVE};
static cvar_t	ps_monster_user = {"ps_monster_user", "monster_army", CVAR_ARCHIVE};
static cvar_t	ps_monster_daemon = {"ps_monster_daemon", "monster_demon1", CVAR_ARCHIVE};
static cvar_t	ps_monster_protected = {"ps_monster_protected", "monster_zombie", CVAR_ARCHIVE};

enum { PM_ALIVE = 1, PM_DEAD, PM_SPARED };

typedef struct {
	long	pid;
	int	ent;			// edict number
	int	state;
	int	health;			// last seen, for psd_wound
	int	spawnhealth;
	double	confirm_until;		// root process: a second kill before this
} psmon_t;

#define PSQ_MAX	64

static psd_ctx	*ctx;
static qboolean	tried, no_safety, active;
static psmon_t	mons[PSQ_MAX];
static int	nmons;
static double	poll_every, next_poll;
static dfunction_t *fn_damage, *fn_killed;
static qboolean	removing;		// our own ED_Free: don't psd_forget

// where monsters can stand: the level's own monsters and player starts
#define PSQ_MAX_SPOTS 256
static vec3_t	spots[PSQ_MAX_SPOTS];
static float	spot_yaw[PSQ_MAX_SPOTS];
static int	nspots;

// the crosshair label, per client
static int	label_ent[MAX_SCOREBOARD];
static double	label_time[MAX_SCOREBOARD];

// -pstest
static char	*test_glob, *test_shot;
static qboolean	test_view, test_firing;
static int	test_state;		// 0 off, 1 targets, 2 removal, 3 picture, 4 done
static double	test_start, test_deadline, test_shot_at;
static long	test_pids[PSQ_MAX];
static int	test_ntargets, test_nremoved, test_view_ent = -1;
static vec3_t	test_me, test_me_angles, test_mon_at;

// ------------------------------------------------------------------

static dfunction_t *find_function (const char *name)
{
	int i;

	for (i = 0; i < progs->numfunctions; i++)
		if (!strcmp (PR_GetString (pr_functions[i].s_name), name))
			return &pr_functions[i];
	return NULL;
}

static psmon_t *mon_for_pid (long pid)
{
	int i;

	for (i = 0; i < nmons; i++)
		if (mons[i].pid == pid)
			return &mons[i];
	return NULL;
}

static psmon_t *mon_for_ent (edict_t *e)
{
	int i, n = NUM_FOR_EDICT (e);

	for (i = 0; i < nmons; i++)
		if (mons[i].ent == n)
			return &mons[i];
	return NULL;
}

static void drop_mon (psmon_t *m)
{
	*m = mons[--nmons];
}

static edict_t *mon_edict (psmon_t *m)
{
	return EDICT_NUM (m->ent);
}

static void say (edict_t *to, const char *text)
{
	int c = NUM_FOR_EDICT (to) - 1;

	Con_Printf ("psdoom: %s\n", text);
	if (c < 0 || c >= svs.maxclients || !svs.clients[c].active)
		return;
	MSG_WriteByte (&svs.clients[c].message, svc_centerprint);
	MSG_WriteString (&svs.clients[c].message, text);
}

static qboolean is_player (edict_t *e)
{
	int c = NUM_FOR_EDICT (e) - 1;

	return c >= 0 && c < svs.maxclients && svs.clients[c].active;
}

// ------------------------------------------------------------------
// spawning

// the kinds this map uses: fixed while it loads, which is when Quake allows
// precaching (a later cvar change applies from the next map)
static char	kind[3][64];		// user, daemon, protected

static qboolean call_spawn (const char *classname, edict_t *ent)
{
	dfunction_t	*f = find_function (classname);
	server_state_t	state = sv.state;

	if (!f)
		return false;
	ent->v.classname = PR_SetEngineString (classname);
	pr_global_struct->self = EDICT_TO_PROG (ent);
	// the spawn function precaches its models and sounds, which Quake only
	// allows while a map loads: PSQ_MapLoaded did that for these kinds, so
	// it only finds them in the lists now
	sv.state = ss_loading;
	PR_ExecuteProgram (f - pr_functions);
	sv.state = state;
	return !ent->free;
}

static void collect_spots (void)
{
	int i;

	nspots = 0;
	for (i = 1; i < sv.num_edicts && nspots < PSQ_MAX_SPOTS; i++)
	{
		edict_t *e = EDICT_NUM (i);
		const char *cn;

		if (e->free)
			continue;
		cn = PR_GetString (e->v.classname);
		if (strncmp (cn, "monster_", 8) && strncmp (cn, "info_player_", 12))
			continue;
		if (!strcmp (cn, "monster_fish") || !strcmp (cn, "monster_wizard"))
			continue;	// in water, or flying
		VectorCopy (e->v.origin, spots[nspots]);
		spot_yaw[nspots] = e->v.angles[1];
		nspots++;
	}
}

static const char *template_for (const psd_proc *p)
{
	return p->is_protected ? kind[2] : p->is_daemon ? kind[1] : kind[0];
}

// a monster for this process, on free floor near one of the level's spots
static edict_t *spawn_process (const psd_proc *p)
{
	const char	*tmpl;
	int		attempt;

	if (!nspots)
		return NULL;
	tmpl = template_for (p);
	for (attempt = 0; attempt < 16; attempt++)
	{
		int	s = (int)((p->pid * 7 + attempt * 13) % nspots);
		edict_t	*ent = ED_Alloc ();
		trace_t	tr;

		ent->v.origin[0] = spots[s][0] + (attempt ? ((attempt * 37 + p->pid) % 161) - 80 : 0);
		ent->v.origin[1] = spots[s][1] + (attempt ? ((attempt * 53 + p->pid) % 161) - 80 : 0);
		ent->v.origin[2] = spots[s][2];
		ent->v.angles[1] = spot_yaw[s] + (p->pid % 8) * 45;
		if (!call_spawn (tmpl, ent))
			return NULL;		// the spawn function removed it (deathmatch?)
		tr = SV_Move (ent->v.origin, ent->v.mins, ent->v.maxs, ent->v.origin, MOVE_NORMAL, ent);
		if (!tr.startsolid && !tr.allsolid)
			return ent;
		removing = true;
		ED_Free (ent);
		removing = false;
	}
	return NULL;
}

static void vanish (edict_t *e)
{
	removing = true;
	ED_Free (e);
	removing = false;
}

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

static int add_mon (const psd_proc *p)
{
	edict_t *e;

	if (nmons >= PSQ_MAX || nmons >= ps_monsters.value)
		return 0;
	e = spawn_process (p);
	if (!e)
		return 0;			// no room now: libpsd offers it again later
	mons[nmons].pid = p->pid;
	mons[nmons].ent = NUM_FOR_EDICT (e);
	mons[nmons].state = PM_ALIVE;
	mons[nmons].health = mons[nmons].spawnhealth = (int)e->v.health;
	mons[nmons].confirm_until = 0;
	nmons++;
	// a process is not one of the level's monsters
	if (pr_global_struct->total_monsters > 0)
		pr_global_struct->total_monsters -= 1;
	Con_Printf ("psdoom: %ld [%s] spawned\n", p->pid, p->label);
	return 1;
}

static int on_event (void *user, psd_event ev, const psd_proc *p)
{
	psmon_t	*m = mon_for_pid (p->pid);
	(void)user;

	switch (ev)
	{
	case PSD_EV_SPAWN:
		if (m)
		{
			if (m->state == PM_ALIVE)
				return 1;
			drop_mon (m);		// a corpse from an earlier life
		}
		return add_mon (p);

	case PSD_EV_UPDATE:
		return 1;

	case PSD_EV_RESURRECT:
		// Quake has no archvile: the corpse stays, a new monster appears
		if (m)
			drop_mon (m);
		if (!add_mon (p))
			return 0;
		Con_Printf ("psdoom: %ld [%s] lives on\n", p->pid, p->label);
		return 1;

	case PSD_EV_REMOVE:
		test_note_removed (p->pid);
		if (m)
		{
			edict_t *e = mon_edict (m);

			if (m->state != PM_DEAD && !e->free)
			{
				Con_Printf ("psdoom: %ld [%s] gone\n", p->pid, p->label);
				vanish (e);
			}
			drop_mon (m);		// a corpse stays, as in psDooM
		}
		return 1;
	}
	return 0;
}

// ------------------------------------------------------------------

static void open_psd (void)
{
	psd_config	*cfg;
	int		err, p;

	tried = true;
	Cvar_RegisterVariable (&ps_monsters);
	Cvar_RegisterVariable (&ps_monster_user);
	Cvar_RegisterVariable (&ps_monster_daemon);
	Cvar_RegisterVariable (&ps_monster_protected);

	cfg = psd_config_new ();
	psd_config_load_default_file (cfg);
	psd_config_parse_args (cfg, com_argc, com_argv);
	no_safety = psd_config_no_safety (cfg);
	if (psd_config_disabled (cfg))
	{
		Con_Printf ("psdoom: process monitoring off (-nopsmon)\n");
		psd_config_free (cfg);
		return;
	}
	p = COM_CheckParm ("-pstest");
	if (!p || p >= com_argc - 1)
		psd_config_set_max_procs (cfg, PSQ_MAX);	// the busiest first
	ctx = psd_open (cfg, &err);
	psd_config_free (cfg);
	if (!ctx)
	{
		Con_Printf ("psdoom: %s; playing plain Quake\n", psd_strerror (err));
		return;
	}

	// psDooM polled every 256 tics (~7 s); modern mode is livelier
	poll_every = psd_mode_of (ctx) == PSD_MODE_CLASSIC ? 7.3 : 2.0;
	p = COM_CheckParm ("-psinterval");
	if (p && p < com_argc - 1)
		poll_every = atoi (com_argv[p + 1]) / 35.0;	// Doom tics
	p = COM_CheckParm ("-pstest");
	if (p && p < com_argc - 1)
	{
		test_glob = com_argv[p + 1];
		test_state = 1;
		if (poll_every > 1)
			poll_every = 1;
		p = COM_CheckParm ("-pstestshot");
		if (p && p < com_argc - 1)
			test_shot = com_argv[p + 1];
		test_view = COM_CheckParm ("-pstestview") != 0;
	}
	if (poll_every < 0.1)
		poll_every = 0.1;
	Con_Printf ("psdoom-plus %s: %s mode, %s backend\n", PSD_VERSION,
		psd_mode_of (ctx) == PSD_MODE_CLASSIC ? "classic" : "modern",
		psd_backend_name (ctx));
}

void PSQ_MapEnd (void)
{
	nmons = 0;			// the edicts are about to go
	active = false;
	fn_damage = fn_killed = NULL;
	if (ctx)
		psd_reset (ctx);
}

void PSQ_MapLoaded (void)
{
	edict_t	*e;
	int	i;

	if (!tried)
		open_psd ();
	if (!ctx)
		return;
	if (deathmatch.value)
	{
		Con_Printf ("psdoom: %s: no process monsters in deathmatch (QuakeC removes monsters)\n", sv.name);
		return;
	}
	fn_damage = find_function ("T_Damage");
	fn_killed = find_function ("Killed");
	if (!fn_damage || !fn_killed)
	{
		Con_Printf ("psdoom: this progs.dat has no T_Damage/Killed: no process monsters\n");
		return;
	}
	collect_spots ();
	// precaching is only allowed while the map loads: spawn one of each
	// kind now, so that processes can appear later
	for (i = 0; i < 3; i++)
	{
		float total = pr_global_struct->total_monsters;

		q_strlcpy (kind[i], i == 0 ? ps_monster_user.string
			   : i == 1 ? ps_monster_daemon.string : ps_monster_protected.string,
			   sizeof kind[i]);
		e = ED_Alloc ();
		e->v.origin[2] = -99999;
		if (call_spawn (kind[i], e))
			vanish (e);
		else
			Con_Printf ("psdoom: no spawn function %s in this progs.dat\n", kind[i]);
		pr_global_struct->total_monsters = total;
	}
	memset (label_ent, 0, sizeof label_ent);
	active = true;
	next_poll = 0;
	if (test_state == 1)
		test_start = sv.time;
}

void PSQ_Shutdown (void)
{
	if (ctx)
	{
		psd_close (ctx);
		ctx = NULL;
	}
}

void PSQ_Freed (edict_t *ed)
{
	psmon_t *m;

	if (!nmons || removing)
		return;
	m = mon_for_ent (ed);
	if (!m)
		return;
	// QuakeC removed it (gibbed and faded, or a trigger): offer it again
	if (m->state != PM_DEAD && ctx)
		psd_forget (ctx, m->pid);
	drop_mon (m);
}

// ------------------------------------------------------------------
// damage and death, seen from the QuakeC interpreter

qboolean PSQ_Call (dfunction_t *f)
{
	edict_t		*targ, *attacker;
	psmon_t		*m;
	const psd_proc	*p;

	if (!active || (f != fn_damage && f != fn_killed))
		return false;
	targ = G_EDICT (OFS_PARM0);
	if (!nmons || !(m = mon_for_ent (targ)))
		return false;

	if (f == fn_damage)		// T_Damage (targ, inflictor, attacker, damage)
	{
		if (test_firing || m->state == PM_DEAD)
			return false;
		attacker = G_EDICT (OFS_PARM2);
		p = psd_lookup (ctx, m->pid);
		if (p && p->is_protected)
			return true;
		// psDooM: only players may hurt process monsters (not other
		// monsters, not lava or slime), unless -nopssafety
		return !(no_safety || is_player (attacker));
	}

	// Killed (targ, attacker)
	if (m->state == PM_DEAD)
		return false;
	attacker = G_EDICT (OFS_PARM1);
	p = psd_lookup (ctx, m->pid);
	if (p && psd_needs_confirm (ctx, p->pid) && !test_state && sv.time > m->confirm_until)
	{
		char buf[96];

		// a root process: kill it twice (psDooM asked y/n). It comes
		// back at once; a second kill within 30 s kills the process
		q_snprintf (buf, sizeof buf, "root process %ld %s:\nkill it again to kill it",
			    p->pid, p->label);
		say (attacker, buf);
		m->state = PM_SPARED;
		m->confirm_until = sv.time + 30;
		return false;
	}
	m->state = PM_DEAD;
	{
		int rc = psd_kill (ctx, m->pid);

		if (p)
		{
			char buf[96];

			q_snprintf (buf, sizeof buf, rc == PSD_OK ? "killed %ld %s" : "%ld %s survives",
				    p->pid, p->label);
			say (attacker, buf);
		}
	}
	return false;
}

// ------------------------------------------------------------------

static void wounds (void)
{
	int i;

	for (i = 0; i < nmons; i++)
	{
		edict_t *e = mon_edict (&mons[i]);

		if (e->free || mons[i].state != PM_ALIVE)
			continue;
		if ((int)e->v.health < mons[i].health && e->v.health > 0)
			psd_wound (ctx, mons[i].pid, (int)e->v.health, mons[i].spawnhealth);
		mons[i].health = (int)e->v.health;
	}
	// a spared root process is up again: a new monster at once
	for (i = 0; i < nmons; i++)
		if (mons[i].state == PM_SPARED)
		{
			const psd_proc *p = psd_lookup (ctx, mons[i].pid);
			double until = mons[i].confirm_until;

			if (p && add_mon (p))
				mons[nmons - 1].confirm_until = until;
			drop_mon (&mons[i]);
			break;
		}
}

// "pid:command" in the middle of the screen while a player looks at one
static void labels (void)
{
	int c;

	for (c = 0; c < svs.maxclients; c++)
	{
		client_t	*cl = &svs.clients[c];
		edict_t		*me = cl->edict;
		vec3_t		fwd, right, up, start, end;
		trace_t		tr;
		psmon_t		*m;
		const psd_proc	*p;
		char		buf[64];
		int		n;

		if (!cl->active || !me || me->v.health <= 0)
			continue;
		AngleVectors (me->v.v_angle, fwd, right, up);
		VectorAdd (me->v.origin, me->v.view_ofs, start);
		VectorMA (start, 2048, fwd, end);
		tr = SV_Move (start, vec3_origin, vec3_origin, end, MOVE_NORMAL, me);
		if (!tr.ent || tr.ent == sv.edicts || !(m = mon_for_ent (tr.ent))
		    || m->state == PM_DEAD || !(p = psd_lookup (ctx, m->pid)))
			continue;
		n = NUM_FOR_EDICT (tr.ent);
		if (n == label_ent[c] && sv.time - label_time[c] < 1.5)
			continue;
		label_ent[c] = n;
		label_time[c] = sv.time;
		q_snprintf (buf, sizeof buf, "%ld:%s", p->pid, p->comm[0] ? p->comm : p->label);
		MSG_WriteByte (&cl->message, svc_centerprint);
		MSG_WriteString (&cl->message, buf);
	}
}

// ------------------------------------------------------------------
// -pstest

static void test_finish (qboolean ok)
{
	printf ("PSTEST mode=%s targeted=%d removed=%d result=%s maxtic=0ms target=%s\n",
		psd_mode_of (ctx) == PSD_MODE_CLASSIC ? "classic" : "modern",
		test_ntargets, test_nremoved, ok ? "ok" : "fail", test_glob);
	fflush (stdout);
	test_state = 4;
	Cbuf_AddText ("quit\n");
}

static edict_t *first_player (void)
{
	int c;

	for (c = 0; c < svs.maxclients; c++)
		if (svs.clients[c].active && svs.clients[c].edict)
			return svs.clients[c].edict;
	return NULL;
}

static void place_player (qboolean first)
{
	edict_t *me = first_player (), *mon;

	if (!me || test_view_ent < 0)
		return;
	mon = EDICT_NUM (test_view_ent);
	VectorCopy (test_me, me->v.origin);
	(me->v.velocity)[0] = (me->v.velocity)[1] = (me->v.velocity)[2] = 0;
	VectorCopy (test_me_angles, me->v.angles);
	VectorCopy (test_me_angles, me->v.v_angle);
	me->v.fixangle = 1;
	me->v.flags = (int)me->v.flags | FL_GODMODE;
	SV_LinkEdict (me, false);
	if (!mon->free)
	{
		VectorCopy (test_mon_at, mon->v.origin);
		(mon->v.velocity)[0] = (mon->v.velocity)[1] = (mon->v.velocity)[2] = 0;
		mon->v.nextthink = sv.time + 5;	// hold still for the picture
		SV_LinkEdict (mon, false);
	}
	(void)first;
}

// stand in front of a living process monster, looking at it
static qboolean test_aim (void)
{
	edict_t *me = first_player ();
	int i;

	if (!me)
		return false;
	for (i = 0; i < nmons; i++)
	{
		edict_t	*mon = mon_edict (&mons[i]);
		vec3_t	fwd, right, up, spot, dir, eye;
		trace_t	tr;

		if (mon->free || mons[i].state != PM_ALIVE || mon->v.health <= 0)
			continue;
		AngleVectors (mon->v.angles, fwd, right, up);
		VectorMA (mon->v.origin, 128, fwd, spot);
		tr = SV_Move (mon->v.origin, me->v.mins, me->v.maxs, spot, MOVE_NOMONSTERS, mon);
		if (tr.fraction < 1 || tr.startsolid)
			continue;
		VectorCopy (spot, test_me);
		VectorCopy (mon->v.origin, test_mon_at);
		VectorCopy (spot, eye);
		eye[2] += me->v.view_ofs[2];
		VectorSubtract (mon->v.origin, eye, dir);
		dir[2] += 8;
		VectorAngles (dir, test_me_angles);
		// and the monster faces us
		VectorSubtract (spot, mon->v.origin, dir);
		mon->v.angles[1] = atan2 (dir[1], dir[0]) * 180 / M_PI;
		test_view_ent = mons[i].ent;
		place_player (true);
		return true;
	}
	return false;
}

static void test_frame (void)
{
	int i;
	edict_t *me = first_player ();

	if (me)
		me->v.flags = (int)me->v.flags | FL_GODMODE;	// survive for the picture
	if (test_state == 1 && sv.time - test_start > 3)
	{
		for (i = 0; i < nmons && test_ntargets < PSQ_MAX; i++)
		{
			const psd_proc	*p = psd_lookup (ctx, mons[i].pid);
			edict_t		*e = mon_edict (&mons[i]);

			if (!p || e->free || mons[i].state != PM_ALIVE || fnmatch (test_glob, p->comm, 0))
				continue;
			test_pids[test_ntargets++] = p->pid;
		}
		if (test_ntargets == 0 && sv.time - test_start < 15)
			return;
		// through QuakeC's own T_Damage: a hit (renice), then a kill
		test_firing = true;
		for (i = 0; i < test_ntargets; i++)
		{
			psmon_t *m = mon_for_pid (test_pids[i]);
			edict_t *e;
			int dmg;

			if (!m)
				continue;
			e = mon_edict (m);
			for (dmg = 1; dmg <= 10000 && !e->free && e->v.health > 0 && m->state == PM_ALIVE; dmg *= 10000)
			{
				G_INT (OFS_PARM0) = EDICT_TO_PROG (e);
				G_INT (OFS_PARM1) = EDICT_TO_PROG (sv.edicts);
				G_INT (OFS_PARM2) = EDICT_TO_PROG (sv.edicts);
				G_FLOAT (OFS_PARM3) = dmg;
				pr_global_struct->self = EDICT_TO_PROG (sv.edicts);
				PR_ExecuteProgram (fn_damage - pr_functions);
				wounds ();
			}
		}
		test_firing = false;
		test_state = 2;
		test_deadline = sv.time + 12;
	}
	else if (test_state == 2)
	{
		if ((test_nremoved == test_ntargets && sv.time - test_start > 4) || sv.time >= test_deadline)
		{
			if (test_view && !first_player () && cls.state != ca_dedicated && sv.time - test_start < 60)
				return;		// the local player is still connecting
			if (test_view && test_aim ())
			{
				test_state = 3;
				test_shot_at = sv.time + 1.5;
			}
			else
				test_finish (test_ntargets > 0 && test_nremoved == test_ntargets);
		}
	}
	else if (test_state == 3)
	{
		place_player (false);
		if (sv.time >= test_shot_at)
		{
			if (test_shot)
				Cbuf_AddText ("screenshot png\nwait\nwait\n");	// id1/spasmNNNN.png
			test_finish (test_ntargets > 0 && test_nremoved == test_ntargets);
		}
	}
}

void PSQ_Frame (void)
{
	if (!active || !ctx)
		return;
	psd_tick (ctx);
	if (sv.time >= next_poll)
	{
		psd_poll (ctx, on_event, NULL);
		next_poll = sv.time + poll_every;
	}
	wounds ();
	labels ();
	if (test_state && test_state < 4)
		test_frame ();
}
