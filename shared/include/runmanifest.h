/* SPDX-License-Identifier: MIT */
/*
 * runmanifest.h -- shared schema + emit + environment-detect helpers for
 * self-describing, agent-reviewable real-hardware/emulator telemetry.
 *
 * Adapted from doskutsu's own runmanifest.h (there: GPL-3.0-or-later,
 * alongside the rest of that repo) -- relicensed MIT for shared/ with the
 * operator's explicit approval, since this file is original instrumentation
 * code with no upstream/GPL dependency, not a derivative of NXEngine-evo or
 * any other GPL source. doskutsu's own copy is untouched.
 *
 * Header-only static-inline: no separate .c/.cpp file, no build-system
 * wiring beyond an include path. Any port's engine can call
 * runmanifest_emit() once at clean shutdown and get a schema-versioned,
 * self-identifying telemetry block that an agent (or script) can review
 * from a captured log alone -- which binary, which config, which
 * environment, did it finish cleanly -- without re-deriving any of that
 * from prose or operator memory. See
 * shared/skills/dos-hardware-validation's RUNMANIFEST material for the
 * methodology this supports (two-witness verification, noise-floor
 * discipline, single-lever attribution, and why a proxy is not the thing
 * itself).
 *
 * What this header does NOT include, by design -- these are per-port
 * decisions, not shared/'s to make:
 *   - The env-var allowlist runmanifest_compute_env_block_sha12() hashes.
 *     Every port has its own set of measurement-affecting hints/env vars;
 *     hardcoding one port's list here would silently mean nothing for any
 *     other port. Build your own NULL-terminated, alphabetically-sorted
 *     allowlist and pass it in.
 *   - The actual call site (where in your engine's shutdown path to build
 *     and emit the struct) and which fields you have real data for versus
 *     leave at their NA/zero defaults.
 *
 * Environment detection rationale (carried forward from doskutsu's own
 * cross-incident history, worth keeping even though the specific incident
 * was doskutsu's): an early version used an OR-gate (DOSBox-X detected if
 * EITHER a CPUID-hypervisor-leaf vendor-string match OR an INT21h AX=4452h
 * probe responded positive) and false-positived under 86Box, because
 * INT21h AH=44h is the legitimate MS-DOS Generic IOCTL handler and 86Box's
 * MS-DOS 6.22 answers it too. Fixed by requiring BOTH signals (an AND-gate)
 * before classifying DOSBox-X. General lesson this encodes: two ways to
 * ask the same question is not robustness; two signals with genuinely
 * different blind spots is. 86Box gets its own dedicated signal (a BIOS
 * sign-on string scan) rather than trying to reuse either DOSBox-X signal.
 * realhw is the default when neither positively identifies -- the correct
 * default under "we have no positive signal for anything else" discipline.
 *
 * Known limitation, not yet root-caused: the two DOSBox-X signals behind
 * the AND-gate do not reliably fire under every DOSBox-X config/version --
 * this is why shared/tools/dosbox-x.conf's [autoexec] sets the operator
 * override (DOS_PORT_ENVIRONMENT=dosbox-x) unconditionally rather than
 * relying on auto-detect, and why runmanifest_detect_environment() checks
 * the override first. A DOSBox-X run with that override unset can
 * legitimately fall through to REALHW; that is this documented gap, not a
 * new bug, and reproducing it doesn't mean the AND-gate itself regressed.
 * Nobody has yet isolated which of the two signals (CPUID hypervisor leaf
 * vs. INT21h AX=4452h) is the one that misses, or whether it's version-
 * specific to a given DOSBox-X release -- that needs a DJGPP+DOSBox-X
 * repro with the per-signal breakdown (see tests/probes/hwinv.c's
 * HWINV-ENV log lines). Record any such finding in
 * dos-hardware-validation/references/runmanifest-log.md, not just here.
 */

#ifndef DOS_PORT_RUNMANIFEST_H
#define DOS_PORT_RUNMANIFEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>   /* getenv() */
#include <string.h>

/* C++ flow needs <ctime> for time() etc; C uses <time.h>. */
#include <time.h>

#ifdef __DJGPP__
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <sys/farptr.h>
#endif

/* ===== Schema version ===== */
#define RUNMANIFEST_SCHEMA_VERSION 1

/* ===== Sentinel literals (essential grep targets) ===== */
#define RUNMANIFEST_BEGIN_MARKER "[RUNMANIFEST-BEGIN]"
#define RUNMANIFEST_END_MARKER   "[RUNMANIFEST-END]"

/* ===== 3-state environment classifier ===== */
typedef enum {
    RUNMANIFEST_ENV_UNKNOWN  = 0,
    RUNMANIFEST_ENV_DOSBOX_X = 1,
    RUNMANIFEST_ENV_86BOX    = 2,
    RUNMANIFEST_ENV_REALHW   = 3
} runmanifest_env_t;

static inline const char *runmanifest_env_str(runmanifest_env_t e)
{
    switch (e) {
    case RUNMANIFEST_ENV_DOSBOX_X: return "dosbox-x";
    case RUNMANIFEST_ENV_86BOX:    return "86box";
    case RUNMANIFEST_ENV_REALHW:   return "realhw";
    case RUNMANIFEST_ENV_UNKNOWN:
    default:                       return "unknown";
    }
}

/* ===== Manifest record (all 18 fields per schema v1) ===== */
typedef struct {
    int                schema_version;
    runmanifest_env_t  environment;
    /* Strings: null-terminated; emit "" if unset (the schema treats "" as
     * "field present, empty" -- spec parsers accept; analysts grep on
     * presence not content). */
    char        binary_sha12[16];        /* hex12 */
    char        wave_tag[32];
    char        scene[40];
    char        env_block_sha[16];       /* hex12 */
    char        started_utc[32];         /* ISO8601 e.g. 2026-05-14T00:42:05Z */
    int         duration_s;
    int         exit_code;
    /* fps_* : negative sentinel (-1.0) -> emit "NA"; else emit float. */
    double      fps_p50;
    double      fps_p95;
    /* String pointers: NULL -> emit "NA"; else emit literal. */
    const char *audio_vital_status;
    const char *regime;
    int         banner_required_hit;
    int         banner_required_total;
    int         banner_forbidden_hit;
    int         banner_optional_hit;
    int         critical_count;
    int         warn_count;
} runmanifest_t;

/* Initialize a manifest to schema-v1 defaults: NA for all perf fields,
 * 0 for counters, empty strings, environment=UNKNOWN. Call this then
 * fill the fields you have. */
static inline void runmanifest_init(runmanifest_t *m)
{
    if (!m) return;
    memset(m, 0, sizeof *m);
    m->schema_version = RUNMANIFEST_SCHEMA_VERSION;
    m->environment    = RUNMANIFEST_ENV_UNKNOWN;
    m->fps_p50        = -1.0;       /* NA sentinel */
    m->fps_p95        = -1.0;
    m->audio_vital_status = NULL;   /* NA */
    m->regime             = NULL;   /* NA */
    m->exit_code      = 0;
}

#ifdef __DJGPP__

/* ===== Environment detection (DJGPP-only; consumers compile this on DOS) =====
 *
 * Three steps in priority order:
 *   1. Operator-set env-var override DOS_PORT_ENVIRONMENT=<dosbox-x|86box|realhw>
 *      (belt-and-suspenders; matches shared/tools/dosbox-launch.sh's
 *      LAUNCH_EXTRA_SET convention -- see
 *      dos-hardware-validation/references/runmanifest-log.md).
 *   2. DOSBox-X detect AND-gate: CPUID-hypervisor vendor contains "DOSBOX"
 *      AND INT 21h AX=4452h returns AX != 0xFFFF and != 0x4452.
 *   3. 86Box detect: scan F000h:E000h..F000h:FFFFh for literal "86Box".
 *   4. Default: REALHW.
 */

static inline int runmanifest_cpuid_available(void)
{
    uint32_t before = 0, after = 0;
    __asm__ volatile (
        "pushfl\n"
        "popl %0\n"
        "movl %0, %%eax\n"
        "xorl $0x200000, %%eax\n"
        "pushl %%eax\n"
        "popfl\n"
        "pushfl\n"
        "popl %1\n"
        : "=r"(before), "=r"(after)
        : : "eax"
    );
    return ((before ^ after) & 0x200000) != 0;
}

static inline void runmanifest_cpuid(uint32_t leaf,
                                      uint32_t *a, uint32_t *b,
                                      uint32_t *c, uint32_t *d)
{
    __asm__ volatile (
        "cpuid"
        : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "a"(leaf)
    );
}

/* Signal 1: CPUID hypervisor leaf 0x40000000 vendor string contains "DOSBOX".
 * Returns 1 if positive, 0 if not (including not-available). */
static inline int runmanifest_signal_cpuid_dosbox(void)
{
    if (!runmanifest_cpuid_available()) return 0;
    uint32_t a, b, c, d;
    runmanifest_cpuid(0x40000000ul, &a, &b, &c, &d);
    char hv[13];
    *(uint32_t *)(hv + 0) = b;
    *(uint32_t *)(hv + 4) = c;
    *(uint32_t *)(hv + 8) = d;
    hv[12] = 0;
    /* Sanitize: only ASCII. Reject garbage. */
    for (int i = 0; i < 12; i++) {
        if (hv[i] != 0 && (hv[i] < 0x20 || hv[i] > 0x7E)) return 0;
    }
    if (!hv[0]) return 0;
    /* Case-insensitive substring search for "DOSBOX". */
    for (int i = 0; i + 6 <= 12; i++) {
        const char *p = hv + i;
        if ((p[0] == 'D' || p[0] == 'd') &&
            (p[1] == 'O' || p[1] == 'o') &&
            (p[2] == 'S' || p[2] == 's') &&
            (p[3] == 'B' || p[3] == 'b') &&
            (p[4] == 'O' || p[4] == 'o') &&
            (p[5] == 'X' || p[5] == 'x')) {
            return 1;
        }
    }
    return 0;
}

/* Signal 2: INT 21h AX=4452h ("DOSBox-X version" multiplex). On DOSBox-X
 * with the multiplex enabled, returns AX != 0xFFFF. Returns 1 if positive.
 *
 * NOTE: this signal ALSO positives under 86Box / real MS-DOS because the
 * AH=44h func family is the legit Generic IOCTL handler. Only useful in
 * combination with signal 1 (AND-gate) -- see the file header comment. */
static inline int runmanifest_signal_int21_4452(void)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4452;
    if (__dpmi_int(0x21, &r) < 0) return 0;
    if (r.x.ax == 0xFFFF || r.x.ax == 0x4452) return 0;
    return 1;
}

/* Signal 3: F000h:E000h..F000h:FFFFh BIOS sign-on string scan for "86Box"
 * literal. 86Box's emulated BIOS ROMs include the upstream identification
 * string. Returns 1 if positive, 0 if not. */
static inline int runmanifest_signal_86box_bios_string(void)
{
    /* Scan 8 KB of BIOS ROM area for "86Box" (case-sensitive; the upstream
     * canonical capitalization). */
    static const char needle[] = "86Box";
    const size_t nlen = sizeof needle - 1;
    /* Use _farpeekb so we walk through DPMI's BIOS ROM mapping. */
    unsigned long base = 0xFE000ul;  /* F000:E000 in real-mode segment math */
    for (unsigned long off = 0; off < 0x2000 - nlen; off++) {
        int match = 1;
        for (size_t k = 0; k < nlen; k++) {
            if (_farpeekb(_dos_ds, base + off + k) != (unsigned char)needle[k]) {
                match = 0;
                break;
            }
        }
        if (match) return 1;
    }
    return 0;
}

/* Operator-override env var. Returns the corresponding enum or UNKNOWN
 * if absent/invalid. */
static inline runmanifest_env_t runmanifest_env_override(void)
{
    const char *s = getenv("DOS_PORT_ENVIRONMENT");
    if (!s) return RUNMANIFEST_ENV_UNKNOWN;
    if (strcmp(s, "dosbox-x") == 0) return RUNMANIFEST_ENV_DOSBOX_X;
    if (strcmp(s, "86box") == 0)    return RUNMANIFEST_ENV_86BOX;
    if (strcmp(s, "realhw") == 0)   return RUNMANIFEST_ENV_REALHW;
    return RUNMANIFEST_ENV_UNKNOWN;
}

/* Composite detection. Returns the inferred environment + (optionally)
 * fills `*out_signals` with a 4-bit bitmap: bit 0 = override matched,
 * bit 1 = cpuid-dosbox signal, bit 2 = int21-4452 signal, bit 3 = 86Box
 * BIOS string. Caller can log the signal map for forensic transparency. */
static inline runmanifest_env_t runmanifest_detect_environment(unsigned *out_signals)
{
    unsigned signals = 0;
    runmanifest_env_t override = runmanifest_env_override();
    if (override != RUNMANIFEST_ENV_UNKNOWN) {
        signals |= 0x1;
        if (out_signals) *out_signals = signals;
        return override;
    }
    int sig_cpuid = runmanifest_signal_cpuid_dosbox();
    int sig_int21 = runmanifest_signal_int21_4452();
    int sig_86box = runmanifest_signal_86box_bios_string();
    if (sig_cpuid) signals |= 0x2;
    if (sig_int21) signals |= 0x4;
    if (sig_86box) signals |= 0x8;
    if (out_signals) *out_signals = signals;

    /* AND-gate for DOSBox-X: BOTH cpuid + int21 must positive. */
    if (sig_cpuid && sig_int21) return RUNMANIFEST_ENV_DOSBOX_X;
    /* 86Box has its own dedicated string signal. */
    if (sig_86box) return RUNMANIFEST_ENV_86BOX;
    /* Default: real hardware (or any DOS environment we don't have a
     * positive signal for, which under realhw discipline is the correct
     * default). */
    return RUNMANIFEST_ENV_REALHW;
}

#endif  /* __DJGPP__ */

/* ===== env_block_sha12 helper =====
 *
 * Computes a 12-hex-char fingerprint over a canonical-form rendering of the
 * essential env-var k=v pairs at runtime. Used to populate the schema-v1
 * `env_block_sha` field so cross-iter analysis can detect "two runs claiming
 * the same config emit different shas" without grep'ing each env var by hand.
 *
 * Algorithm:
 *   1. Iterate a caller-provided allowlist of env-var names (NULL-terminated
 *      array of const char *). Allowlist should be pre-sorted lexically;
 *      we do not sort at runtime (avoids alloc + qsort dependency).
 *   2. For each name: getenv(); if NULL or empty value, SKIP (functionally
 *      identical to unset; canonical form normalizes them out).
 *   3. Append "name=value\n" to a stack buffer (~4 KB; cap at buffer size).
 *   4. Hash the buffer with FNV-1a 64-bit (compact, header-friendly, no
 *      crypto deps; collision probability for ~280T distinct env blocks
 *      = ~2^48 truncation -- adequate for fingerprinting, not security).
 *   5. Format low 48 bits as 12 hex chars; write null-terminated to out.
 *
 * out_buf must be at least 13 bytes. Returns the number of allowlist entries
 * that contributed to the hash (entries with empty/unset values skipped).
 *
 * On empty env block (nothing in allowlist set), emits "000000000000" so the
 * sentinel is distinguishable from a real hash that happens to land on zero
 * (probability 2^-48).
 *
 * The allowlist itself is deliberately not shipped here -- see the file
 * header comment. Build your own per-port, covering whatever hints/env
 * vars actually affect measurement for your engine. */
static inline uint64_t runmanifest_fnv1a_64(const char *data, size_t len)
{
    uint64_t h = 0xCBF29CE484222325ULL;  /* FNV-1a 64-bit offset basis */
    for (size_t i = 0; i < len; i++) {
        h ^= (unsigned char)data[i];
        h *= 0x100000001B3ULL;            /* FNV-1a 64-bit prime */
    }
    return h;
}

static inline int runmanifest_compute_env_block_sha12(
    const char * const *allowlist,
    char *out_buf, size_t out_buf_size)
{
    if (!allowlist || !out_buf || out_buf_size < 13) return 0;
    /* Canonical-form buffer. Bounded; oversize is fine (FNV hashes what
     * fits + we report truncation via the return path). */
    char canon[4096];
    size_t pos = 0;
    int contributed = 0;
    for (const char * const *p = allowlist; *p; ++p) {
        const char *val = getenv(*p);
        if (!val || !val[0]) continue;
        size_t kl = strlen(*p);
        size_t vl = strlen(val);
        /* "name=value\n" -- bounds-check before write. */
        if (pos + kl + 1 + vl + 1 + 1 >= sizeof canon) break;
        memcpy(canon + pos, *p, kl);     pos += kl;
        canon[pos++] = '=';
        memcpy(canon + pos, val, vl);    pos += vl;
        canon[pos++] = '\n';
        contributed++;
    }
    if (contributed == 0) {
        /* Empty env block -- emit the all-zero sentinel rather than the
         * FNV-1a-of-empty value (which is the basis 0xCBF29CE484222325). */
        snprintf(out_buf, out_buf_size, "000000000000");
        return 0;
    }
    uint64_t h = runmanifest_fnv1a_64(canon, pos);
    /* Take low 48 bits -> 12 hex chars. */
    unsigned long long h48 = (unsigned long long)(h & 0xFFFFFFFFFFFFULL);
    snprintf(out_buf, out_buf_size, "%012llx", h48);
    return contributed;
}

/* ===== Emit format ===== */

/* Helper: emit one key=value line. */
static inline void runmanifest_emit_kv_str(FILE *fp, const char *key, const char *val)
{
    fprintf(fp, "%s=%s\n", key, val ? val : "");
}

static inline void runmanifest_emit_kv_int(FILE *fp, const char *key, int val)
{
    fprintf(fp, "%s=%d\n", key, val);
}

static inline void runmanifest_emit_kv_float_or_na(FILE *fp, const char *key, double val)
{
    if (val < 0.0) fprintf(fp, "%s=NA\n", key);
    else           fprintf(fp, "%s=%.2f\n", key, val);
}

static inline void runmanifest_emit_kv_str_or_na(FILE *fp, const char *key, const char *val)
{
    fprintf(fp, "%s=%s\n", key, val ? val : "NA");
}

/* Emit the full RUNMANIFEST block bracketed by sentinels. Caller is
 * responsible for fp positioning + fsync/fflush after (a crash between
 * emit and fsync loses the block on a hard reset -- see
 * dos-hardware-validation's log-flush-on-exit-only hazard material).
 * Also emit a runtime "engagement banner" line elsewhere in your log
 * (distinct from this block) so a smoke gate has a second, independent
 * witness that this code path actually ran -- `strings`-grep on the
 * binary only proves RUNMANIFEST_BEGIN_MARKER is embedded, not that
 * runmanifest_emit() executed. */
static inline void runmanifest_emit(FILE *fp, const runmanifest_t *m)
{
    if (!fp || !m) return;
    fprintf(fp, "%s\n", RUNMANIFEST_BEGIN_MARKER);
    runmanifest_emit_kv_int(fp, "schema_version", m->schema_version);
    runmanifest_emit_kv_str(fp, "environment", runmanifest_env_str(m->environment));
    runmanifest_emit_kv_str(fp, "binary_sha12", m->binary_sha12);
    runmanifest_emit_kv_str(fp, "wave_tag",     m->wave_tag);
    runmanifest_emit_kv_str(fp, "scene",        m->scene);
    runmanifest_emit_kv_str(fp, "env_block_sha", m->env_block_sha);
    runmanifest_emit_kv_str(fp, "started_utc",  m->started_utc);
    runmanifest_emit_kv_int(fp, "duration_s",   m->duration_s);
    runmanifest_emit_kv_int(fp, "exit_code",    m->exit_code);
    runmanifest_emit_kv_float_or_na(fp, "fps_p50", m->fps_p50);
    runmanifest_emit_kv_float_or_na(fp, "fps_p95", m->fps_p95);
    runmanifest_emit_kv_str_or_na(fp, "audio_vital_status", m->audio_vital_status);
    runmanifest_emit_kv_str_or_na(fp, "regime",             m->regime);
    runmanifest_emit_kv_int(fp, "banner_required_hit",   m->banner_required_hit);
    runmanifest_emit_kv_int(fp, "banner_required_total", m->banner_required_total);
    runmanifest_emit_kv_int(fp, "banner_forbidden_hit",  m->banner_forbidden_hit);
    runmanifest_emit_kv_int(fp, "banner_optional_hit",   m->banner_optional_hit);
    runmanifest_emit_kv_int(fp, "critical_count",        m->critical_count);
    runmanifest_emit_kv_int(fp, "warn_count",            m->warn_count);
    fprintf(fp, "%s\n", RUNMANIFEST_END_MARKER);
    fflush(fp);
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* DOS_PORT_RUNMANIFEST_H */
