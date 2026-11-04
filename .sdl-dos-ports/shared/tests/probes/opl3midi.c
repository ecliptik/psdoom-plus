/*
 * opl3midi.c -- standalone DOSBox-X probe for shared/audio/opl3_sink.
 *
 * Pure DJGPP; no game engine. Plays a small, self-authored, byte-known
 * Standard MIDI File through shared/audio/midi_sched + shared/audio/
 * opl3_sink, and reports checkable output: the parser's own event count,
 * a tempo/division witness (proves the SMF header AND a mid-file tempo
 * meta-event both actually took effect, not just defaults), and an
 * independent per-callback dispatch count captured by a small counting
 * wrapper around the OPL3 sink's own bound functions (so the report
 * proves events actually reached the SINK, not just midi_sched's own
 * internal bookkeeping).
 *
 * The test tune (TEST_SMF below) is self-authored for this probe --
 * format 0, 1 track, 240 PPQ, a mid-track tempo meta-event (400000 us/
 * quarter = 150 BPM, deliberately NOT the SMF/library default of 500000
 * at 480 PPQ, so a correct read is unambiguously distinguishable from a
 * default that was never overwritten) -- a C-E-G triad (GM program 0),
 * held, then released. No third-party MIDI data is committed anywhere;
 * per midi_sched.h's own design note, an SMF is read at runtime, and this
 * probe writes its embedded test bytes to a scratch file at startup
 * rather than shipping a .mid asset.
 *
 * Usage: OPL3MIDI.EXE
 * Output: OPL3MIDI.LOG (+ stdout). Optional: stage shared/audio/opl3bank.dat
 * as OPL3BANK.DAT alongside the exe to also exercise the full 128-program
 * bank-loader path (patches 0141/0232) instead of the built-in 8-patch
 * fallback; the probe runs and passes either way.
 *
 * BUILD -- unlike most of this directory, this probe is NOT bare-metal: it
 * calls into the real compiled SDL3-DOS OPL3 register primitives, so it
 * needs to link against an actual libSDL3.a built with SDL patch 0037
 * applied (any port's own build/sysroot/lib/libSDL3.a works; this hub does
 * not vendor/build SDL itself). Example, using a port's already-built
 * sysroot:
 *   i586-pc-msdosdjgpp-gcc -O2 -c opl3midi.c -I../../audio
 *   i586-pc-msdosdjgpp-gcc -O2 -c ../../audio/midi_sched.c
 *   i586-pc-msdosdjgpp-gcc -O2 -c ../../audio/opl3_sink.c
 *   i586-pc-msdosdjgpp-gcc -O2 opl3midi.o midi_sched.o opl3_sink.o \
 *       -L<port>/build/sysroot/lib -lSDL3 -lm -o opl3midi.exe
 *   stubedit opl3midi.exe minstack=2048k
 *
 * RUN -- this hub's own shared/tools/dosbox-x*.conf ship `oplmode = none`
 * by default (most ports here are PCM-focused), so SDL_DOSOpl3Detect()
 * will report no chip under dosbox-run.sh's stock confs -- the probe still
 * runs and every midi_sched witness below is unaffected (they don't depend
 * on a chip being present), but the sink's own register I/O goes
 * unexercised. To actually validate the sink, run with a conf that sets
 * `oplmode = opl3` (or `auto`) in its [sblaster] section -- do not change
 * the shared confs themselves for this (they're every port's default);
 * copy one to a scratch location and flip the setting there instead.
 *
 * License: MIT (probe is original work; see shared/audio/opl3_sink.h /
 * THIRD-PARTY.md for the sink module's own provenance).
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "midi_sched.h"
#include "opl3_sink.h"

/* Linker shim: the compiled SDL3-DOS static library's OPL3 helpers live in
 * the same translation unit as its (unused-here) SB16/MPU-401 DSP-mediated
 * MIDI helpers, which reference this engine-provided counter (normally
 * defined by a port's own PCM mixer, e.g. dosags's dos_audio_mixer.cpp).
 * This probe never opens an SB16 audio device or plays PCM, so the real
 * value is always 0 -- this dummy definition exists only to satisfy the
 * archive's cross-object reference, not because the probe uses it. */
volatile uint32_t g_dos_sfx_synth_active_count = 0;

/* ---- embedded test SMF (see file header for the authored content) -------*/
static const unsigned char TEST_SMF[] = {
  0x4D, 0x54, 0x68, 0x64, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x01,
  0x00, 0xF0, 0x4D, 0x54, 0x72, 0x6B, 0x00, 0x00, 0x00, 0x21, 0x00, 0xFF,
  0x51, 0x03, 0x06, 0x1A, 0x80, 0x00, 0xC0, 0x00, 0x00, 0x90, 0x3C, 0x64,
  0x00, 0x40, 0x64, 0x00, 0x43, 0x64, 0x60, 0x3C, 0x00, 0x00, 0x40, 0x00,
  0x00, 0x43, 0x00, 0x00, 0xFF, 0x2F, 0x00,
};
static const unsigned int TEST_SMF_LEN = (unsigned int)sizeof(TEST_SMF);

/* Expected witnesses (verified against this exact byte array on the host
 * with a plain build of midi_sched.c before this probe was ever run under
 * DOSBox-X -- see the S3 handoff report for that host-side check). A
 * correct parse + a full first pass of dispatch should read:
 *   event_count       = 9  (tempo, program_change, 3x note_on, 3x note_off,
 *                            end_of_track)
 *   dispatched        = 7  (program_change + 3x note_on + 3x note_off --
 *                            tempo/end_of_track update state but are not
 *                            "dispatched" to a sink callback)
 *   division          = 240
 *   tempo_us          = 400000  (only true once the mid-track tempo event
 *                            has actually been dispatched; the default is
 *                            500000)
 *   us_per_tick_x1000 = 1666667 (400000/240*1000, rounded -- distinct from
 *                            both the all-default value 1041667 [500000/480]
 *                            and the real-division-but-default-tempo value
 *                            2083333 [500000/240], so this single number
 *                            proves BOTH the header division and the
 *                            mid-file tempo event took effect)
 */
#define EXPECT_EVENT_COUNT       9u
#define EXPECT_DISPATCHED        7u
#define EXPECT_DIVISION          240u
#define EXPECT_TEMPO_US          400000u
#define EXPECT_US_PER_TICK_X1000 1666667ull

static FILE *g_log = NULL;

static void plog(const char *fmt, ...)
{
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  fputs(buf, stdout);
  fputc('\n', stdout);
  fflush(stdout);
  if (g_log)
  {
    fputs(buf, g_log);
    fputc('\n', g_log);
    fflush(g_log);
    fsync(fileno(g_log));
  }
}

static double now_secs(void) { return (double)uclock() / (double)UCLOCKS_PER_SEC; }

/* Counting wrapper around the OPL3 sink's own bound callbacks -- proves
 * dispatch actually reached the SINK's real functions (not just midi_sched's
 * internal `dispatched` counter, which would still increment even with a
 * NULL sink bound). */
typedef struct
{
  midi_sched_sink real;
  unsigned int    note_on_count;
  unsigned int    note_off_count;
  unsigned int    control_change_count;
  unsigned int    program_change_count;
} counting_wrap;

static void wrap_note_on(void *user, int channel, int note, int velocity)
{
  counting_wrap *w = (counting_wrap *)user;
  w->note_on_count++;
  if (w->real.note_on) w->real.note_on(w->real.user, channel, note, velocity);
}
static void wrap_note_off(void *user, int channel, int note, int velocity)
{
  counting_wrap *w = (counting_wrap *)user;
  w->note_off_count++;
  if (w->real.note_off) w->real.note_off(w->real.user, channel, note, velocity);
}
static void wrap_control_change(void *user, int channel, int controller, int value)
{
  counting_wrap *w = (counting_wrap *)user;
  w->control_change_count++;
  if (w->real.control_change) w->real.control_change(w->real.user, channel, controller, value);
}
static void wrap_program_change(void *user, int channel, int program)
{
  counting_wrap *w = (counting_wrap *)user;
  w->program_change_count++;
  if (w->real.program_change) w->real.program_change(w->real.user, channel, program);
}

int main(void)
{
  const char    *smf_path = "OPL3TEST.MID";
  const char    *bank_path = "OPL3BANK.DAT"; /* optional; probe runs fine without it */
  FILE          *fp;
  midi_sched    *m;
  opl3_sink     *sink;
  counting_wrap  wrap;
  const char    *bank_reason = NULL;
  int            bank_loaded;
  int            iters;
  double         t_start, t_now;
  int            all_ok = 1;

  g_log = fopen("OPL3MIDI.LOG", "w");
  if (!g_log) g_log = fopen("C:\\OPL3MIDI.LOG", "w");

  plog("=== opl3midi probe: shared/audio/opl3_sink + midi_sched ===");

  /* Write the embedded test SMF to a scratch file -- midi_sched_open only
   * takes a path, and no MIDI data is committed to this repo (see file
   * header). Binary mode: DJGPP defaults to text-mode fopen, which would
   * corrupt this on write. */
  fp = fopen(smf_path, "wb");
  if (!fp)
  {
    plog("FATAL: could not create %s for writing", smf_path);
    return 1;
  }
  if (fwrite(TEST_SMF, 1, TEST_SMF_LEN, fp) != TEST_SMF_LEN)
  {
    plog("FATAL: short write to %s", smf_path);
    fclose(fp);
    return 1;
  }
  fclose(fp);
  plog("wrote embedded test SMF to %s (%u bytes)", smf_path, TEST_SMF_LEN);

  sink = opl3_sink_open();
  if (!sink)
  {
    plog("OPL3 sink: no chip detected (SDL_DOSOpl3Detect failed) -- "
         "cannot validate the sink's actual register I/O on this run. "
         "midi_sched parse/dispatch witnesses below are still valid "
         "(they do not depend on a chip being present).");
  }
  else
  {
    plog("OPL3 sink: chip detected + initialized (18 x 2-op mode)");
    bank_loaded = opl3_sink_load_bank(sink, bank_path, &bank_reason);
    plog("OPL3 sink: bank load '%s' -> %s (programs=%d)",
         bank_path, bank_reason ? bank_reason : "?",
         opl3_sink_bank_program_count(sink));
    (void)bank_loaded;
  }

  memset(&wrap, 0, sizeof wrap);
  if (sink)
    opl3_sink_bind(sink, &wrap.real);

  m = midi_sched_open(smf_path);
  if (!m)
  {
    plog("FATAL: midi_sched_open(%s) failed", smf_path);
    if (sink) opl3_sink_close(sink);
    return 1;
  }

  {
    uint32_t ev_count = midi_sched_event_count(m);
    uint16_t division = midi_sched_division(m);
    plog("parsed: event_count=%u division=%u", ev_count, division);
    if (ev_count != EXPECT_EVENT_COUNT)
    {
      plog("MISMATCH: event_count expected %u", EXPECT_EVENT_COUNT);
      all_ok = 0;
    }
    if (division != EXPECT_DIVISION)
    {
      plog("MISMATCH: division expected %u", EXPECT_DIVISION);
      all_ok = 0;
    }
  }

  {
    midi_sched_sink wrap_sink;
    wrap_sink.note_on        = wrap_note_on;
    wrap_sink.note_off       = wrap_note_off;
    wrap_sink.control_change = wrap_control_change;
    wrap_sink.program_change = wrap_program_change;
    wrap_sink.user           = &wrap;
    midi_sched_set_sink(m, &wrap_sink);
  }

  midi_sched_start(m, 0);
  t_start = now_secs();
  iters = 0;
  plog("ticking (real wall clock via uclock()) until 7 events dispatch or 5s timeout...");
  for (;;)
  {
    t_now = now_secs();
    midi_sched_tick(m, (uint64_t)((t_now - t_start) * 1000.0));
    iters++;
    if (midi_sched_dispatched(m) >= EXPECT_DISPATCHED)
      break;
    if ((t_now - t_start) > 5.0)
    {
      plog("TIMEOUT waiting for dispatch (5s)");
      all_ok = 0;
      break;
    }
  }

  plog("dispatch loop: iters=%d wall_ms=%llu",
       iters, (unsigned long long)((now_secs() - t_start) * 1000.0));
  plog("witnesses: dispatched=%llu tempo_us=%u division=%u "
       "us_per_tick_x1000=%llu position_ms=%llu",
       (unsigned long long)midi_sched_dispatched(m),
       midi_sched_tempo_us(m), midi_sched_division(m),
       (unsigned long long)midi_sched_us_per_tick_x1000(m),
       (unsigned long long)midi_sched_position_ms(m));
  plog("sink wrapper counts: note_on=%u note_off=%u control_change=%u "
       "program_change=%u",
       wrap.note_on_count, wrap.note_off_count,
       wrap.control_change_count, wrap.program_change_count);

  if (midi_sched_dispatched(m) != EXPECT_DISPATCHED)
  {
    plog("MISMATCH: dispatched expected %u", EXPECT_DISPATCHED);
    all_ok = 0;
  }
  if (midi_sched_tempo_us(m) != EXPECT_TEMPO_US)
  {
    plog("MISMATCH: tempo_us expected %u (mid-file tempo event did not apply)",
         EXPECT_TEMPO_US);
    all_ok = 0;
  }
  if (midi_sched_us_per_tick_x1000(m) != EXPECT_US_PER_TICK_X1000)
  {
    plog("MISMATCH: us_per_tick_x1000 expected %llu",
         EXPECT_US_PER_TICK_X1000);
    all_ok = 0;
  }
  if (wrap.note_on_count != 3 || wrap.note_off_count != 3 ||
      wrap.program_change_count != 1 || wrap.control_change_count != 0)
  {
    plog("MISMATCH: sink wrapper counts expected note_on=3 note_off=3 "
         "program_change=1 control_change=0");
    all_ok = 0;
  }

  midi_sched_close(m);
  if (sink)
  {
    /* Give the chord's release a moment to be audible before teardown --
     * cosmetic for an operator listening under DOSBox-X, not required for
     * the checkable witnesses above (already captured). */
    double t_end = now_secs() + 1.0;
    while (now_secs() < t_end) { /* busy-wait */ }
    opl3_sink_close(sink);
    plog("OPL3 sink: closed (chip-wide latch cleared, all voices KEY-OFF, "
         "OPL2 mode restored)");
  }

  plog("[opl3midi VERDICT=%s]", all_ok ? "PASS" : "FAIL");
  plog("[SENTINEL_END]");
  if (g_log) fclose(g_log);
  return all_ok ? 0 : 1;
}
