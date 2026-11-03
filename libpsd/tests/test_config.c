/*
 * test_config.c - psdoom.conf and command-line parsing
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psd_internal.h"
#include "psd_test.h"

static const char *conf_path = "test_config_psdoom.conf";

static void write_conf(const char *text)
{
	FILE *f = fopen(conf_path, "w");

	fputs(text, f);
	fclose(f);
}

static void file_settings(void)
{
	psd_config *cfg = psd_config_new();

	write_conf("# psdoom-plus\n"
	           "mode = classic\n"
	           "users = alice, bob carol\n"
	           "notusers = mallory\n"
	           "protect = foo*  bar\n"
	           "max_procs = 12   # trailing comment\n"
	           "grace_ms = 500\n"
	           "show_protected = yes\n"
	           "log = off\n");
	CHECK_EQ(psd_config_load_file(cfg, conf_path), PSD_OK);
	CHECK_EQ(cfg->mode, PSD_MODE_CLASSIC);
	CHECK_EQ(cfg->nusers, 3);
	CHECK_EQ(strcmp(cfg->users[2], "carol"), 0);
	CHECK_EQ(cfg->nnotusers, 1);
	CHECK_EQ(cfg->nprotect, 2);
	CHECK_EQ(strcmp(cfg->protect[0], "foo*"), 0);
	CHECK_EQ(cfg->max_procs, 12);
	CHECK_EQ(cfg->grace_ms, 500);
	CHECK(cfg->show_protected);
	CHECK(cfg->log_set && cfg->log_path[0] == '\0');
	psd_config_free(cfg);
	remove(conf_path);
}

static void bad_lines_are_reported_not_fatal(void)
{
	psd_config *cfg = psd_config_new();

	write_conf("mode = sideways\nmax_procs = 7\nnonsense\n");
	CHECK_EQ(psd_config_load_file(cfg, conf_path), PSD_ERR);
	CHECK_EQ(cfg->mode, PSD_MODE_MODERN);        /* unchanged */
	CHECK_EQ(cfg->max_procs, 7);                 /* good lines still apply */
	psd_config_free(cfg);
	remove(conf_path);
}

static void missing_file_is_fine(void)
{
	psd_config *cfg = psd_config_new();

	CHECK_EQ(psd_config_load_file(cfg, "/nonexistent/psdoom.conf"), PSD_OK);
	psd_config_free(cfg);
}

static void args_override_file(void)
{
	char *argv[] = { "doom", "-psconf", (char *)conf_path, "-psmode", "modern",
	                 "-psmax", "3", "-psgrace", "0", "-warp", "1" };
	psd_config *cfg = psd_config_new();

	write_conf("mode = classic\nmax_procs = 50\n");
	CHECK_EQ(psd_config_parse_args(cfg, 11, argv), PSD_OK);
	CHECK_EQ(cfg->mode, PSD_MODE_MODERN);
	CHECK_EQ(cfg->max_procs, 3);
	CHECK_EQ(cfg->grace_ms, 0);
	psd_config_free(cfg);
	remove(conf_path);
}

static void psdoom_2000_flags(void)
{
	char *argv[] = { "ps-xdoom", "-nopsact", "-nopssafety", "-nopslev",
	                 "-psuser", "root", "daemon", "-psnotuser", "-psclassic" };
	psd_config *cfg = psd_config_new();

	psd_config_parse_args(cfg, 9, argv);
	CHECK(cfg->no_act);
	CHECK(psd_config_no_safety(cfg));
	CHECK(psd_config_no_pslev(cfg));
	CHECK_EQ(cfg->nusers, 2);
	CHECK_EQ(strcmp(cfg->users[1], "daemon"), 0);
	CHECK(cfg->notuser_add_self);                /* -psnotuser with no names */
	CHECK(!cfg->user_add_self);
	CHECK(cfg->userlist_given);
	CHECK_EQ(psd_config_mode(cfg), PSD_MODE_CLASSIC);
	psd_config_free(cfg);
}

static void bad_mode_is_an_error(void)
{
	char *argv[] = { "doom", "-psmode", "chaotic" };
	psd_config *cfg = psd_config_new();

	CHECK_EQ(psd_config_parse_args(cfg, 3, argv), PSD_ERR);
	CHECK_EQ(cfg->mode, PSD_MODE_MODERN);
	psd_config_free(cfg);
}

static void xdg_paths(void)
{
	char buf[PSD_PATH_MAX];

	setenv("XDG_CONFIG_HOME", "/tmp/xdgc", 1);
	unsetenv("PSDOOM_CONF");
	CHECK_EQ(psd_platform_default_conf(buf, sizeof buf), PSD_OK);
	CHECK_EQ(strcmp(buf, "/tmp/xdgc/psdoom/psdoom.conf"), 0);
	setenv("PSDOOM_CONF", "/etc/psdoom.conf", 1);
	psd_platform_default_conf(buf, sizeof buf);
	CHECK_EQ(strcmp(buf, "/etc/psdoom.conf"), 0);
	unsetenv("XDG_STATE_HOME");
	setenv("HOME", "/home/alice", 1);
	CHECK_EQ(psd_platform_default_log(buf, sizeof buf), PSD_OK);
	CHECK_EQ(strcmp(buf, "/home/alice/.local/state/psdoom/actions.log"), 0);
}

static void whoami_order(void)
{
	setenv("PSDOOMUSER", "a", 1);
	setenv("LOGNAME", "b", 1);
	CHECK_EQ(strcmp(psd_platform_whoami(), "a"), 0);
	unsetenv("PSDOOMUSER");
	CHECK_EQ(strcmp(psd_platform_whoami(), "b"), 0);
	unsetenv("LOGNAME");
	setenv("USER", "c", 1);
	CHECK_EQ(strcmp(psd_platform_whoami(), "c"), 0);
}

int main(void)
{
	RUN(file_settings);
	RUN(bad_lines_are_reported_not_fatal);
	RUN(missing_file_is_fine);
	RUN(args_override_file);
	RUN(psdoom_2000_flags);
	RUN(bad_mode_is_an_error);
	RUN(xdg_paths);
	RUN(whoami_order);
	TEST_DONE();
}
