// psdoom.zs - psDooM for GZDoom and UZDoom: processes as monsters
//
// Copied into the engine's wadsrc/static/zscript/ at configure time (natives
// may only be declared in the engine's own resource). The process side is
// libpsd, reached through the natives in psdoom_zs.cpp.
//
// Copyright (C) 1999 Dennis Chao
// Copyright (C) 2000 David Koppenhofer
// Copyright (C) 2026 psdoom-plus contributors
// SPDX-License-Identifier: GPL-2.0-or-later

struct Psdoom native
{
	enum EEvent { EV_SPAWN = 1, EV_UPDATE, EV_RESURRECT, EV_REMOVE }
	enum ELevel { LV_PSDOOM = 0, LV_E1M1, LV_MAP01 }

	native static bool Active();
	native static int Poll();
	native static int EventType(int i);
	native static int EventPid(int i);
	native static bool EventDaemon(int i);
	native static bool EventProtected(int i);
	native static String EventLabel(int i);
	native static void Tick();
	native static int Wound(int pid, int health, int spawnhealth);
	native static int Kill(int pid);
	native static void Forget(int pid);
	native static void Reset();
	native static Vector3 Place(int level, int pid, int radius, int attempt);
	native static int Interval();
	native static bool NoSafety();
	native static bool CustomLevel();
	native static bool TestActive();
	native static bool TestMatch(int pid);
	native static void TestReport(int targeted, int removed);
	native static void Quit();
}

// A monster that is a process. Only players may hurt it (psDooM's safety,
// lifted by -nopssafety); its first hits renice, its death kills.
mixin class PsdoomProcess
{
	int ps_pid;
	String ps_label;
	bool ps_removing;
	bool ps_protected;

	override int DamageMobj(Actor inflictor, Actor source, int damage, Name mod, int flags, double angle)
	{
		if (ps_pid)
		{
			if (ps_protected)
				return 0;
			if (!Psdoom.NoSafety() && !(source && source.player))
				return 0;
			if (health > 0 && damage > 0)
				Psdoom.Wound(ps_pid, health - damage, SpawnHealth());
		}
		return Super.DamageMobj(inflictor, source, damage, mod, flags, angle);
	}

	override void Die(Actor source, Actor inflictor, int dmgflags, Name MeansOfDeath)
	{
		if (ps_pid && !ps_removing)
		{
			int rc = Psdoom.Kill(ps_pid);
			if (source && source.player)
				source.player.mo.A_Print(String.Format(rc == 0 ? "killed %d %s" : "%d %s survives", ps_pid, ps_label));
		}
		Super.Die(source, inflictor, dmgflags, MeansOfDeath);
	}
}

class PsdoomShotguy : ShotgunGuy
{
	mixin PsdoomProcess;
	Default { -COUNTKILL; }
}

class PsdoomDemon : Demon
{
	mixin PsdoomProcess;
	Default { -COUNTKILL; }
}

class PsdoomHandler : EventHandler
{
	const PS_ONFLOORZ = -2147483648.0;	// Actor.ONFLOORZ

	Array<Actor> mons;
	Array<bool> seen;		// in the player's line of sight (labels)
	bool active;

	// -pstest
	int testState, testDeadline, testFinishAt, targeted, removed;
	Array<int> testPids;

	override void WorldLoaded(WorldEvent e)
	{
		// pid monsters from a savegame are stale: processes are re-read
		ThinkerIterator it = ThinkerIterator.Create("Actor");
		Actor a;
		while ((a = Actor(it.Next())) != null)
			if (IsProcess(a))
			{
				SetProcess(a, PidOf(a), "", false, true);
				a.Destroy();
			}
		mons.Clear();
		seen.Clear();
		Psdoom.Reset();
		active = Psdoom.Active() && LevelQualifies();
		if (active)
			DoPoll();
	}

	static clearscope bool IsProcess(Actor a)
	{
		return a is "PsdoomShotguy" || a is "PsdoomDemon";
	}

	bool LevelQualifies()
	{
		// psDooM: E1M1 or MAP01, never in demos or netgames
		if (demoplayback || multiplayer)
			return false;
		return level.MapName ~== "E1M1" || level.MapName ~== "MAP01";
	}

	int LevelKind()
	{
		if (Psdoom.CustomLevel())
			return Psdoom.LV_PSDOOM;
		return level.MapName ~== "MAP01" ? Psdoom.LV_MAP01 : Psdoom.LV_E1M1;
	}

	int Find(int pid)
	{
		for (int i = 0; i < mons.Size(); i++)
		{
			let s = PsdoomShotguy(mons[i]);
			let d = PsdoomDemon(mons[i]);
			if ((s && s.ps_pid == pid) || (d && d.ps_pid == pid))
				return i;
		}
		return -1;
	}

	static clearscope int PidOf(Actor a)
	{
		let s = PsdoomShotguy(a);
		if (s) return s.ps_pid;
		let d = PsdoomDemon(a);
		if (d) return d.ps_pid;
		return 0;
	}

	static void SetProcess(Actor a, int pid, String label, bool prot, bool removing)
	{
		let s = PsdoomShotguy(a);
		if (s) { s.ps_pid = pid; s.ps_label = label; s.ps_protected = prot; s.ps_removing = removing; return; }
		let d = PsdoomDemon(a);
		if (d) { d.ps_pid = pid; d.ps_label = label; d.ps_protected = prot; d.ps_removing = removing; }
	}

	static clearscope String LabelOf(Actor a)
	{
		let s = PsdoomShotguy(a);
		if (s) return s.ps_label;
		let d = PsdoomDemon(a);
		if (d) return d.ps_label;
		return "";
	}

	// psDooM's add_new_process(): place it, nudging around collisions
	Actor SpawnProcess(int pid, bool daemon, bool prot, String label)
	{
		class<Actor> cls = daemon ? "PsdoomDemon" : "PsdoomShotguy";
		int radius = int(GetDefaultByType(cls).radius);
		for (int attempt = 0; ; attempt++)
		{
			Vector3 pl = Psdoom.Place(LevelKind(), pid, radius, attempt);
			if (pl.z < 0)
				return null;
			Actor mo = Actor.Spawn(cls, (pl.x, pl.y, PS_ONFLOORZ));
			if (!mo)
				return null;
			if (!mo.TestMobjLocation())
			{
				mo.ClearCounters();
				mo.Destroy();
				continue;
			}
			mo.angle = pl.z;
			SetProcess(mo, pid, label, prot, false);
			return mo;
		}
		return null;
	}

	void DoPoll()
	{
		int n = Psdoom.Poll();
		for (int i = 0; i < n; i++)
		{
			int pid = Psdoom.EventPid(i);
			int idx = Find(pid);
			switch (Psdoom.EventType(i))
			{
			case Psdoom.EV_SPAWN:
				if (idx >= 0)
				{
					mons.Delete(idx);
					seen.Delete(idx);
				}
				Actor mo = SpawnProcess(pid, Psdoom.EventDaemon(i), Psdoom.EventProtected(i), Psdoom.EventLabel(i));
				if (mo)
				{
					mons.Push(mo);
					seen.Push(false);
				}
				else
					Psdoom.Forget(pid);	// no room now: offered again later
				break;

			case Psdoom.EV_UPDATE:
				if (idx >= 0)
					SetProcess(mons[idx], pid, Psdoom.EventLabel(i), Psdoom.EventProtected(i), false);
				break;

			case Psdoom.EV_RESURRECT:
				// the monster died but the process did not: raise it
				if (idx >= 0 && mons[idx].A_RaiseSelf())
				{
					mons[idx].bCOUNTKILL = false;
					break;
				}
				if (idx >= 0)
				{
					Actor body = mons[idx];
					mons.Delete(idx);
					seen.Delete(idx);
					SetProcess(body, pid, "", false, true);
					body.Destroy();
				}
				Psdoom.Forget(pid);
				break;

			case Psdoom.EV_REMOVE:
				if (testPids.Find(pid) != testPids.Size())
					removed++;
				if (idx >= 0)
				{
					Actor mo = mons[idx];
					mons.Delete(idx);
					seen.Delete(idx);
					if (mo.health > 0)
					{
						SetProcess(mo, pid, "", false, true);
						mo.Destroy();		// the process is gone: no corpse
					}
					else
						SetProcess(mo, 0, "", false, false);	// a corpse stays
				}
				break;
			}
		}
	}

	override void WorldThingDestroyed(WorldEvent e)
	{
		// the engine removed a pid monster itself: let libpsd offer it again
		if (!e.thing || !IsProcess(e.thing))
			return;
		int pid = PidOf(e.thing);
		int idx = mons.Find(e.thing);
		if (idx == mons.Size())
			return;
		mons.Delete(idx);
		seen.Delete(idx);
		let s = PsdoomShotguy(e.thing);
		let d = PsdoomDemon(e.thing);
		bool removing = (s && s.ps_removing) || (d && d.ps_removing);
		if (pid && !removing)
			Psdoom.Forget(pid);
	}

	override void WorldTick()
	{
		if (!active)
			return;
		Psdoom.Tick();
		if (level.maptime > 0 && level.maptime % Psdoom.Interval() == 0)
			DoPoll();

		let pmo = players[consoleplayer].mo;
		for (int i = 0; i < mons.Size(); i++)
			seen[i] = pmo && mons[i] && mons[i].health > 0 && pmo.CheckSight(mons[i])
			          && pmo.Distance3D(mons[i]) < 900;

		if (Psdoom.TestActive())
			TestTicker(pmo);
	}

	void TestTicker(Actor shooter)
	{
		if (testState == 0 && level.maptime > 35 * 2)
		{
			for (int i = 0; i < mons.Size(); i++)
			{
				int pid = PidOf(mons[i]);
				if (pid && mons[i].health > 0 && Psdoom.TestMatch(pid))
					testPids.Push(pid);
			}
			if (testPids.Size() == 0 && level.maptime < 35 * 15)
				return;		// wait for room on a crowded level
			targeted = testPids.Size();
			for (int i = 0; i < testPids.Size(); i++)
			{
				int idx = Find(testPids[i]);
				if (idx < 0)
					continue;
				Actor mo = mons[idx];
				mo.DamageMobj(shooter, shooter, 1, 'None');
				if (mo.health > 0)
					mo.DamageMobj(shooter, shooter, 10000, 'None');
			}
			testState = 1;
			testDeadline = level.maptime + 35 * 12;
		}
		else if (testState == 1)
		{
			if ((removed >= targeted && level.maptime > 35 * 4) || level.maptime >= testDeadline)
			{
				// the picture: pid monsters in a row, or failing that one of them
				if (!LineUp(shooter))
					for (int i = 0; i < mons.Size(); i++)
						if (mons[i].health > 0)
						{
							Actor mo = mons[i];
							Vector2 at = mo.pos.xy - Actor.AngleToVector(mo.angle, 160);
							shooter.SetOrigin((at.x, at.y, shooter.pos.z), false);
							shooter.angle = shooter.AngleTo(mo);
							break;
						}
				shooter.player.cheats |= CF_NOTARGET;
				testState = 2;
				testFinishAt = level.maptime + 8;
			}
		}
		else if (testState == 2 && level.maptime >= testFinishAt)
		{
			Psdoom.TestReport(targeted, removed);
			testState = 3;
			testFinishAt = level.maptime + 8;	// let the screenshot happen
		}
		else if (testState == 3 && level.maptime >= testFinishAt)
		{
			Psdoom.Quit();
			testState = 4;
		}
	}

	// -pstestview: up to four living pid monsters (a daemon first) stand in a
	// row across the view, facing the player, so every label is clear of the
	// others, of the weapon and of the screen edges. As test_line_up in
	// engines/common/psdoom/p_psdoom.c: units left of the middle from 240
	// away, the daemon (wider) just right of the middle.
	static const int VIEW_LAT[] = { -60, 155, 54, -155 };
	static const int VIEW_LAT_EVEN[] = { -54, 155, 54, -155 };	// no daemon

	int LineUpAt(Actor shooter, Array<Actor> row, Vector2 c, double a, double d)
	{
		Vector2 p = c - Actor.AngleToVector(a, d);
		if (!shooter.CheckPosition(p))
			return -1;
		int fit = 0, seen = 0;
		Array<bool> solid, shootable;
		for (int i = 0; i < row.Size(); i++)
		{
			// where they stood for the last try doesn't block this one
			solid.Push(row[i].bSolid);
			shootable.Push(row[i].bShootable);
			row[i].bSolid = row[i].bShootable = false;
		}
		shooter.bPickup = false;	// trying spots picks nothing up
		for (int i = 0; i < row.Size(); i++)
		{
			int lat = row[0] is "PsdoomDemon" ? VIEW_LAT[i] : VIEW_LAT_EVEN[i];
			Vector2 at = c + Actor.AngleToVector(a + 90, lat * d / 240);
			if (row[i].CheckPosition(at) && row[i].TeleportMove((at.x, at.y, row[i].pos.z), false))
				fit++;
			row[i].SetZ(row[i].floorz);
			row[i].angle = a + 180;
		}
		for (int i = 0; i < row.Size(); i++)
		{
			row[i].bSolid = solid[i];
			row[i].bShootable = shootable[i];
		}
		shooter.TeleportMove((p.x, p.y, shooter.pos.z), false);
		shooter.SetZ(shooter.floorz);	// not falling into the picture
		shooter.vel = (0, 0, 0);
		shooter.player.viewz = shooter.pos.z + shooter.player.viewheight;
		shooter.angle = a;
		shooter.pitch = 0;
		// seen, and on the player's level (the labels sit right)
		for (int i = 0; i < row.Size(); i++)
			if (shooter.CheckSight(row[i]) && abs(row[i].pos.z - shooter.pos.z) <= 24)
				seen++;
		return fit * 8 + seen;
	}

	bool LineUp(Actor shooter)
	{
		Array<Actor> row;
		Array<double> cx, cy;
		// one daemon, then user processes, then more daemons if that's all there is
		for (int k = 0; k < 3; k++)
			for (int i = 0; i < mons.Size() && row.Size() < (k == 0 ? 1 : 4); i++)
			{
				Actor mo = mons[i];
				if (!mo || mo.health <= 0)
					continue;
				bool daemon = mo is "PsdoomDemon";
				if ((k == 1 ? !daemon : daemon) && (k != 2 || mo != row[0]))
					row.Push(mo);
			}
		if (row.Size() == 0)
			return false;
		// any pid monster's spot may be the middle of the row; the others
		// leave the picture (the game ends after it)
		for (int i = 0; i < mons.Size(); i++)
		{
			Actor mo = mons[i];
			if (!mo || mo.health <= 0)
				continue;
			cx.Push(mo.pos.x);
			cy.Push(mo.pos.y);
			if (row.Find(mo) == row.Size())
			{
				mo.bInvisible = true;
				mo.bSolid = mo.bShootable = false;
			}
		}
		double a0 = row[0].angle;
		int best = -1, bestDir = 0, bestC = 0;
		double bestD = 240;
		for (double d = 240; d >= 160; d -= 40)	// the farthest that works
			for (int c = 0; c < cx.Size(); c++)
				for (int k = 0; k < 16; k++)
				{
					int score = LineUpAt(shooter, row, (cx[c], cy[c]), a0 + k * 22.5, d);
					if (score > best)
					{
						best = score;
						bestDir = k;
						bestC = c;
						bestD = d;
					}
				}
		shooter.bPickup = true;
		if (best < 0)
			return false;
		LineUpAt(shooter, row, (cx[bestC], cy[bestC]), a0 + bestDir * 22.5, bestD);
		shooter.bPickup = true;
		for (int i = 0; i < row.Size(); i++)
		{
			row[i].SetState(row[i].SpawnState);
			row[i].tics = -1;	// hold still for the picture
		}
		return true;
	}

	// psDooM's floating "pid / name" label, over monsters in plain sight
	override void RenderOverlay(RenderEvent e)
	{
		if (!active || automapactive)
			return;
		double w = Screen.GetWidth(), h = Screen.GetHeight();
		double fov = players[consoleplayer].fov;
		double focal = (h * 4. / 3.) / 2. / tan(fov / 2.);
		double c = cos(e.ViewAngle), s = sin(e.ViewAngle);
		for (int i = 0; i < mons.Size(); i++)
		{
			if (!seen[i] || !mons[i] || mons[i].bInvisible)
				continue;
			Actor mo = mons[i];
			Vector3 rel = mo.Prev + (mo.pos - mo.Prev) * e.FracTic + (0, 0, mo.height) - e.ViewPos;
			double fwd = rel.x * c + rel.y * s;
			if (fwd < 32)
				continue;
			double side = rel.x * s - rel.y * c;
			double sx = w / 2 + side * focal / fwd;
			double sy = h / 2 - rel.z * focal * 1.2 / fwd - 14 * CleanYfac;
			if (sx < w / 8 || sx > w * 7 / 8)
				continue;
			Screen.DrawText(SmallFont, Font.CR_RED, sx, sy, String.Format("%d", PidOf(mo)), DTA_CleanNoMove, true);
			Screen.DrawText(SmallFont, Font.CR_RED, sx, sy + 7 * CleanYfac, LabelOf(mo), DTA_CleanNoMove, true);
		}
	}
}
