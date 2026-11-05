// g_psd.c - psDooM for Quake 3 (OpenArena game code): processes as bots
//
// Quake 3 has no monsters, so every process is a bot named "pid:command"
// (Quake 3's own crosshair names and scoreboard are its labels). Hurting a
// process bot renices the process, fragging it kills the process. libpsd
// decides which processes exist, which are protected and what a kill means,
// exactly as for the Doom engines (engines/common/psdoom/p_psdoom.c).
//
//   +set ps_args "-psclassic -psuser me"   psDooM's options (README.md)
//   +set ps_bots 12                        at most this many process bots
//   +set ps_skill 2                        their bot skill (1-5)
//   +set ps_bot_user Grunt                 bots.txt names for user processes,
//   +set ps_bot_daemon Gargoyle            daemons (psDooM's Demons)
//   +set ps_bot_protected Penguin          and protected ones (modern mode)
//
// Copyright (C) 1999 Dennis Chao
// Copyright (C) 2000 David Koppenhofer
// Copyright (C) 2026 psdoom-plus contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <fnmatch.h>
#include <stdlib.h>
#include <string.h>

#include "g_local.h"
#include "psd.h"

enum { PB_FREE, PB_ALIVE, PB_DEAD, PB_SPARED };

typedef struct {
	long	pid;
	int	state;
	int	died;			// level.time of the frag
	int	confirm_until;		// root process: a second frag before this kills
	qboolean removing;		// we dropped it: don't psd_forget
} psbot_t;

static psd_ctx	*ctx;
static psbot_t	bots[MAX_CLIENTS];
static qboolean	no_safety;
static int	poll_ms, next_poll;
static int	max_bots;

static vmCvar_t	ps_args, ps_bots, ps_skill, ps_bot_user, ps_bot_daemon, ps_bot_protected;

// -pstest: the automated check used by tests/vm/doom-pstest.sh
static char	test_glob[64];
static char	test_shot[64];
static qboolean	test_view;
static int	test_state;		// 0 off, 1 waiting for targets, 2 waiting for removal,
					// 3 picture, 4 quitting
static int	test_start, test_deadline, test_shot_at;
static long	test_pids[MAX_CLIENTS];
static int	test_ntargets, test_nremoved;
static qboolean	test_firing;		// our own shots get past the damage rules
static int	test_last_ms, test_max_gap;

#define PSQ3_ARGV_MAX 64

// ------------------------------------------------------------------

static int bot_of( gentity_t *ent ) {
	int c;

	if ( !ctx || !ent || !ent->client ) {
		return -1;
	}
	c = ent - g_entities;
	if ( c < 0 || c >= MAX_CLIENTS || bots[c].state == PB_FREE ) {
		return -1;
	}
	return c;
}

static int bot_for_pid( long pid ) {
	int c;

	for ( c = 0; c < MAX_CLIENTS; c++ ) {
		if ( bots[c].state != PB_FREE && bots[c].pid == pid ) {
			return c;
		}
	}
	return -1;
}

static int count_bots( void ) {
	int c, n = 0;

	for ( c = 0; c < MAX_CLIENTS; c++ ) {
		if ( bots[c].state != PB_FREE ) {
			n++;
		}
	}
	return n;
}

static qboolean is_human( gentity_t *ent ) {
	return ent && ent->client && !( ent->r.svFlags & SVF_BOT )
		&& ent->client->pers.connected == CON_CONNECTED;
}

static void say( gentity_t *to, const char *text ) {
	if ( is_human( to ) ) {
		trap_SendServerCommand( to - g_entities, va( "cp \"%s\n\"", text ) );
	}
	G_Printf( "psdoom: %s\n", text );
}

// "pid:command", safe for a userinfo string
static void bot_name( const psd_proc *p, char *out, int size ) {
	char *s;

	Com_sprintf( out, size, "%ld:%s", p->pid, p->comm[0] ? p->comm : p->label );
	for ( s = out; *s; s++ ) {
		if ( *s == '\\' || *s == ';' || *s == '"' || *s == '^' || *s < ' ' ) {
			*s = '_';
		}
	}
}

static void drop_bot( int c, const char *why ) {
	bots[c].removing = qtrue;
	trap_DropClient( c, why );		// ClientDisconnect -> PSQ3_ClientGone
	memset( &bots[c], 0, sizeof bots[c] );
}

static void test_note_removed( long pid ) {
	int i;

	for ( i = 0; i < test_ntargets; i++ ) {
		if ( test_pids[i] == pid ) {
			test_pids[i] = -1;
			test_nremoved++;
		}
	}
}

static int on_event( void *user, psd_event ev, const psd_proc *p ) {
	int	c = bot_for_pid( p->pid );
	char	name[MAX_NETNAME], userinfo[MAX_INFO_STRING];
	const char *tmpl;
	(void)user;

	switch ( ev ) {
	case PSD_EV_SPAWN:
		if ( c >= 0 ) {			// still here from an earlier life
			bots[c].state = PB_ALIVE;
			return 1;
		}
		if ( count_bots() >= max_bots ) {
			return 0;		// no room now: libpsd offers it again later
		}
		tmpl = p->is_protected ? ps_bot_protected.string
			: p->is_daemon ? ps_bot_daemon.string : ps_bot_user.string;
		bot_name( p, name, sizeof name );
		c = G_AddBot( tmpl, ps_skill.value, NULL, 0, name );
		if ( c < 0 || c >= MAX_CLIENTS ) {
			return 0;
		}
		// marks it as a process bot across a map change (PSQ3_StaleBot)
		trap_GetUserinfo( c, userinfo, sizeof userinfo );
		Info_SetValueForKey( userinfo, "psd_pid", va( "%ld", p->pid ) );
		trap_SetUserinfo( c, userinfo );
		bots[c].pid = p->pid;
		bots[c].state = PB_ALIVE;
		G_Printf( "psdoom: %ld [%s] joined as client %d\n", p->pid, p->label, c );
		return 1;

	case PSD_EV_UPDATE:
		return 1;

	case PSD_EV_RESURRECT:
		if ( c < 0 ) {
			return 0;
		}
		bots[c].state = PB_ALIVE;	// let it respawn
		G_Printf( "psdoom: %ld [%s] lives on\n", p->pid, p->label );
		return 1;

	case PSD_EV_REMOVE:
		test_note_removed( p->pid );
		if ( c >= 0 ) {
			G_Printf( "psdoom: %ld [%s] gone\n", p->pid, p->label );
			drop_bot( c, "process ended" );
		}
		return 1;
	}
	return 0;
}

// ------------------------------------------------------------------

static int split_args( char *s, char **argv, int max ) {
	int argc = 0;

	argv[argc++] = "psdoom-q3";
	while ( *s && argc < max - 1 ) {
		while ( *s == ' ' || *s == '\t' ) {
			*s++ = 0;
		}
		if ( !*s ) {
			break;
		}
		argv[argc++] = s;
		while ( *s && *s != ' ' && *s != '\t' ) {
			s++;
		}
	}
	argv[argc] = NULL;
	return argc;
}

static const char *arg_value( int argc, char **argv, const char *name ) {
	int i;

	for ( i = 1; i < argc - 1; i++ ) {
		if ( !strcmp( argv[i], name ) ) {
			return argv[i + 1];
		}
	}
	return NULL;
}

static qboolean arg_present( int argc, char **argv, const char *name ) {
	int i;

	for ( i = 1; i < argc; i++ ) {
		if ( !strcmp( argv[i], name ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

void PSQ3_Init( int restart ) {
	static char	buf[MAX_CVAR_VALUE_STRING];
	char		*argv[PSQ3_ARGV_MAX];
	const char	*v;
	psd_config	*cfg;
	int		argc, err;
	(void)restart;

	memset( bots, 0, sizeof bots );
	trap_Cvar_Register( &ps_args, "ps_args", "", CVAR_ARCHIVE );
	trap_Cvar_Register( &ps_bots, "ps_bots", "12", CVAR_ARCHIVE );
	trap_Cvar_Register( &ps_skill, "ps_skill", "2", CVAR_ARCHIVE );
	trap_Cvar_Register( &ps_bot_user, "ps_bot_user", "Grunt", CVAR_ARCHIVE );
	trap_Cvar_Register( &ps_bot_daemon, "ps_bot_daemon", "Gargoyle", CVAR_ARCHIVE );
	trap_Cvar_Register( &ps_bot_protected, "ps_bot_protected", "Penguin", CVAR_ARCHIVE );

	Q_strncpyz( buf, ps_args.string, sizeof buf );
	argc = split_args( buf, argv, PSQ3_ARGV_MAX );

	cfg = psd_config_new();
	psd_config_load_default_file( cfg );
	psd_config_parse_args( cfg, argc, argv );
	no_safety = psd_config_no_safety( cfg );
	if ( psd_config_disabled( cfg ) ) {
		G_Printf( "psdoom: process monitoring off (-nopsmon)\n" );
		psd_config_free( cfg );
		return;
	}
	if ( !trap_Cvar_VariableIntegerValue( "bot_enable" ) ) {
		G_Printf( "psdoom: bot_enable is 0, so no process bots\n" );
		psd_config_free( cfg );
		return;
	}

	// one slot stays free for a player; libpsd keeps the busiest processes
	max_bots = ps_bots.integer;
	if ( ( v = arg_value( argc, argv, "-psmax" ) ) != NULL && atoi( v ) > 0
		&& atoi( v ) < max_bots ) {
		max_bots = atoi( v );
	}
	if ( max_bots > level.maxclients - 1 ) {
		max_bots = level.maxclients - 1;
	}
	if ( max_bots < 1 ) {
		G_Printf( "psdoom: no free client slots (raise sv_maxclients)\n" );
		psd_config_free( cfg );
		return;
	}
	psd_config_set_max_procs( cfg, max_bots );

	ctx = psd_open( cfg, &err );
	psd_config_free( cfg );
	if ( !ctx ) {
		G_Printf( "psdoom: %s; playing plain OpenArena\n", psd_strerror( err ) );
		return;
	}

	// psDooM polled every 256 tics (~7 s); modern mode is livelier
	poll_ms = psd_mode_of( ctx ) == PSD_MODE_CLASSIC ? 7300 : 2000;
	if ( ( v = arg_value( argc, argv, "-psinterval" ) ) != NULL ) {
		poll_ms = atoi( v ) * 1000 / 35;	// Doom tics, as for the Doom engines
	}

	test_state = 0;
	if ( ( v = arg_value( argc, argv, "-pstest" ) ) != NULL ) {
		Q_strncpyz( test_glob, v, sizeof test_glob );
		test_state = 1;
		test_ntargets = test_nremoved = 0;
		test_max_gap = test_last_ms = 0;
		test_start = level.time;
		if ( poll_ms > 1000 ) {
			poll_ms = 1000;
		}
		v = arg_value( argc, argv, "-pstestshot" );
		Q_strncpyz( test_shot, v ? v : "", sizeof test_shot );
		test_view = arg_present( argc, argv, "-pstestview" );
	}
	if ( poll_ms < 50 ) {
		poll_ms = 50;
	}
	next_poll = level.time + 1000;		// let the level settle first

	G_Printf( "psdoom-plus %s: %s mode, %s backend, up to %d process bots\n", PSD_VERSION,
		psd_mode_of( ctx ) == PSD_MODE_CLASSIC ? "classic" : "modern",
		psd_backend_name( ctx ), max_bots );
}

void PSQ3_Shutdown( int restart ) {
	(void)restart;
	// the bots stay connected through a map change, but this module is
	// unloaded: PSQ3_StaleBot drops them and the next map adds them again
	if ( ctx ) {
		psd_close( ctx );
		ctx = NULL;
	}
	memset( bots, 0, sizeof bots );
}

// ------------------------------------------------------------------
// -pstest

static void test_finish( qboolean ok ) {
	G_Printf( "PSTEST mode=%s targeted=%d removed=%d result=%s maxtic=%dms target=%s\n",
		psd_mode_of( ctx ) == PSD_MODE_CLASSIC ? "classic" : "modern",
		test_ntargets, test_nremoved, ok ? "ok" : "fail", test_max_gap, test_glob );
	test_state = 4;
	trap_SendConsoleCommand( EXEC_APPEND, "quit\n" );
}

static gentity_t *first_human( void ) {
	int c;

	for ( c = 0; c < level.maxclients; c++ ) {
		if ( is_human( &g_entities[c] ) ) {
			return &g_entities[c];
		}
	}
	return NULL;
}

// the picture: stand in front of a living process bot, facing it (its name
// shows under the crosshair), and hold both still until the shot
static int	pic_bot = -1;
static vec3_t	pic_me, pic_bot_at, pic_me_angles, pic_bot_angles;

static void place( gentity_t *ent, vec3_t at, vec3_t angles, qboolean teleport ) {
	VectorCopy( at, ent->client->ps.origin );
	VectorClear( ent->client->ps.velocity );
	if ( teleport ) {
		ent->client->ps.eFlags ^= EF_TELEPORT_BIT;
	}
	SetClientViewAngle( ent, angles );
	G_SetOrigin( ent, at );
	trap_LinkEntity( ent );
}

static void test_hold( void ) {
	gentity_t *me = first_human(), *bot;

	if ( !me || pic_bot < 0 ) {
		return;
	}
	bot = &g_entities[pic_bot];
	if ( !bot->inuse || bot->health <= 0 ) {
		return;
	}
	place( me, pic_me, pic_me_angles, qfalse );
	place( bot, pic_bot_at, pic_bot_angles, qfalse );
}

static qboolean test_aim( void ) {
	gentity_t	*me = first_human(), *bot;
	vec3_t		fwd, angles, eye, dir, spot;
	int		c;

	if ( !me ) {
		return qfalse;
	}
	if ( me->health <= 0 ) {
		ClientRespawn( me );		// telefragged by a bot that spawned on us
	}
	for ( c = 0; c < MAX_CLIENTS; c++ ) {
		trace_t tr;

		bot = &g_entities[c];
		if ( bots[c].state == PB_FREE || bots[c].state == PB_DEAD || !bot->inuse
			|| bot->health <= 0 || bot->client->ps.groundEntityNum == ENTITYNUM_NONE ) {
			continue;
		}
		VectorSet( angles, 0, bot->client->ps.viewangles[YAW], 0 );
		AngleVectors( angles, fwd, NULL, NULL );
		VectorMA( bot->r.currentOrigin, 144, fwd, spot );
		// open floor in front of it, not a wall or a drop
		trap_Trace( &tr, bot->r.currentOrigin, me->r.mins, me->r.maxs, spot, c, MASK_PLAYERSOLID );
		if ( tr.fraction < 1.0f || tr.startsolid ) {
			continue;
		}
		VectorCopy( spot, eye );
		eye[2] -= 64;
		trap_Trace( &tr, spot, me->r.mins, me->r.maxs, eye, c, MASK_PLAYERSOLID );
		if ( tr.fraction >= 1.0f ) {
			continue;
		}
		VectorCopy( spot, pic_me );
		VectorCopy( bot->r.currentOrigin, pic_bot_at );
		VectorCopy( spot, eye );
		eye[2] += me->client->ps.viewheight;
		VectorSubtract( bot->r.currentOrigin, eye, dir );
		dir[2] += 12;
		vectoangles( dir, pic_me_angles );
		VectorSubtract( spot, bot->r.currentOrigin, dir );
		dir[2] = 0;
		vectoangles( dir, pic_bot_angles );
		pic_bot = c;
		place( me, pic_me, pic_me_angles, qtrue );
		place( bot, pic_bot_at, pic_bot_angles, qfalse );
		me->flags |= FL_GODMODE;
		return qtrue;
	}
	return qfalse;
}

static void test_frame( void ) {
	int	c, t = trap_Milliseconds();
	gentity_t *ent;

	if ( test_last_ms && level.time - test_start > 1000 && t - test_last_ms > test_max_gap ) {
		test_max_gap = t - test_last_ms;
	}
	test_last_ms = t;
	if ( ( ent = first_human() ) != NULL ) {
		ent->flags |= FL_GODMODE;	// the bots fight back: survive for the picture
	}

	if ( test_state == 1 && level.time - test_start > 3000 ) {
		// shoot every process bot whose command matches, through the normal
		// damage path: a hit (renice), then a frag (kill)
		for ( c = 0; c < MAX_CLIENTS && test_ntargets < MAX_CLIENTS; c++ ) {
			const psd_proc *p;

			ent = &g_entities[c];
			if ( bots[c].state != PB_ALIVE || !ent->inuse || ent->health <= 0 ) {
				continue;
			}
			p = psd_lookup( ctx, bots[c].pid );
			if ( !p || fnmatch( test_glob, p->comm, 0 ) ) {
				continue;
			}
			test_pids[test_ntargets++] = p->pid;
		}
		// a full server may not have room for it yet: wait a while
		if ( test_ntargets == 0 && level.time - test_start < 15000 ) {
			return;
		}
		test_firing = qtrue;
		for ( c = 0; c < test_ntargets; c++ ) {
			int b = bot_for_pid( test_pids[c] );

			if ( b < 0 ) {
				continue;
			}
			ent = &g_entities[b];
			G_Damage( ent, NULL, NULL, NULL, NULL, 1, DAMAGE_NO_ARMOR | DAMAGE_NO_PROTECTION, MOD_UNKNOWN );
			if ( ent->health > 0 ) {
				G_Damage( ent, NULL, NULL, NULL, NULL, 10000,
					DAMAGE_NO_ARMOR | DAMAGE_NO_PROTECTION, MOD_UNKNOWN );
			}
		}
		test_firing = qfalse;
		test_state = 2;
		test_deadline = level.time + 12000;
	} else if ( test_state == 2 ) {
		if ( ( test_nremoved == test_ntargets && level.time - test_start > 4000 )
			|| level.time >= test_deadline ) {
			if ( test_view && !first_human() && !trap_Cvar_VariableIntegerValue( "dedicated" )
				&& level.time - test_start < 60000 ) {
				return;		// the local player is still loading
			}
			if ( test_view && test_aim() ) {
				test_state = 3;
				test_shot_at = level.time + 1500;
			} else {
				test_finish( test_ntargets > 0 && test_nremoved == test_ntargets );
			}
		}
	} else if ( test_state == 3 ) {
		test_hold();
		if ( level.time >= test_shot_at ) {
			// the view, then the scoreboard: every process bot by name
			if ( test_shot[0] ) {
				trap_SendConsoleCommand( EXEC_APPEND, va( "screenshotJPEG %s\nwait 5\n"
					"+scores\nwait 30\nscreenshotJPEG %s-scores\nwait 20\n",
					test_shot, test_shot ) );
			}
			test_finish( test_ntargets > 0 && test_nremoved == test_ntargets );
		}
	}
}

void PSQ3_Frame( void ) {
	int c;

	if ( !ctx ) {
		return;
	}
	psd_tick( ctx );
	if ( level.time >= next_poll && !level.intermissiontime ) {
		psd_poll( ctx, on_event, NULL );
		next_poll = level.time + poll_ms;
	}
	// a frag that libpsd never answered (the process neither went nor
	// came back): don't keep the bot down for ever
	for ( c = 0; c < MAX_CLIENTS; c++ ) {
		if ( bots[c].state == PB_DEAD && level.time - bots[c].died > 20000 ) {
			bots[c].state = PB_SPARED;
		}
	}
	if ( test_state && test_state < 4 ) {
		test_frame();
	}
}

// ------------------------------------------------------------------
// damage, death, respawn

qboolean PSQ3_DamageAllowed( gentity_t *targ, gentity_t *attacker, int mod ) {
	int		c = bot_of( targ );
	const psd_proc	*p;
	(void)mod;

	if ( c < 0 || test_firing ) {
		return qtrue;
	}
	p = psd_lookup( ctx, bots[c].pid );
	if ( p && p->is_protected ) {
		return qfalse;
	}
	// psDooM: only players may hurt process monsters (not other bots, not
	// lava or falling), unless -nopssafety
	return no_safety || ( is_human( attacker ) && bot_of( attacker ) < 0 );
}

void PSQ3_Damaged( gentity_t *targ ) {
	int c = bot_of( targ );
	int max;

	if ( c < 0 || bots[c].state == PB_DEAD || targ->health <= 0 ) {
		return;
	}
	max = targ->client->ps.stats[STAT_MAX_HEALTH];
	if ( max <= 0 ) {
		max = 100;
	}
	psd_wound( ctx, bots[c].pid, targ->health < max ? targ->health : max, max );
}

void PSQ3_Killed( gentity_t *self, gentity_t *attacker ) {
	int		c = bot_of( self ), rc;
	const psd_proc	*p;
	char		buf[96];

	if ( c < 0 || bots[c].state == PB_DEAD || bots[c].removing ) {
		return;		// (a spared root process can be fragged again)
	}
	p = psd_lookup( ctx, bots[c].pid );

	// a root process: frag it twice to kill it (psDooM asked y/n)
	if ( p && psd_needs_confirm( ctx, p->pid ) && !test_state && level.time > bots[c].confirm_until ) {
		bots[c].confirm_until = level.time + 30000;
		bots[c].state = PB_SPARED;
		Com_sprintf( buf, sizeof buf, "root process %ld %s:\nfrag it again to kill it", p->pid, p->label );
		say( attacker, buf );
		return;
	}

	bots[c].state = PB_DEAD;		// stays down until libpsd answers
	bots[c].died = level.time;
	rc = psd_kill( ctx, bots[c].pid );
	if ( p ) {
		Com_sprintf( buf, sizeof buf, rc == PSD_OK ? "killed %ld %s" : "%ld %s survives",
			p->pid, p->label );
		say( attacker, buf );
	}
}

qboolean PSQ3_HoldRespawn( gentity_t *ent ) {
	int c = bot_of( ent );

	return c >= 0 && bots[c].state == PB_DEAD;
}

qboolean PSQ3_StaleBot( const char *userinfo ) {
	return Info_ValueForKey( userinfo, "psd_pid" )[0] != '\0';
}

void PSQ3_ClientGone( int clientNum ) {
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || bots[clientNum].state == PB_FREE ) {
		return;
	}
	// kicked by someone else: libpsd offers the process again
	if ( !bots[clientNum].removing && ctx ) {
		psd_forget( ctx, bots[clientNum].pid );
	}
	memset( &bots[clientNum], 0, sizeof bots[clientNum] );
}
