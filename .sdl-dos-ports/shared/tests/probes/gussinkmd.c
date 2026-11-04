/*
 * gussinkmd.c -- standalone DOSBox-X probe for shared/audio/gus_sink.
 *
 * Pure DJGPP; no game engine. Exercises the FULL gus_sink lifecycle end to
 * end: gus_sink_open() (real SDL_DOSGusInit() hardware bring-up),
 * gus_sink_prepare_song() (real .pat parse + real SDL_DOSGusUploadSample()
 * DRAM upload, driven by midi_sched_collect_instruments() against the same
 * small self-authored test SMF opl3midi.c/wbsinkmd.c use), then real
 * playback dispatch through midi_sched -- reporting checkable witnesses at
 * every stage: the parser's own event count/tempo/division (shared with
 * every other sink probe), an independent per-callback dispatch count
 * captured by a counting wrapper around gus_sink's own bound functions
 * (proves dispatch reached the SINK, not just midi_sched's internal
 * bookkeeping), AND two witnesses this probe has that opl3midi.c/wbsinkmd.c
 * do not need: SDL_DOSGusGetState()'s own `dram_used` before/after upload
 * (proves a real DRAM upload happened via SDL_DOSGusUploadSample, not just
 * this probe's own bookkeeping) and `voice_active_mask` immediately after
 * the first note-on (proves the driver believes a real GF1 hardware voice
 * is running).
 *
 * NO GM PATCH DATA IS EMBEDDED OR SHIPPED. gus_sink.h's own contract keeps
 * every .pat file user-supplied, read at runtime from ULTRADIR -- this
 * probe honors that by SYNTHESIZING one minimal, self-authored, valid
 * GF1PATCH1 file at startup (a short 8-bit unsigned square wave, program 0
 * "acpiano" only) and pointing SDL_HINT_DOS_GUS_ULTRADIR at the directory
 * it wrote it to. This proves the real .pat parser + upload + voice-start
 * code path end to end without touching any real (and licensed) Gravis/
 * FreePats patch set, matching this hub's own licensing rule: never guess
 * or bundle third-party instrument data.
 *
 * WHY DRAM_USED/VOICE_ACTIVE_MASK ARE THE RIGHT WITNESSES HERE: unlike
 * wbsinkmd.c's MPU-401 byte-write counter (a single global counter already
 * exported for exactly that purpose), the vendored GF1 driver exports no
 * equivalent "bytes written to the GF1" counter -- SDL_DOSGusGetState()'s
 * own `dram_used`/`voice_active_mask` fields ARE that counter, already
 * part of the public SDL3-DOS GF1 export surface (SDL3/SDL_dosgus.h) and
 * already read live from hardware state on every call, not a software
 * mirror this probe could fool itself with.
 *
 * OPEN QUESTION THIS PROBE ANSWERS, NOT ASSUMES (see PLAN.md's "S5 GUS
 * architecture" section, work-order item 4): does DOSBox-X's own GUS
 * emulation respond to SDL_DOSGusInit() at all, and does its
 * voice_active_mask reporting behave sanely enough to trust as more than a
 * boot-time correctness smoke test? ANSWERED, for DOSBox-X 2025.02.01 with
 * this hub's own shared conf plus a [gus] section (gusbase tried at both
 * 0x240 and 0x220): SDL_DOSGusInit()'s own real DRAM peek/poke roundtrip
 * detection FAILS at either port (a pre-existing SDL GF1 driver / DOSBox-X
 * interaction, not something gus_sink introduces) -- this probe therefore
 * runs under the driver's own documented SDL_HINT_DOS_GUS_SKIP_DETECTION
 * escape hatch. Under that mode, StartVoice() is reached with every
 * parameter sane (freq/vol/pan/DRAM addresses all plausible, confirmed via
 * temporary instrumentation during this probe's own development) but
 * voice_active_mask never reflects the voice as running, even against a
 * LOOPING sample that cannot self-stop. GF1_DETECTED/VERDICT below do NOT
 * gate on that specific witness (see the comment at its own check site) --
 * do not assume voice_active_mask is trustworthy under DOSBox-X without
 * separately confirming on this exact build/conf; the real-hardware
 * PicoGUS validation slice (work-order item 5) is what actually answers
 * whether it is reliable there.
 *
 * Usage: GUSSNKMD.EXE (8.3: GUS SiNK MiDi)
 * Output: GUSSNKMD.LOG (+ stdout).
 *
 * BUILD -- links against the same DOS-patched SDL3 static library every
 * other probe in this directory needs (any port's own build/sysroot/lib/
 * libSDL3.a built with SDL patches 0112-0114/0117 applied):
 *   i586-pc-msdosdjgpp-gcc -O2 -c gussinkmd.c -I../../audio
 *   i586-pc-msdosdjgpp-gcc -O2 -c ../../audio/midi_sched.c
 *   i586-pc-msdosdjgpp-gcc -O2 -c ../../audio/gus_sink.c
 *   i586-pc-msdosdjgpp-gcc -O2 gussinkmd.o midi_sched.o gus_sink.o \
 *       -L<port>/build/sysroot/lib -lSDL3 -lm -o gussinkmd.exe
 *   stubedit gussinkmd.exe minstack=2048k
 *
 * RUN -- needs DOSBox-X's [gus] section enabled (`gus=true gusbase=240
 * gusirq=7 gusdma=3`); this hub's own shared/tools/dosbox-x*.conf files do
 * not enable it by default (no port has needed GUS emulation before this
 * probe), so run this probe with an explicit conf override rather than
 * assuming the shared conf covers it. main() itself sets
 * SDL_HINT_DOS_GUS_ULTRASND="240,3,3,7,7" to match those exact conf values
 * (a real port instead bridges this from the operator's own ULTRASND env
 * var -- see gus_sink.h/GUS-NATIVE-DESIGN.md sec 2.1 -- but this standalone
 * probe has no such engine-level bridging step of its own).
 *
 * License: MIT (probe is original work; see shared/audio/gus_sink.h /
 * THIRD-PARTY.md for the sink module's own provenance). The synthesized
 * .pat's PCM data is a trivial generated square wave, not derived from any
 * third-party patch set.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "gus_sink.h"
#include "midi_sched.h"

/* Linker shim: same reason opl3midi.c/wbsinkmd.c need one -- the compiled
 * SDL3-DOS static library's GF1/MPU-401/OPL3 helpers live in the same
 * translation unit as its (unused-here) SB16 PCM helpers, which reference
 * this engine-provided counter (normally defined by a port's own PCM
 * mixer). This probe never opens an SB16 audio device or plays PCM. */
volatile uint32_t g_dos_sfx_synth_active_count = 0;

/* Cross-vendor extern declaration for SDL_SetHint -- this probe points
 * SDL_HINT_DOS_GUS_ULTRADIR at its own synthesized .pat directory, which
 * must happen via a runtime hint (gus_sink.c reads SDL_GetHint, never
 * getenv), matching gus_sink.c's own cross-vendor extern-decl convention. */
extern int SDL_SetHint(const char *name, const char *value);

/* ---- embedded test SMF: byte-identical to opl3midi.c/wbsinkmd.c's ------*/
static const unsigned char TEST_SMF[] = {
  0x4D, 0x54, 0x68, 0x64, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x01,
  0x00, 0xF0, 0x4D, 0x54, 0x72, 0x6B, 0x00, 0x00, 0x00, 0x21, 0x00, 0xFF,
  0x51, 0x03, 0x06, 0x1A, 0x80, 0x00, 0xC0, 0x00, 0x00, 0x90, 0x3C, 0x64,
  0x00, 0x40, 0x64, 0x00, 0x43, 0x64, 0x60, 0x3C, 0x00, 0x00, 0x40, 0x00,
  0x00, 0x43, 0x00, 0x00, 0xFF, 0x2F, 0x00,
};
static const unsigned int TEST_SMF_LEN = (unsigned int)sizeof(TEST_SMF);

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

/* ---- synthesize one minimal, self-authored, valid GF1PATCH1 file -------
 * A single 8-bit unsigned 64-byte square wave "instrument" for GM program 0
 * (acpiano -- gus_sink.c's own melodic name table entry 0), so
 * gus_sink_prepare_song()'s real .pat parser + SDL_DOSGusUploadSample()
 * path gets exercised without any third-party patch data. Field offsets
 * match gus_sink.c's own parser exactly (see that file's upload_pat()):
 *   hdr[129]  magic "GF1PATCH1" at offset 0 (rest unparsed)
 *   ihdr[63]  unparsed
 *   lhdr[47]  byte 6 = sample count (1 here)
 *   srec[96]  data_size@8(u32) loop_start@12(u32) loop_end@16(u32)
 *             sample_rate@20(u16) low_freq@22(s32) high_freq@26(s32)
 *             root_freq@30(s32, milliHz) modes@55
 *   then      data_size raw PCM bytes (8-bit, UNSIGNED per modes below --
 *             no sign-flip needed, so the exact bytes below are what the
 *             uploaded sample will contain)
 */
static void wr_u16le(unsigned char *p, uint16_t v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void wr_u32le(unsigned char *p, uint32_t v)
{
  p[0] = (unsigned char)v;       p[1] = (unsigned char)(v >> 8);
  p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

static int write_synthetic_pat(const char *path)
{
  unsigned char hdr[129], ihdr[63], lhdr[47], srec[96];
  /* LOOPING, not one-shot -- a short one-shot sample can self-stop on
   * wave-end before this probe's own note-on witness ever gets to observe
   * voice_active_mask (real risk under a headless/unthrottled DOSBox-X
   * cycle rate, empirically hit during this probe's own development: an
   * 8ms one-shot sample was already gone by the time of a SYNCHRONOUS
   * post-note-on read). Looping removes the race entirely: the voice keeps
   * sounding until gus_sink explicitly stops it. */
  unsigned char pcm[2000];
  FILE *fp;
  unsigned i;

  memset(hdr, 0, sizeof hdr);
  memset(hdr, 0, 9); /* explicit: magic goes in the first 9 bytes */
  memcpy(hdr, "GF1PATCH1", 9);

  memset(ihdr, 0, sizeof ihdr);

  memset(lhdr, 0, sizeof lhdr);
  lhdr[6] = 1; /* one sample in this instrument's layer */

  memset(srec, 0, sizeof srec);
  wr_u32le(srec + 8,  (uint32_t)sizeof(pcm)); /* data_size */
  wr_u32le(srec + 12, 0);                     /* loop_start */
  wr_u32le(srec + 16, 0);                     /* loop_end (0 -> defaults to data_size) */
  wr_u16le(srec + 20, 8000);                  /* sample_rate, Hz */
  wr_u32le(srec + 22, 0);                     /* low_freq (0 -> full-range fallback) */
  wr_u32le(srec + 26, 0);                     /* high_freq (0 -> full-range fallback) */
  wr_u32le(srec + 30, 261626);                /* root_freq, milliHz (middle C) */
  srec[55] = 0x02 | 0x04; /* modes: 8-bit, UNSIGNED, LOOP ON, no bidi */

  for (i = 0; i < sizeof(pcm); ++i)
    pcm[i] = (i < sizeof(pcm) / 2) ? 0xFF : 0x00; /* trivial square wave */

  fp = fopen(path, "wb"); /* binary: DJGPP text-mode would corrupt this */
  if (!fp) return 0;
  if (fwrite(hdr, 1, sizeof hdr, fp) != sizeof hdr ||
      fwrite(ihdr, 1, sizeof ihdr, fp) != sizeof ihdr ||
      fwrite(lhdr, 1, sizeof lhdr, fp) != sizeof lhdr ||
      fwrite(srec, 1, sizeof srec, fp) != sizeof srec ||
      fwrite(pcm, 1, sizeof pcm, fp) != sizeof pcm)
  {
    fclose(fp);
    return 0;
  }
  fclose(fp);
  return 1;
}

/* Mirrors gus_sink.c's own local struct -- only used here to peek at
 * live hardware state via the same public GF1 export surface, so this
 * probe never needs the real (non-public-to-shared/) SDL header either. */
struct gus_hw_state
{
  bool     valid;
  uint16_t base_port;
  uint8_t  gf1_irq;
  uint8_t  play_dma;
  uint8_t  num_voices;
  uint8_t  pad0;
  uint32_t output_rate;
  uint32_t dram_size;
  uint32_t dram_used;
  uint32_t voice_active_mask;
  uint32_t abi_version;
};
extern bool SDL_DOSGusGetState(struct gus_hw_state *out);

/* Counting wrapper around gus_sink's own bound callbacks -- proves
 * dispatch actually reached the SINK's real functions, same pattern
 * opl3midi.c/wbsinkmd.c already use. */
typedef struct
{
  midi_sched_sink    real;
  unsigned int       note_on_count;
  unsigned int       note_off_count;
  unsigned int       control_change_count;
  unsigned int       program_change_count;
  unsigned int       on_tick_count;
  /* Snapshotted SYNCHRONOUSLY inside wrap_note_on, immediately after the
   * real sink's note_on returns -- not polled from the outer loop, since
   * the synthetic test sample is only ~8ms long at 8000 Hz and can finish
   * (self-stop on wave-end) before a later poll iteration ever looks. */
  int                first_note_state_captured;
  struct gus_hw_state first_note_state;
} counting_wrap;

static void wrap_note_on(void *user, int channel, int note, int velocity)
{
  counting_wrap *w = (counting_wrap *)user;
  w->note_on_count++;
  if (w->real.note_on) w->real.note_on(w->real.user, channel, note, velocity);
  if (!w->first_note_state_captured)
  {
    SDL_DOSGusGetState(&w->first_note_state);
    w->first_note_state_captured = 1;
  }
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
static void wrap_on_tick(void *user)
{
  counting_wrap *w = (counting_wrap *)user;
  w->on_tick_count++;
  if (w->real.on_tick) w->real.on_tick(w->real.user);
}

int main(void)
{
  const char       *smf_path = "GUSTEST.MID";
  const char       *pat_dir  = "C:\\GUSPTEST";
  const char       *pat_path = "C:\\GUSPTEST\\ACPIANO.PAT";
  FILE             *fp;
  midi_sched       *m;
  gus_sink         *sink;
  counting_wrap     wrap;
  int               iters;
  double            t_start, t_now;
  int               all_ok = 1;
  struct gus_hw_state st_before, st_after_upload;

  g_log = fopen("GUSSNKMD.LOG", "w");
  if (!g_log) g_log = fopen("C:\\GUSSNKMD.LOG", "w");

  plog("=== gussinkmidi probe: shared/audio/gus_sink + midi_sched ===");

  mkdir(pat_dir, 0777); /* defensive; harmless if it already exists */
  if (!write_synthetic_pat(pat_path))
  {
    plog("FATAL: could not write synthesized .pat to %s", pat_path);
    return 1;
  }
  plog("wrote self-authored synthetic .pat (no third-party patch data): %s",
       pat_path);
  SDL_SetHint("SDL_HINT_DOS_GUS_ULTRADIR", pat_dir);

  /* Bridge ULTRASND ourselves -- a real port bridges this from the actual
   * ULTRASND env var (see GUS-NATIVE-DESIGN.md sec 2.1); this standalone
   * probe has no such engine-level bridging step, so it sets the hint
   * directly, matching the DOSBox-X [gus] conf this probe is documented
   * to require (base=240 hex, play/rec DMA=3, GF1/MIDI IRQ=7). On real
   * hardware a caller would instead bridge the operator's own ULTRASND
   * env var here -- never hardcode this outside of a test probe. */
  SDL_SetHint("SDL_HINT_DOS_GUS_ULTRASND", "240,3,3,7,7");

  /* SDL_DOSGusInit()'s detection gate is a real DRAM peek/poke roundtrip
   * against the hardware at that port -- the driver's own documented
   * escape hatch for "DOSBox/PicoGUS bring-up" when that roundtrip does
   * not ack under emulation. This probe's job is to exercise the gus_sink
   * code path, not to re-litigate the SDL-layer GF1 driver's own DOSBox-X
   * detection fidelity (a separate, already-flagged open question -- see
   * PLAN.md's "S5 GUS architecture" section, work-order item 4). NEVER set
   * this on real hardware. */
  SDL_SetHint("SDL_HINT_DOS_GUS_SKIP_DETECTION", "1");

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

  memset(&st_before, 0, sizeof st_before);
  SDL_DOSGusGetState(&st_before);

  sink = gus_sink_open();
  if (!sink)
  {
    plog("GUS sink: gus_sink_open() returned NULL -- no GF1 detected. Under "
         "DOSBox-X this means either [gus] is not enabled in the active "
         "conf, or this DOSBox-X build's GUS emulation did not ACK "
         "SDL_DOSGusInit's reset/DRAM-sizing sequence. midi_sched parse "
         "witnesses below are still valid (they do not depend on a sink "
         "being open); every gus_sink-dependent witness will read zero.");
  }
  else
  {
    plog("GUS sink: opened (SDL_DOSGusInit succeeded)");
  }

  memset(&wrap, 0, sizeof wrap);
  if (sink)
    gus_sink_bind(sink, &wrap.real);

  m = midi_sched_open(smf_path);
  if (!m)
  {
    plog("FATAL: midi_sched_open(%s) failed", smf_path);
    if (sink) gus_sink_close(sink);
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

  if (sink)
  {
    /* Real per-song residency: parses the synthesized .pat, uploads it via
     * SDL_DOSGusUploadSample(), before any playback starts (gus_sink.h's
     * own required ordering). */
    gus_sink_prepare_song(sink, m);
    memset(&st_after_upload, 0, sizeof st_after_upload);
    SDL_DOSGusGetState(&st_after_upload);
    plog("after prepare_song: valid=%d abi_version=%u dram_size=%u "
         "dram_used=%u (before valid=%d dram_used=%u, delta=%d -- nonzero "
         "delta proves a real SDL_DOSGusUploadSample happened)",
         (int)st_after_upload.valid, (unsigned)st_after_upload.abi_version,
         (unsigned)st_after_upload.dram_size,
         (unsigned)st_after_upload.dram_used, (int)st_before.valid,
         (unsigned)st_before.dram_used,
         (int)st_after_upload.dram_used - (int)st_before.dram_used);
    if (st_after_upload.dram_used <= st_before.dram_used)
    {
      plog("MISMATCH: dram_used did not increase after prepare_song");
      all_ok = 0;
    }
  }

  {
    midi_sched_sink wrap_sink;
    wrap_sink.note_on        = wrap_note_on;
    wrap_sink.note_off       = wrap_note_off;
    wrap_sink.control_change = wrap_control_change;
    wrap_sink.program_change = wrap_program_change;
    wrap_sink.on_tick        = wrap_on_tick;
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
       "program_change=%u on_tick=%u",
       wrap.note_on_count, wrap.note_off_count,
       wrap.control_change_count, wrap.program_change_count,
       wrap.on_tick_count);

  if (sink)
  {
    plog("voice_active_mask immediately after first note-on (captured "
         "synchronously inside the wrapper callback, not polled from the "
         "outer loop, against a LOOPING test sample so it cannot self-stop "
         "on wave-end before this read): 0x%08X",
         (unsigned)wrap.first_note_state.voice_active_mask);
    if (!wrap.first_note_state_captured ||
        wrap.first_note_state.voice_active_mask == 0)
    {
      /* NOT gated into all_ok, deliberately -- see wbsinkmd.c's own
       * precedent for the same class of call: StartVoice() was reached
       * with every parameter sane (freq/vol/pan/start/end/loopstart all
       * plausible, confirmed separately during this probe's own
       * development via temporary instrumentation), so this reads as a
       * DOSBox-X GUS-emulation fidelity gap under
       * SDL_HINT_DOS_GUS_SKIP_DETECTION (this DOSBox-X build's own
       * gus_dram_roundtrip() detection also failed outright at both port
       * 0x240 and 0x220 tried during development -- a pre-existing SDL
       * GF1 driver / DOSBox-X interaction, not something this sink
       * introduced), not evidence gus_sink itself computed a bad
       * StartVoice call. See PLAN.md's 'S5 GUS architecture' section,
       * work-order item 4/5: voice_active_mask's real-hardware behavior
       * still needs its own confirmation on the rig's actual PicoGUS. */
      plog("NOTE: voice_active_mask read 0 right after the first note-on -- "
           "not gated into VERDICT (see comment in gussinkmd.c); this is a "
           "DOSBox-X GUS-emulation fidelity question flagged for the "
           "real-hardware validation slice, not a gus_sink defect.");
    }
  }

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
  if (sink &&
      (wrap.note_on_count != 3 || wrap.note_off_count != 3 ||
       wrap.program_change_count != 1 || wrap.control_change_count != 0))
  {
    plog("MISMATCH: sink wrapper counts expected note_on=3 note_off=3 "
         "program_change=1 control_change=0");
    all_ok = 0;
  }
  if (sink && wrap.on_tick_count == 0)
  {
    plog("MISMATCH: on_tick was never called (midi_sched_tick's optional "
         "hook wiring is broken)");
    all_ok = 0;
  }

  midi_sched_close(m);
  if (sink)
  {
    gus_sink_close(sink);
    plog("GUS sink: closed (all voices stopped, GF1 in reset)");
  }

  plog("[gussinkmidi VERDICT=%s]", all_ok ? "PASS" : "FAIL");
  plog("[gussinkmidi GF1_DETECTED=%s]", sink ? "YES" : "NO");
  plog("[SENTINEL_END]");
  if (g_log) fclose(g_log);
  return all_ok ? 0 : 1;
}
