// psdoom_zs.cpp - libpsd for GZDoom and UZDoom
//
// The natives behind the ZScript struct Psdoom (psdoom.zs). libpsd's ps_events
// are queued here and handled by PsdoomHandler on the play side; a monster
// that can't be placed is handed back with Psdoom.Forget(), so libpsd offers
// the process again on a later poll, as psDooM did.
//
// Copyright (C) 1999 Dennis Chao
// Copyright (C) 2000 David Koppenhofer
// Copyright (C) 2026 psdoom-plus contributors
// SPDX-License-Identifier: GPL-2.0-or-later (built into GPL-3.0 GZDoom/UZDoom)

#include <fnmatch.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "vm.h"
#include "m_argv.h"
#include "c_dispatch.h"
#include "printf.h"
#include "g_game.h"

#include "psd.h"

static psd_ctx*	ctx;
static bool	tried;
static bool	no_safety;
static bool	custom_level;
static int	interval = 70;
static std::string test_glob, test_shot;

struct PsEvent
{
	int type;
	int pid;
	bool daemon;
	bool prot;
	char label[PSD_LABEL_LEN + 1];
};
static std::vector<PsEvent> ps_events;

static int on_event(void*, psd_event ev, const psd_proc* p)
{
	PsEvent e;
	e.type = (int)ev;
	e.pid = (int)p->pid;
	e.daemon = p->is_daemon != 0;
	e.prot = p->is_protected != 0;
	memcpy(e.label, p->label, sizeof e.label);
	ps_events.push_back(e);
	return 1;	// accepted; the play side calls Forget() if it can't place it
}

static const char* arg_value(const char* name)
{
	for (int i = 1; i + 1 < Args->NumArgs(); i++)
		if (!strcmp(Args->GetArg(i), name))
			return Args->GetArg(i + 1);
	return nullptr;
}

static bool arg_present(const char* name)
{
	for (int i = 1; i < Args->NumArgs(); i++)
		if (!strcmp(Args->GetArg(i), name))
			return true;
	return false;
}

static bool open_ctx()
{
	if (tried)
		return ctx != nullptr;
	tried = true;

	psd_config* cfg = psd_config_new();
	std::vector<char*> av;
	for (int i = 0; i < Args->NumArgs(); i++)
		av.push_back(const_cast<char*>(Args->GetArg(i)));
	psd_config_load_default_file(cfg);
	psd_config_parse_args(cfg, (int)av.size(), av.data());
	no_safety = psd_config_no_safety(cfg) != 0;
	if (psd_config_disabled(cfg))
	{
		Printf("psdoom: process monitoring off (-nopsmon)\n");
		psd_config_free(cfg);
		return false;
	}
	int err = 0;
	ctx = psd_open(cfg, &err);
	psd_config_free(cfg);
	if (!ctx)
	{
		Printf("psdoom: %s; playing plain Doom\n", psd_strerror(err));
		return false;
	}
	interval = psd_mode_of(ctx) == PSD_MODE_CLASSIC ? 256 : 70;
	if (const char* v = arg_value("-psinterval"))
		interval = atoi(v);
	if (const char* v = arg_value("-pstest"))
	{
		test_glob = v;
		if (interval > 35)
			interval = 35;
		if (const char* s = arg_value("-pstestshot"))
			test_shot = s;
	}
	if (interval < 1)
		interval = 1;
	Printf("psdoom-plus %s: %s mode, %s backend, %s\n", PSD_VERSION,
	       psd_mode_of(ctx) == PSD_MODE_CLASSIC ? "classic" : "modern",
	       psd_backend_name(ctx), custom_level ? "custom level" : "stock first map");
	return true;
}

// ---- called from d_main.cpp's AddAutoloadFiles -------------------------

static const char* find_data(const char* name)
{
	static std::string path;
	const char* dirs[] = { getenv("PSDOOM_DATA"), getenv("DOOMWADDIR"),
#ifdef PSDOOM_SHAREDIR
		PSDOOM_SHAREDIR,
#endif
#ifdef PSDOOM_DATADIR
		PSDOOM_DATADIR,
#endif
		"." };
	for (const char* d : dirs)
	{
		if (!d || !*d)
			continue;
		path = std::string(d) + "/" + name;
		if (access(path.c_str(), R_OK) == 0)
			return path.c_str();
	}
	return nullptr;
}

// psdoom1.wad replaces E1M1 (Doom/Ultimate Doom/Freedoom phase 1),
// psdoom2.wad MAP01 (Doom II/Freedoom phase 2) - not shareware, TNT or Plutonia
const char* PS_CustomLevel(const char* autoname)
{
	const char* wad = nullptr;

	if (!autoname || arg_present("-nopslev") || arg_present("-nopsmon"))
		return nullptr;
	std::string a = autoname;
	if (a.find("shareware") != std::string::npos)
		return nullptr;
	if (a.find("doom1") != std::string::npos || a.find("phase1") != std::string::npos)
		wad = find_data("psdoom1.wad");
	else if (a.find("doom2.commercial") != std::string::npos || a.find("phase2") != std::string::npos)
		wad = find_data("psdoom2.wad");
	custom_level = wad != nullptr;
	return wad;
}

// ---- natives -------------------------------------------------------------

DEFINE_ACTION_FUNCTION(_Psdoom, Active)
{
	PARAM_PROLOGUE;
	ACTION_RETURN_BOOL(open_ctx());
}

DEFINE_ACTION_FUNCTION(_Psdoom, Poll)
{
	PARAM_PROLOGUE;
	ps_events.clear();
	if (ctx)
		psd_poll(ctx, on_event, nullptr);
	ACTION_RETURN_INT((int)ps_events.size());
}

static const PsEvent* ev_at(int i)
{
	return (i >= 0 && i < (int)ps_events.size()) ? &ps_events[i] : nullptr;
}

DEFINE_ACTION_FUNCTION(_Psdoom, EventType)
{
	PARAM_PROLOGUE;
	PARAM_INT(i);
	const PsEvent* e = ev_at(i);
	ACTION_RETURN_INT(e ? e->type : 0);
}

DEFINE_ACTION_FUNCTION(_Psdoom, EventPid)
{
	PARAM_PROLOGUE;
	PARAM_INT(i);
	const PsEvent* e = ev_at(i);
	ACTION_RETURN_INT(e ? e->pid : 0);
}

DEFINE_ACTION_FUNCTION(_Psdoom, EventDaemon)
{
	PARAM_PROLOGUE;
	PARAM_INT(i);
	const PsEvent* e = ev_at(i);
	ACTION_RETURN_BOOL(e && e->daemon);
}

DEFINE_ACTION_FUNCTION(_Psdoom, EventProtected)
{
	PARAM_PROLOGUE;
	PARAM_INT(i);
	const PsEvent* e = ev_at(i);
	ACTION_RETURN_BOOL(e && e->prot);
}

DEFINE_ACTION_FUNCTION(_Psdoom, EventLabel)
{
	PARAM_PROLOGUE;
	PARAM_INT(i);
	const PsEvent* e = ev_at(i);
	ACTION_RETURN_STRING(FString(e ? e->label : ""));
}

DEFINE_ACTION_FUNCTION(_Psdoom, Tick)
{
	PARAM_PROLOGUE;
	if (ctx)
		psd_tick(ctx);
	return 0;
}

DEFINE_ACTION_FUNCTION(_Psdoom, Wound)
{
	PARAM_PROLOGUE;
	PARAM_INT(pid);
	PARAM_INT(health);
	PARAM_INT(spawnhealth);
	ACTION_RETURN_INT(ctx ? psd_wound(ctx, pid, health, spawnhealth) : PSD_ERR);
}

DEFINE_ACTION_FUNCTION(_Psdoom, Kill)
{
	PARAM_PROLOGUE;
	PARAM_INT(pid);
	ACTION_RETURN_INT(ctx ? psd_kill(ctx, pid) : PSD_ERR);
}

DEFINE_ACTION_FUNCTION(_Psdoom, Forget)
{
	PARAM_PROLOGUE;
	PARAM_INT(pid);
	if (ctx)
		psd_forget(ctx, pid);
	return 0;
}

DEFINE_ACTION_FUNCTION(_Psdoom, Reset)
{
	PARAM_PROLOGUE;
	if (ctx)
		psd_reset(ctx);
	return 0;
}

// x, y and angle in z; angle -1 when the attempts are used up
DEFINE_ACTION_FUNCTION(_Psdoom, Place)
{
	PARAM_PROLOGUE;
	PARAM_INT(level);
	PARAM_INT(pid);
	PARAM_INT(radius);
	PARAM_INT(attempt);
	int x = 0, y = 0, angle = -1;
	if (psd_place((psd_level)level, pid, radius, attempt, &x, &y, &angle) != PSD_OK)
		angle = -1;
	ACTION_RETURN_VEC3(DVector3(x, y, angle));
}

DEFINE_ACTION_FUNCTION(_Psdoom, Interval)
{
	PARAM_PROLOGUE;
	ACTION_RETURN_INT(interval);
}

DEFINE_ACTION_FUNCTION(_Psdoom, NoSafety)
{
	PARAM_PROLOGUE;
	ACTION_RETURN_BOOL(no_safety);
}

DEFINE_ACTION_FUNCTION(_Psdoom, CustomLevel)
{
	PARAM_PROLOGUE;
	ACTION_RETURN_BOOL(custom_level);
}

DEFINE_ACTION_FUNCTION(_Psdoom, TestActive)
{
	PARAM_PROLOGUE;
	ACTION_RETURN_BOOL(ctx && !test_glob.empty());
}

DEFINE_ACTION_FUNCTION(_Psdoom, TestMatch)
{
	PARAM_PROLOGUE;
	PARAM_INT(pid);
	const psd_proc* p = ctx ? psd_lookup(ctx, pid) : nullptr;
	ACTION_RETURN_BOOL(p && fnmatch(test_glob.c_str(), p->comm, 0) == 0);
}

DEFINE_ACTION_FUNCTION(_Psdoom, TestReport)
{
	PARAM_PROLOGUE;
	PARAM_INT(targeted);
	PARAM_INT(removed);
	printf("PSTEST mode=%s targeted=%d removed=%d result=%s\n",
	       psd_mode_of(ctx) == PSD_MODE_CLASSIC ? "classic" : "modern",
	       targeted, removed, targeted == removed ? "ok" : "fail");
	fflush(stdout);
	if (!test_shot.empty())
		G_ScreenShot(test_shot.c_str());
	return 0;
}

DEFINE_ACTION_FUNCTION(_Psdoom, Quit)
{
	PARAM_PROLOGUE;
	if (ctx)
		psd_close(ctx);
	ctx = nullptr;
	AddCommandString("quit");
	return 0;
}
