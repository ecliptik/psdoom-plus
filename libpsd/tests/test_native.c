/*
 * test_native.c - the native backend against real processes
 *
 * Only ever signals children this test forked itself, so it is safe to run
 * anywhere. Skips (exit 0) where there is no native backend yet.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "psd_internal.h"
#include "psd_test.h"

#ifdef __HAIKU__
#include <OS.h>
/* Haiku renices by lowering thread priorities: report the lowest one as a nice */
static int nice_of(pid_t pid)
{
	thread_info th;
	int32 cookie = 0, lowest = B_NORMAL_PRIORITY;

	while (get_next_thread_info(pid, &cookie, &th) == B_OK)
		if (th.priority < lowest)
			lowest = th.priority;
	return lowest < B_NORMAL_PRIORITY ? 5 : 0;
}
#else
static int nice_of(pid_t pid)
{
	errno = 0;
	return getpriority(PRIO_PROCESS, (id_t)pid);
}
#endif

typedef struct rec {
	long pids[1024];
	int n;
} rec;

static int on_event(void *user, psd_event ev, const psd_proc *p)
{
	rec *r = user;

	if (ev == PSD_EV_SPAWN && r->n < 1024)
		r->pids[r->n++] = p->pid;
	return 1;
}

static int seen(rec *r, long pid)
{
	int i;

	for (i = 0; i < r->n; i++)
		if (r->pids[i] == pid)
			return 1;
	return 0;
}

static void msleep(long ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
}

/* fork a `sleep 60`; optionally ignoring SIGTERM (SIG_IGN survives exec) */
static pid_t child(int ignore_term)
{
	pid_t pid = fork();

	if (pid == 0) {
		if (ignore_term)
			signal(SIGTERM, SIG_IGN);
		execlp("sleep", "sleep", "60", (char *)NULL);
		_exit(127);
	}
	return pid;
}

static int died_by(pid_t pid, int sig)
{
	int status, i;

	for (i = 0; i < 200; i++) {
		pid_t r = waitpid(pid, &status, WNOHANG);
		if (r == pid)
			return WIFSIGNALED(status) && WTERMSIG(status) == sig;
		msleep(10);
	}
	return 0;
}

static psd_ctx *open_native(psd_mode mode, int grace, int show)
{
	psd_config *cfg = psd_config_new();
	psd_ctx *ctx;
	int err;

	psd_config_set_mode(cfg, mode);
	psd_config_set_log(cfg, "test_native_actions.log");
	psd_config_set_max_procs(cfg, 0);
	psd_config_set_grace_ms(cfg, grace);
	psd_config_set_show_protected(cfg, show);
	ctx = psd_open(cfg, &err);
	psd_config_free(cfg);
	if (ctx == NULL && err != PSD_ERR_BACKEND)
		fprintf(stderr, "psd_open: %s\n", psd_strerror(err));
	return ctx;
}

static pid_t a, b;

static void lists_my_children(void)
{
	psd_ctx *ctx = open_native(PSD_MODE_CLASSIC, 0, 0);
	rec r = { 0 };
	const psd_proc *p;

	psd_poll(ctx, on_event, &r);
	CHECK(seen(&r, a));
	CHECK(seen(&r, b));
	CHECK(seen(&r, getpid()));                   /* classic lists itself */
	p = psd_lookup(ctx, a);
	CHECK(p != NULL);
	if (p) {
		CHECK_EQ(p->ppid, getpid());
		CHECK_EQ(p->uid, (long)geteuid());
		CHECK(strstr(p->name, "sleep") != NULL);
		CHECK(strstr(p->cmdline, "sleep 60") != NULL);
		CHECK(p->start_id != 0);
	}
	psd_close(ctx);
}

static void classic_renice_and_kill(void)
{
	psd_ctx *ctx = open_native(PSD_MODE_CLASSIC, 0, 0);
	rec r = { 0 };

	psd_poll(ctx, on_event, &r);
	CHECK_EQ(psd_wound(ctx, a, 70, 100), PSD_OK);
	CHECK_EQ(nice_of(a), 5);
	CHECK_EQ(psd_kill(ctx, a), PSD_OK);
	CHECK(died_by(a, SIGKILL));
	psd_close(ctx);
}

static void modern_protects_me_and_escalates(void)
{
	psd_ctx *ctx = open_native(PSD_MODE_MODERN, 300, 0);
	rec r = { 0 };

	psd_poll(ctx, on_event, &r);
	CHECK(!seen(&r, getpid()));                  /* ourselves */
	CHECK(!seen(&r, getppid()));                 /* our parent (shell/ctest) */
	CHECK(seen(&r, b));
	CHECK_EQ(psd_kill(ctx, getpid()), PSD_ERR_UNKNOWN);

	CHECK_EQ(psd_kill(ctx, b), PSD_OK);          /* TERM: ignored by b */
	msleep(100);
	psd_tick(ctx);
	CHECK_EQ(waitpid(b, NULL, WNOHANG), 0);      /* still alive inside grace */
	msleep(300);
	psd_tick(ctx);                               /* -> KILL */
	CHECK(died_by(b, SIGKILL));
	psd_close(ctx);

	ctx = open_native(PSD_MODE_MODERN, 300, 1);
	psd_poll(ctx, on_event, &r);
	CHECK_EQ(psd_kill(ctx, getpid()), PSD_ERR_PROTECTED);
	psd_close(ctx);
}

static void stale_identity_is_refused(void)
{
	psd_config *cfg = psd_config_new();
	psd_backend be;
	psd_proc *snap, fake;
	size_t n, i;
	pid_t c = child(0);
	int found = 0;

	msleep(50);
	CHECK_EQ(psd_backend_native_open(&be, cfg), PSD_OK);
	be.snapshot(&be, &snap, &n);
	for (i = 0; i < n; i++)
		if (snap[i].pid == c) {
			fake = snap[i];
			found = 1;
		}
	CHECK(found);
	if (found) {
		fake.start_id += 12345;              /* pretend the pid was recycled */
		CHECK_EQ(be.signal(&be, &fake, PSD_SIG_KILL, 1), PSD_ERR_GONE);
		CHECK_EQ(waitpid(c, NULL, WNOHANG), 0);  /* untouched */
	}
	kill(c, SIGKILL);
	waitpid(c, NULL, 0);
	be.close(&be);
	psd_config_free(cfg);
}

int main(void)
{
	struct passwd *pw = getpwuid(geteuid());
	psd_ctx *probe;

	setenv("PSDOOMUSER", pw ? pw->pw_name : "nobody", 1);
	probe = open_native(PSD_MODE_CLASSIC, 0, 0);
	if (probe == NULL) {
		printf("no native backend on this platform: skipped\n");
		return 0;
	}
	psd_close(probe);

	a = child(0);
	b = child(1);
	msleep(100);                                 /* let them exec */

	RUN(lists_my_children);
	RUN(classic_renice_and_kill);
	RUN(modern_protects_me_and_escalates);
	RUN(stale_identity_is_refused);

	kill(a, SIGKILL);
	kill(b, SIGKILL);
	while (waitpid(-1, NULL, WNOHANG) > 0)
		;
	remove("test_native_actions.log");
	TEST_DONE();
}
