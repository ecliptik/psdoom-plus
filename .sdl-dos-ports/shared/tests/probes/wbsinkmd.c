/*
 * wbsinkmd.c -- standalone DOSBox-X probe for shared/audio/waveblaster_sink.
 *
 * Pure DJGPP; no game engine. Plays the same small, self-authored,
 * byte-known Standard MIDI File opl3midi.c uses through shared/audio/
 * midi_sched + shared/audio/waveblaster_sink, and reports checkable
 * output: the parser's own event count, a tempo/division witness, an
 * independent per-callback dispatch count captured by a counting wrapper
 * around the WB sink's own bound functions (proves dispatch reached the
 * SINK, not just midi_sched's internal bookkeeping), AND a THIRD witness
 * this probe has that opl3midi.c does not need: the shared SDL3-DOS
 * layer's own `dos_port_mpu_write_count` (SDL_dos_audio_synth.h), read
 * before and after the run, proving MIDI bytes actually reached
 * SDL_DOSMpu401WriteByte -- the real MPU-401 data-port write function --
 * not just this probe's own wrapper.
 *
 * WHY THIS PROBE EXISTS / WHAT IT ANSWERS: unlike opl3_sink_open()
 * (SDL_DOSOpl3Detect() gives a real presence signal), waveblaster_sink_open()
 * is built on SDL_DOSMpu401Init()'s DEFAULT "cold-init" contract, which is
 * BLIND -- it writes the UART-mode entry sequence without ever reading the
 * status port, so it cannot distinguish "chip present" from "chip absent"
 * (both return success; see waveblaster_sink.h's own doc comment on why:
 * a real hot-bus status read is documented to stall the ISA bus on at
 * least one real card). That means the normal build of this probe proves
 * only that midi_sched + the sink dispatch MIDI bytes correctly -- it does
 * NOT prove DOSBox-X (or real hardware) actually emulates/has an MPU-401
 * that responds to them.
 *
 * This probe additionally, and ONLY here, sets SDL_HINT_DOS_AUDIO_PROBE_MPU401=1
 * (an environment variable read via SDL_getenv(), not a runtime SDL_SetHint
 * call -- see wbsinkmd.bat) BEFORE calling waveblaster_sink_open(), which
 * switches SDL_DOSMpu401Init() to its opt-in, ACK-based real probe sequence
 * (0xFF reset -> poll for a real 0xFE ACK byte -> UART entry) instead of the
 * blind default. SDL_dos_audio_synth.c's own doc comment names this path
 * "LOCKUP RISK on real Vibra16S+S2 -- see W22-WB-D postmortem. Retained for
 * diagnosis on emulated MPU-401 hardware (DOSBox-X) or future chips
 * known-safe to probe." DO NOT set this env var outside of this probe, and
 * NEVER on real hardware -- it exists specifically because this probe's
 * whole job is to answer "does DOSBox-X's MPU-401 emulation ACK a real
 * reset", a question the blind default cannot answer either way.
 *
 * The test tune (TEST_SMF below) is byte-identical to opl3midi.c's own --
 * same format 0, 1 track, 240 PPQ, mid-track tempo meta-event, C-E-G triad
 * (GM program 0) -- so the two probes' witnesses are directly comparable.
 * No third-party MIDI data is committed anywhere; the probe writes its
 * embedded test bytes to a scratch file at startup rather than shipping a
 * .mid asset, matching midi_sched.h's own runtime-read design note.
 *
 * Usage: WBSINKMD.EXE (8.3: WaveBlaster SINK MiDi)
 * Output: WBSNKMD.LOG (+ stdout).
 *
 * BUILD -- like opl3midi.c, this is NOT bare-metal: it calls into the real
 * compiled SDL3-DOS MPU-401 register primitives, so it needs to link
 * against an actual libSDL3.a built with SDL patches 0037/0047/0080-0101
 * applied (any port's own build/sysroot/lib/libSDL3.a works; this hub does
 * not vendor/build SDL itself). Example, using a port's already-built
 * sysroot:
 *   i586-pc-msdosdjgpp-gcc -O2 -c wbsinkmd.c -I../../audio
 *   i586-pc-msdosdjgpp-gcc -O2 -c ../../audio/midi_sched.c
 *   i586-pc-msdosdjgpp-gcc -O2 -c ../../audio/waveblaster_sink.c
 *   i586-pc-msdosdjgpp-gcc -O2 wbsinkmd.o midi_sched.o waveblaster_sink.o \
 *       -L<port>/build/sysroot/lib -lSDL3 -lm -o wbsinkmd.exe
 *   stubedit wbsinkmd.exe minstack=2048k
 *
 * RUN -- main() itself calls setenv("SDL_HINT_DOS_AUDIO_PROBE_MPU401", "1", 1)
 * before touching the sink (see below) -- this env var does not depend on
 * being launched through wbsinkmd.bat's own SET line (kept only for a
 * human running it directly / documentation), so it also works correctly
 * when staged and run via tools/dosbox-run.sh --exe, which generates its
 * own RUN.BAT with no generic --env passthrough. No DOSBox-X conf change
 * is needed for MPU-401 the way opl3midi.c needs `oplmode = opl3` --
 * DOSBox-X emulates MPU-401 at the default port (0x330) whenever its
 * `[midi]` section is not set to `mpu401 = none` (this hub's own shared
 * confs do not disable it); the probe's own witnesses below report
 * whether that emulation actually ACKed, rather than assuming so.
 *
 * License: MIT (probe is original work; see shared/audio/waveblaster_sink.h
 * / THIRD-PARTY.md for the sink module's own provenance).
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "midi_sched.h"
#include "waveblaster_sink.h"

/* Linker shim: same reason opl3midi.c needs one -- the compiled SDL3-DOS
 * static library's MPU-401/OPL3 helpers live in the same translation unit
 * as its (unused-here) SB16 PCM helpers, which reference this
 * engine-provided counter (normally defined by a port's own PCM mixer).
 * This probe never opens an SB16 audio device or plays PCM. */
volatile uint32_t g_dos_sfx_synth_active_count = 0;

/* The real MPU-401 byte-dispatch witness (SDL_dos_audio_synth.h) -- see
 * file header. Declared extern here rather than including the full SDL
 * header, matching waveblaster_sink.c's own cross-vendor extern-decl
 * convention (the header lives under a port's own vendor/SDL tree, not on
 * SDL's public include path). */
extern volatile uint32_t dos_port_mpu_write_count;

/* ---- embedded test SMF: byte-identical to opl3midi.c's TEST_SMF --------*/
static const unsigned char TEST_SMF[] = {
  0x4D, 0x54, 0x68, 0x64, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x01,
  0x00, 0xF0, 0x4D, 0x54, 0x72, 0x6B, 0x00, 0x00, 0x00, 0x21, 0x00, 0xFF,
  0x51, 0x03, 0x06, 0x1A, 0x80, 0x00, 0xC0, 0x00, 0x00, 0x90, 0x3C, 0x64,
  0x00, 0x40, 0x64, 0x00, 0x43, 0x64, 0x60, 0x3C, 0x00, 0x00, 0x40, 0x00,
  0x00, 0x43, 0x00, 0x00, 0xFF, 0x2F, 0x00,
};
static const unsigned int TEST_SMF_LEN = (unsigned int)sizeof(TEST_SMF);

/* Same expected parse/dispatch witnesses as opl3midi.c -- see that file's
 * header for the full derivation; unaffected by which sink is bound. */
#define EXPECT_EVENT_COUNT       9u
#define EXPECT_DISPATCHED        7u
#define EXPECT_DIVISION          240u
#define EXPECT_TEMPO_US          400000u
#define EXPECT_US_PER_TICK_X1000 1666667ull

/* Expected MPU-401 byte count for this tune's 7 dispatched channel-voice
 * events: 1 program_change (2 bytes: status + program) + 3 note_on
 * (3 bytes each) + 3 note_off (3 bytes each) = 2 + 9 + 9 = 20. This is the
 * probe's own strongest witness -- it counts bytes actually written by
 * SDL_DOSMpu401WriteByte(), the real MPU-401 data-port write function,
 * not this probe's own wrapper. */
#define EXPECT_MPU_BYTES 20u

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

/* Counting wrapper around the WB sink's own bound callbacks -- proves
 * dispatch actually reached the SINK's real functions (not just midi_sched's
 * internal `dispatched` counter, which would still increment even with a
 * NULL sink bound). Same pattern as opl3midi.c's counting_wrap. */
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
  const char       *smf_path = "WBTEST.MID";
  FILE             *fp;
  midi_sched       *m;
  waveblaster_sink *sink;
  counting_wrap     wrap;
  int               iters;
  double            t_start, t_now;
  int               all_ok = 1;
  uint32_t          mpu_bytes_before, mpu_bytes_after, mpu_bytes_delta;
  const char        *probe_env;

  g_log = fopen("WBSNKMD.LOG", "w");
  if (!g_log) g_log = fopen("C:\\WBSNKMD.LOG", "w");

  plog("=== wbsinkmidi probe: shared/audio/waveblaster_sink + midi_sched ===");

  /* Set this HERE, in-process, rather than relying on wbsinkmd.bat's own
   * SET line -- this probe may also be staged and run via tools/
   * dosbox-run.sh --exe, which generates its own RUN.BAT with no generic
   * --env passthrough. See file header for why this env var must NEVER be
   * set anywhere outside of this probe. setenv()'s overwrite=1 means an
   * operator/harness override (e.g. accidentally set beforehand) is
   * replaced, not merged -- this probe's own run is always the one that
   * decides its value. */
  setenv("SDL_HINT_DOS_AUDIO_PROBE_MPU401", "1", 1);
  probe_env = getenv("SDL_HINT_DOS_AUDIO_PROBE_MPU401");
  plog("SDL_HINT_DOS_AUDIO_PROBE_MPU401=%s (set in-process just above; expected '1')",
       probe_env ? probe_env : "(unset)");
  if (!probe_env || probe_env[0] != '1' || probe_env[1] != 0)
  {
    plog("WARNING: env var not set to strict '1' -- waveblaster_sink_open() "
         "will use the BLIND default init, which always reports success "
         "regardless of real MPU-401 presence. This run's sink-open result "
         "below will therefore NOT be a meaningful presence signal.");
  }

  /* Write the embedded test SMF to a scratch file. Binary mode: DJGPP
   * defaults to text-mode fopen, which would corrupt this on write. */
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

  mpu_bytes_before = dos_port_mpu_write_count;
  plog("dos_port_mpu_write_count before open: %u", (unsigned)mpu_bytes_before);

  sink = waveblaster_sink_open();
  if (!sink)
  {
    plog("WB sink: waveblaster_sink_open() returned NULL -- SDL_DOSMpu401Init "
         "declined. Under the probe env var above, this means DOSBox-X's "
         "MPU-401 emulation did not ACK a real reset (or is disabled in this "
         "conf's [midi] section) -- NOT necessarily 'no chip', since the "
         "cold-init default this env var bypasses always succeeds regardless "
         "of presence. midi_sched parse/dispatch witnesses below are still "
         "valid (they do not depend on a sink being open).");
  }
  else
  {
    plog("WB sink: opened (SDL_DOSMpu401Init succeeded at the resolved BLASTER port)");
  }

  memset(&wrap, 0, sizeof wrap);
  if (sink)
    waveblaster_sink_bind(sink, &wrap.real);

  m = midi_sched_open(smf_path);
  if (!m)
  {
    plog("FATAL: midi_sched_open(%s) failed", smf_path);
    if (sink) waveblaster_sink_close(sink);
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

  mpu_bytes_after = dos_port_mpu_write_count;
  mpu_bytes_delta = mpu_bytes_after - mpu_bytes_before;
  plog("dos_port_mpu_write_count after run: %u (delta=%u, expected=%u if sink "
       "was open; 0 if waveblaster_sink_open() returned NULL above)",
       (unsigned)mpu_bytes_after, (unsigned)mpu_bytes_delta,
       (unsigned)EXPECT_MPU_BYTES);

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
  /* MPU byte-count is reported but NOT gated into all_ok when the sink
   * failed to open (mpu_bytes_delta will legitimately be 0 in that case,
   * a real DOSBox-X-emulation-absent finding rather than a probe bug). */
  if (sink && mpu_bytes_delta != EXPECT_MPU_BYTES)
  {
    plog("MISMATCH: dos_port_mpu_write_count delta expected %u with sink open",
         (unsigned)EXPECT_MPU_BYTES);
    all_ok = 0;
  }

  midi_sched_close(m);
  if (sink)
  {
    waveblaster_sink_close(sink);
    plog("WB sink: closed (all-notes-off/all-sound-off sent, MPU-401 reset)");
  }

  plog("[wbsinkmidi VERDICT=%s]", all_ok ? "PASS" : "FAIL");
  plog("[wbsinkmidi MPU401_EMULATION=%s]",
       sink ? "ACKED" : "NOT_ACKED_OR_PROBE_ENV_UNSET");
  plog("[SENTINEL_END]");
  if (g_log) fclose(g_log);
  return all_ok ? 0 : 1;
}
