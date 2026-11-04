/*
 * gus_sink.c -- native Gravis Ultrasound (GF1) wavetable General MIDI sink
 * implementation. See gus_sink.h for scope, provenance, the per-song
 * residency lifecycle callers MUST honor, and the one deliberate
 * divergence from the doskutsu source this was rebased from.
 *
 * ASCII-only.
 */

#include "gus_sink.h"
#include "gus_dram_floor.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Cross-vendor extern declarations for the SDL3-DOS GF1 export surface
 * (SDL patches 0112-0114/0117, SDL3/SDL_dosgus.h) plus SDL_GetHint -- same
 * pattern opl3_sink.c/waveblaster_sink.c already use for their own
 * primitives: shared/ does not vendor SDL itself (see this hub's
 * THIRD-PARTY.md), so these are declared at file scope with C linkage
 * instead of included. Any port linking this file already links a
 * DOS-patched SDL3 exporting these symbols.
 *
 * gus_hw_state mirrors SDL_DOSGusState (SDL3/SDL_dosgus.h) field-for-field
 * -- same type, deliberately renamed locally so this translation unit
 * never needs the real header. C only cares that the layout matches; the
 * type name crossing the extern-function boundary does not. */
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

extern bool     SDL_DOSGusInit(void);
extern void     SDL_DOSGusShutdown(void);
extern bool     SDL_DOSGusGetState(struct gus_hw_state *out);
extern uint32_t SDL_DOSGusUploadSample(const void *data, uint32_t len, int is16bit);
extern void     SDL_DOSGusResetDram(uint32_t keep_bytes);
extern int      SDL_DOSGusAllocVoice(void);
extern void     SDL_DOSGusStartVoice(int voice, uint32_t start, uint32_t end, uint32_t loopstart, uint32_t flags);
extern void     SDL_DOSGusStopVoice(int voice);
extern void     SDL_DOSGusSetVoiceFreq(int voice, uint32_t playback_hz);
extern void     SDL_DOSGusSetVoiceVol(int voice, int linear_vol);
extern void     SDL_DOSGusSetVoicePan(int voice, int pan);
extern const char *SDL_GetHint(const char *name);

#define GUS_BAD_ADDR   ((uint32_t)0xFFFFFFFFu)
#define GUS_FLAG_LOOP  0x01u
#define GUS_FLAG_16BIT 0x02u
#define GUS_FLAG_BIDI  0x04u

enum
{
  GUS_MAX_VOICES      = 32,
  GUS_MIDI_CHANNELS   = 16,
  GUS_DRUM_CHANNEL    = 9,
  GUS_MAX_SUBSAMPLES  = 8,   /* patch 0255: resident samples/instrument cap */
  GUS_RELEASE_TICKS   = 12   /* patch 0254: software release-ramp length */
};

/* ============================================================================
 * GM program / drum-note -> Gravis stock .pat base filename. Transcribed
 * from doskutsu's own table (patch 0238), itself transcribed from the
 * TiMidity-compatible gravis.cfg for the GUS default patchset -- the
 * de-facto standard name table every GUS GM config shares. Every name is
 * DOS 8.3-safe (<=8 chars).
 * ============================================================================
 */

static const char *const MELODIC_NAMES[128] = {
  "acpiano",  "britepno", "synpiano", "honky",    "epiano1",  "epiano2",
  "hrpschrd", "clavinet", "celeste",  "glocken",  "musicbox", "vibes",
  "marimba",  "xylophon", "tubebell", "santur",   "homeorg",  "percorg",
  "rockorg",  "church",   "reedorg",  "accordn",  "harmonca", "concrtna",
  "nyguitar", "acguitar", "jazzgtr",  "cleangtr", "mutegtr",  "odguitar",
  "distgtr",  "gtrharm",  "acbass",   "fngrbass", "pickbass", "fretless",
  "slapbas1", "slapbas2", "synbass1", "synbass2", "violin",   "viola",
  "cello",    "contraba", "tremstr",  "pizzcato", "harp",     "timpani",
  "marcato",  "slowstr",  "synstr1",  "synstr2",  "choir",    "doo",
  "voices",   "orchhit",  "trumpet",  "trombone", "tuba",     "mutetrum",
  "frenchrn", "hitbrass", "synbras1", "synbras2", "sprnosax", "altosax",
  "tenorsax", "barisax",  "oboe",     "englhorn", "bassoon",  "clarinet",
  "piccolo",  "flute",    "recorder", "woodflut", "bottle",   "shakazul",
  "whistle",  "ocarina",  "sqrwave",  "sawwave",  "calliope", "chiflead",
  "charang",  "voxlead",  "lead5th",  "basslead", "fantasia", "warmpad",
  "polysyn",  "ghostie",  "bowglass", "metalpad", "halopad",  "sweeper",
  "aurora",   "soundtrk", "crystal",  "atmosphr", "freshair", "unicorn",
  "echovox",  "startrak", "sitar",    "banjo",    "shamisen", "koto",
  "kalimba",  "bagpipes", "fiddle",   "shannai",  "carillon", "agogo",
  "steeldrm", "woodblk",  "taiko",    "toms",     "syntom",   "revcym",
  "fx-fret",  "fx-blow",  "seashore", "jungle",   "telephon", "helicptr",
  "applause", "pistol"
};

/* Percussion: GM note 27..87 -> drum .pat. Index by (note - 27). */
static const char *const DRUM_NAMES[61] = {
  "highq",    "slap",     "scratch1", "scratch2", "sticks",   "sqrclick",
  "metclick", "metbell",  "kick1",    "kick2",    "stickrim", "snare1",
  "claps",    "snare2",   "tomlo2",   "hihatcl",  "tomlo1",   "hihatpd",
  "tommid2",  "hihatop",  "tommid1",  "tomhi2",   "cymcrsh1", "tomhi1",
  "cymride1", "cymchina", "cymbell",  "tamborin", "cymsplsh", "cowbell",
  "cymcrsh2", "vibslap",  "cymride2", "bongohi",  "bongolo",  "congahi1",
  "congahi2", "congalo",  "timbaleh", "timbalel", "agogohi",  "agogolo",
  "cabasa",   "maracas",  "whistle1", "whistle2", "guiro1",   "guiro2",
  "clave",    "woodblk1", "woodblk2", "cuica1",   "cuica2",   "triangl1",
  "triangl2", "shaker",   "jingles",  "belltree", "castinet", "surdo1",
  "surdo2"
};

static const char *melodic_patch_name(int program)
{
  if (program < 0 || program > 127) return NULL;
  return MELODIC_NAMES[program];
}

/* patch 0261: clamp out-of-GM-range drum notes to the nearest defined
 * percussion instead of returning NULL -- a null used to be silently
 * "missing" with no .pat lookup at all; a rare edge note nearest-in-range
 * beats dropping it entirely. */
static const char *drum_patch_name(int note)
{
  if (note < 27) note = 27;
  else if (note > 87) note = 87;
  return DRUM_NAMES[note - 27];
}

/* ============================================================================
 * Little-endian byte readers (the .pat wire format packs multi-byte ints at
 * unaligned offsets; explicit readers avoid struct-packing assumptions).
 * ============================================================================
 */

static uint16_t rd_u16le(const uint8_t *p)
{
  return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t rd_u32le(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int32_t rd_s32le(const uint8_t *p)
{
  return (int32_t)rd_u32le(p);
}

/* patch 0244/0255: convert a .pat frequency field (root / low / high) to a
 * MIDI note, robust to the milliHz-vs-Hz unit ambiguity -- pick whichever
 * interpretation yields a musically-plausible pitch (8..12544 Hz, the full
 * MIDI range), else `fallback`. Callers pass 0 for low_freq (cover down to
 * note 0), 127 for high_freq (cover up to note 127), 60 for root (nearest
 * middle-C default). MAIN-LOOP context only (uses log()/FPU) -- never the
 * note-dispatch path. */
static int pat_freq_to_note(int32_t raw, int fallback)
{
  double milli, hz, f;
  int n;

  milli = (raw > 0) ? (raw / 1000.0) : 0.0;
  hz    = (raw > 0) ? (double)raw    : 0.0;
  if (milli >= 8.0 && milli <= 12544.0)      f = milli;
  else if (hz >= 8.0 && hz <= 12544.0)       f = hz;
  else                                       return fallback;

  n = (int)(69.0 + 12.0 * (log(f / 440.0) / log(2.0)) + 0.5);
  if (n < 0)   n = 0;
  if (n > 127) n = 127;
  return n;
}

/* ============================================================================
 * Resident instrument state
 * ============================================================================
 */

/* One resident DRAM sample of an instrument -- patch 0255: an instrument
 * holds up to GUS_MAX_SUBSAMPLES of these, selected per-note. */
typedef struct
{
  uint32_t dram_addr;
  uint32_t data_size;
  uint32_t loop_start;
  uint32_t loop_end;
  uint32_t sample_rate;
  uint32_t g_fp;        /* (sample_rate/root_hz) << 16, fixed-point;
                          * playback_hz(note) = (g_fp*note_freq[note])>>16 */
  uint16_t flags;       /* GUS_FLAG_LOOP / _16BIT / _BIDI */
  uint8_t  low_note;    /* inclusive MIDI-note range this sample covers */
  uint8_t  high_note;   /* (from the .pat low_freq/high_freq fields) */
  uint8_t  root_note;   /* sample's root MIDI note (nearest-root fallback) */
} sub_sample;

typedef struct
{
  bool       resident;  /* >=1 subsample uploaded and ready */
  uint8_t    nsub;
  sub_sample sub[GUS_MAX_SUBSAMPLES];
} pat_sample;

typedef struct
{
  int      active;
  uint8_t  channel;
  uint8_t  note;
  uint64_t last_used;

  /* patch 0254: software release-ramp state. */
  int releasing;
  int cur_vol;   /* volume programmed at note-on (ramp top) */
  int rel_vol;   /* current ramp volume, stepped down per tick */
  int rel_step;  /* per-tick decrement */
} gus_voice;

struct gus_sink
{
  bool     ready;
  int      num_voices;   /* from SDL_DOSGusGetState */
  uint32_t output_rate;  /* informational */
  uint32_t dram_size;

  bool no_reap;      /* SDL_HINT_DOS_GUS_NO_REAP diagnostic killswitch */
  bool release;      /* SDL_HINT_DOS_GUS_RELEASE, default on */
  bool multisample;  /* SDL_HINT_DOS_GUS_MULTISAMPLE, default on */

  gus_voice voices[GUS_MAX_VOICES];
  uint64_t  voice_alloc_seq;

  pat_sample mel[128];   /* melodic programs, lazy per-song residency */
  pat_sample drum[128];  /* percussion notes (GM 27..87 used) */

  uint8_t channel_program[GUS_MIDI_CHANNELS];
  uint8_t channel_volume[GUS_MIDI_CHANNELS];
  uint8_t channel_expression[GUS_MIDI_CHANNELS];
  uint8_t channel_pan[GUS_MIDI_CHANNELS];

  uint32_t note_freq[128];  /* equal-tempered Hz, rounded, computed once */
};

static bool hint_is_1(const char *name)
{
  const char *v = SDL_GetHint(name);
  return v && v[0] == '1' && v[1] == '\0';
}
static bool hint_is_0(const char *name)
{
  const char *v = SDL_GetHint(name);
  return v && v[0] == '0' && v[1] == '\0';
}

gus_sink *gus_sink_open(void)
{
  gus_sink *s;
  int n;
  struct gus_hw_state st;
  bool did_init = false;

  /* Bring up the GF1 only if nothing else has yet (a real coexistence bug
   * found in review: this used to call SDL_DOSGusInit() unconditionally,
   * which is a hard chip reset -- calling it a second time after
   * shared/audio/gus_pcm_stream.c has already brought the card up would
   * audibly cut off any already-uploaded PCM stream state and re-run the
   * comparatively expensive DRAM/voice-count detection for nothing). Track
   * whether THIS call is the one that actually initialized it, so the
   * failure path below only shuts down what it started. */
  if (!(SDL_DOSGusGetState(&st) && st.valid))
  {
    if (!SDL_DOSGusInit())
      return NULL;
    did_init = true;
  }

  s = (gus_sink *)calloc(1, sizeof(*s));
  if (!s)
  {
    if (did_init)
      SDL_DOSGusShutdown();
    return NULL;
  }

  for (n = 0; n < 128; ++n)
    s->note_freq[n] = (uint32_t)(440.0 * pow(2.0, (n - 69) / 12.0) + 0.5);

  s->num_voices  = 28;
  s->output_rate = 22050;
  if (SDL_DOSGusGetState(&st) && st.valid)
  {
    if (st.num_voices > 0 && st.num_voices <= GUS_MAX_VOICES)
      s->num_voices = st.num_voices;
    if (st.output_rate) s->output_rate = st.output_rate;
    s->dram_size = st.dram_size;
  }

  /* No persistent DRAM region of our own (unlike doskutsu's Pixtone SFX
   * bank -- this module carries no PCM SFX): everything ABOVE the shared
   * floor is a per-song pool, rewound fresh by every
   * gus_sink_prepare_song() call. Rewinding to the floor rather than a
   * literal 0 is what keeps this call from invalidating a
   * shared/audio/gus_pcm_stream.c stream that already reserved its own
   * session-lifetime DRAM before this sink ever opened -- see
   * gus_dram_floor.h for the full protocol. */
  SDL_DOSGusResetDram(gus_dram_floor_get());

  s->no_reap     = hint_is_1("SDL_HINT_DOS_GUS_NO_REAP");
  s->release     = !hint_is_0("SDL_HINT_DOS_GUS_RELEASE");
  s->multisample = !hint_is_0("SDL_HINT_DOS_GUS_MULTISAMPLE");

  for (n = 0; n < GUS_MIDI_CHANNELS; ++n)
  {
    s->channel_program[n]    = 0;
    s->channel_volume[n]     = 100; /* GM default */
    s->channel_expression[n] = 127; /* GM default */
    s->channel_pan[n]        = 64;  /* GM center */
  }

  s->ready = true;
  return s;
}

/* MAIN-LOOP context only (file I/O + FPU). Parses every sample of the
 * instrument's layer (patch 0255), keeping up to GUS_MAX_SUBSAMPLES
 * resident (nearest-middle-C first, so a partial DRAM fit degrades
 * gracefully toward a single sample -- never silence), or exactly one
 * (the pre-0255 behavior) when multisample is off or `is_drum`. */
static bool upload_pat(gus_sink *s, const char *base_name, bool is_drum,
                        pat_sample *out)
{
  const char *ultradir;
  char path[256];
  FILE *fp;
  uint8_t hdr[129], ihdr[63], lhdr[47], srec[96];
  int nsamp, i, want, ncand;
  bool multi;

  struct cand
  {
    uint8_t *pcm;
    uint32_t data_size, loop_start, loop_end, sample_rate, g_fp;
    uint16_t flags;
    uint8_t  low_note, high_note, root_note;
    int      dist60;
  };
  struct cand cand[64];
  int order[64];

  if (!s || !base_name || !out)
    return false;
  out->resident = false;
  out->nsub     = 0;

  ultradir = SDL_GetHint("SDL_HINT_DOS_GUS_ULTRADIR");
  if (!ultradir || ultradir[0] == '\0') ultradir = "C:\\ULTRASND";

  /* Try <ULTRADIR>\<name>.pat then <ULTRADIR>\MIDI\<name>.pat (the Gravis
   * stock layout keeps GM patches under a MIDI subdir). "rb" -- DJGPP
   * text-mode would corrupt the binary .pat (CRLF translation). */
  snprintf(path, sizeof(path), "%s\\%s.pat", ultradir, base_name);
  fp = fopen(path, "rb");
  if (!fp)
  {
    snprintf(path, sizeof(path), "%s\\MIDI\\%s.pat", ultradir, base_name);
    fp = fopen(path, "rb");
  }
  if (!fp)
    return false; /* instrument silent -- no .pat found; not an error */

  if (fread(hdr, 1, sizeof(hdr), fp) != sizeof(hdr) ||
      memcmp(hdr, "GF1PATCH1", 9) != 0)
  {
    fclose(fp);
    return false;
  }
  if (fread(ihdr, 1, sizeof(ihdr), fp) != sizeof(ihdr) ||
      fread(lhdr, 1, sizeof(lhdr), fp) != sizeof(lhdr))
  {
    fclose(fp);
    return false;
  }

  nsamp = lhdr[6];
  if (nsamp < 1)  nsamp = 1;
  if (nsamp > 64) nsamp = 64;

  ncand = 0;
  for (i = 0; i < nsamp; ++i)
  {
    uint32_t s_data, s_lstart, s_lend, s_gfp;
    uint32_t s_rate;
    int32_t  s_low, s_high, s_root;
    uint8_t  s_modes;
    bool     fmt16, is_unsigned, loop_on, bidi, use_loop;
    uint8_t *spcm;
    double   rh_milli, rh_hz, root_hz, g;
    int      root_note, low_note, high_note, dist60;
    uint32_t k;
    struct cand *c;

    if (fread(srec, 1, sizeof(srec), fp) != sizeof(srec))
      break; /* truncated sample header; use whatever we have */

    s_data   = rd_u32le(srec + 8);
    s_lstart = rd_u32le(srec + 12);
    s_lend   = rd_u32le(srec + 16);
    s_rate   = rd_u16le(srec + 20);
    s_low    = rd_s32le(srec + 22);
    s_high   = rd_s32le(srec + 26);
    s_root   = rd_s32le(srec + 30);
    s_modes  = srec[55];

    if (s_data == 0 || (s->dram_size && s_data > s->dram_size) ||
        s_data > (2u * 1024u * 1024u))
      break; /* implausible size -- cannot safely advance the file cursor */

    spcm = (uint8_t *)malloc(s_data);
    if (!spcm) break;
    if (fread(spcm, 1, s_data, fp) != s_data) { free(spcm); break; }

    /* patch 0244: root-freq unit auto-detect (milliHz vs Hz). */
    rh_milli = (s_root > 0) ? (s_root / 1000.0) : 0.0;
    rh_hz    = (s_root > 0) ? (double)s_root    : 0.0;
    if (rh_milli >= 8.0 && rh_milli <= 12544.0)      root_hz = rh_milli;
    else if (rh_hz >= 8.0 && rh_hz <= 12544.0)       root_hz = rh_hz;
    else                                              root_hz = 440.0;
    g    = (double)s_rate / root_hz;
    s_gfp = (uint32_t)(g * 65536.0 + 0.5);
    if (s_gfp == 0) s_gfp = 65536;

    fmt16       = (s_modes & 0x01) != 0;
    is_unsigned = (s_modes & 0x02) != 0;
    loop_on     = (s_modes & 0x04) != 0;
    bidi        = (s_modes & 0x08) != 0;

    /* Normalize to native GF1 DRAM format: 8-bit UNSIGNED / 16-bit
     * SIGNED. The driver uploads verbatim, so the engine owns the sign
     * flip. .pat is most commonly 8-bit signed -> xor 0x80. */
    if (!fmt16)
    {
      if (!is_unsigned)
        for (k = 0; k < s_data; ++k) spcm[k] ^= 0x80;
    }
    else
    {
      if (is_unsigned)
        for (k = 1; k < s_data; k += 2) spcm[k] ^= 0x80; /* hi byte */
    }

    /* Drums are one-shot regardless of the .pat loop flag (a looping drum
     * would ring forever on note-selected percussion). */
    use_loop = loop_on && !is_drum;
    if (s_lend == 0 || s_lend > s_data) s_lend = s_data;
    if (s_lstart > s_data) s_lstart = 0;

    root_note = pat_freq_to_note(s_root, 60);
    low_note  = pat_freq_to_note(s_low, 0);
    high_note = pat_freq_to_note(s_high, 127);
    dist60    = (root_note > 60) ? (root_note - 60) : (60 - root_note);

    /* No internal logging here, deliberately -- matching opl3_sink.c/
     * waveblaster_sink.c's own convention: shared/audio does zero logging
     * of its own (stderr/stdout output mid-game would corrupt a graphics-
     * mode DOS screen). A caller wanting per-sample .pat diagnostics adds
     * its own instrumentation at the port layer (see e.g. dosags's own
     * dos_midi_backend_status_fragment()-style counters). */
    c             = &cand[ncand++];
    c->pcm         = spcm;
    c->data_size   = s_data;
    c->loop_start  = s_lstart;
    c->loop_end    = s_lend;
    c->sample_rate = s_rate ? s_rate : 22050;
    c->g_fp        = s_gfp;
    c->flags       = (uint16_t)((use_loop ? GUS_FLAG_LOOP : 0) |
                                 (fmt16 ? GUS_FLAG_16BIT : 0) |
                                 (use_loop && bidi ? GUS_FLAG_BIDI : 0));
    c->low_note    = (uint8_t)low_note;
    c->high_note   = (uint8_t)high_note;
    c->root_note   = (uint8_t)root_note;
    c->dist60      = dist60;

    if (ncand >= (int)(sizeof(cand) / sizeof(cand[0]))) break;
  }
  fclose(fp);

  if (ncand == 0)
    return false; /* no usable sample; instrument silent */

  /* Upload order: nearest-middle-C first (insertion sort; ncand is tiny). */
  for (i = 0; i < ncand; ++i) order[i] = i;
  for (i = 1; i < ncand; ++i)
  {
    int key = order[i], j = i - 1;
    while (j >= 0 && cand[order[j]].dist60 > cand[key].dist60)
    {
      order[j + 1] = order[j];
      --j;
    }
    order[j + 1] = key;
  }

  /* Single-sample mode (multisample off, or a drum) keeps only the
   * nearest-middle-C sample. Multisample keeps up to GUS_MAX_SUBSAMPLES,
   * soft-capped by DRAM. */
  multi = s->multisample && !is_drum;
  want  = multi ? ncand : 1;
  if (want > GUS_MAX_SUBSAMPLES) want = GUS_MAX_SUBSAMPLES;

  for (i = 0; i < want; ++i)
  {
    struct cand *c = &cand[order[i]];
    uint32_t addr = SDL_DOSGusUploadSample(
        c->pcm, c->data_size, (c->flags & GUS_FLAG_16BIT) ? 1 : 0);
    if (addr == GUS_BAD_ADDR)
      break; /* DRAM exhausted -- keep the central samples already resident */
    {
      sub_sample *ss = &out->sub[out->nsub++];
      ss->dram_addr   = addr;
      ss->data_size   = c->data_size;
      ss->loop_start  = c->loop_start;
      ss->loop_end    = c->loop_end;
      ss->sample_rate = c->sample_rate;
      ss->g_fp        = c->g_fp;
      ss->flags       = c->flags;
      ss->low_note    = c->low_note;
      ss->high_note   = c->high_note;
      ss->root_note   = c->root_note;
    }
  }

  for (i = 0; i < ncand; ++i) free(cand[i].pcm);

  out->resident = (out->nsub > 0);
  return out->resident;
}

void gus_sink_prepare_song(gus_sink *s, const midi_sched *m)
{
  uint8_t mel_used[128], drum_used[128];
  int v, p, n;

  if (!s || !m)
    return;

  /* Quiesce only voices THIS module's own tracking believes active -- by
   * construction always the music-only partition (SDL_DOSGusAllocVoice()
   * never returns an SFX-reserved index; see gus_sink.h's divergence note
   * for why this deliberately does NOT StopAllVoices()). */
  for (v = 0; v < s->num_voices; ++v)
  {
    if (s->voices[v].active)
      SDL_DOSGusStopVoice(v);
    memset(&s->voices[v], 0, sizeof(s->voices[v]));
  }

  /* Free the previous song's DRAM region -- this module keeps nothing
   * persistently resident across songs (no PCM SFX bank of its own).
   * Rewinding to the shared floor, not a literal 0, so any coexisting
   * gus_pcm_stream's own session-lifetime half-buffers survive every song
   * change -- see gus_dram_floor.h. */
  SDL_DOSGusResetDram(gus_dram_floor_get());
  for (n = 0; n < 128; ++n)
  {
    s->mel[n].resident  = false;
    s->drum[n].resident = false;
  }

  for (n = 0; n < GUS_MIDI_CHANNELS; ++n)
  {
    s->channel_program[n]    = 0;
    s->channel_volume[n]     = 100;
    s->channel_expression[n] = 127;
    s->channel_pan[n]        = 64;
  }

  midi_sched_collect_instruments(m, mel_used, drum_used);
  mel_used[0] = 1; /* program 0 (piano) is the GM default for
                     * un-programmed channels; always keep it resident. */

  for (p = 0; p < 128; ++p)
  {
    if (!mel_used[p]) continue;
    upload_pat(s, melodic_patch_name(p), false, &s->mel[p]);
  }
  for (n = 0; n < 128; ++n)
  {
    if (!drum_used[n]) continue;
    upload_pat(s, drum_patch_name(n), true, &s->drum[n]);
  }
}

/* ============================================================================
 * Voice pool
 * ============================================================================
 */

static void reap_finished_voices(gus_sink *s)
{
  struct gus_hw_state st;
  int v;

  /* patch 0250: opt-in diagnostic killswitch. LRU-steal in allocate_voice()
   * still recycles the pool with reap off, so voices do not exhaust. */
  if (s->no_reap) return;

  if (!SDL_DOSGusGetState(&st) || !st.valid) return;
  for (v = 0; v < s->num_voices; ++v)
  {
    if (s->voices[v].active && ((st.voice_active_mask >> v) & 1u) == 0u)
    {
      SDL_DOSGusStopVoice(v);
      memset(&s->voices[v], 0, sizeof(s->voices[v]));
    }
  }
}

static int allocate_voice(gus_sink *s)
{
  int v = SDL_DOSGusAllocVoice();

  reap_finished_voices(s);
  ++s->voice_alloc_seq;

  if (v < 0 || v >= s->num_voices)
  {
    /* Pool exhausted -> LRU-steal the oldest active mirror voice. */
    int lru = 0, i;
    for (i = 1; i < s->num_voices; ++i)
    {
      if (s->voices[i].last_used < s->voices[lru].last_used) lru = i;
    }
    SDL_DOSGusStopVoice(lru);
    memset(&s->voices[lru], 0, sizeof(s->voices[lru]));
    v = SDL_DOSGusAllocVoice();
    if (v < 0 || v >= s->num_voices) v = lru; /* last resort */
  }
  s->voices[v].last_used = s->voice_alloc_seq;
  return v;
}

/* patch 0254: skip voices already in the release ramp so a note-off (or a
 * same-note retrigger's note-off) matches the actively-held voice, not one
 * already fading out. */
static int find_voice(const gus_sink *s, int channel, int note)
{
  int v;
  for (v = 0; v < s->num_voices; ++v)
  {
    if (s->voices[v].active && !s->voices[v].releasing &&
        s->voices[v].channel == (uint8_t)channel &&
        s->voices[v].note    == (uint8_t)note)
      return v;
  }
  return -1;
}

/* patch 0255: pick the resident subsample to play for `note`. Note-on
 * context: int-only, no SDL/FPU/malloc. nsub<=1 returns sub[0]; otherwise
 * the sample whose [low_note,high_note] covers the note, else the nearest
 * root note (covers keyboard-extreme gaps in the resident set). */
static const sub_sample *pick_subsample(const pat_sample *ps, int note)
{
  int k, best, bestd;

  if (ps->nsub <= 1) return &ps->sub[0];
  for (k = 0; k < ps->nsub; ++k)
  {
    if (note >= (int)ps->sub[k].low_note && note <= (int)ps->sub[k].high_note)
      return &ps->sub[k];
  }
  best = 0; bestd = 1000;
  for (k = 0; k < ps->nsub; ++k)
  {
    int d = (int)ps->sub[k].root_note - note;
    if (d < 0) d = -d;
    if (d < bestd) { bestd = d; best = k; }
  }
  return &ps->sub[best];
}

/* ============================================================================
 * midi_sched_sink dispatch
 * ============================================================================
 */

static void sink_note_off(void *user, int channel, int note, int velocity);

static void sink_note_on(void *user, int channel, int note, int velocity)
{
  gus_sink *s = (gus_sink *)user;
  int ch, v, is_drum, pan255, lin, vol255;
  const pat_sample *ps;
  const sub_sample *ss;
  uint32_t hz, start, end, loopstart;

  if (!s || !s->ready) return;
  if (velocity == 0) { sink_note_off(user, channel, note, 0); return; }

  ch      = channel & 0x0F;
  note   &= 0x7F;
  is_drum = (ch == GUS_DRUM_CHANNEL);
  ps      = is_drum ? &s->drum[note] : &s->mel[s->channel_program[ch] & 0x7F];
  if (!ps->resident) return; /* not pre-uploaded at prepare_song -> silent */

  ss = pick_subsample(ps, note);

  v = allocate_voice(s);
  s->voices[v].active     = 1;
  s->voices[v].releasing  = 0; /* fresh note cancels any ramp */
  s->voices[v].channel    = (uint8_t)ch;
  s->voices[v].note       = (uint8_t)note;

  if (is_drum)
    hz = ss->sample_rate;
  else
    hz = (uint32_t)(((uint64_t)ss->g_fp * s->note_freq[note]) >> 16);
  if (hz == 0) hz = ss->sample_rate;

  /* Volume: velocity * channel-volume(CC7) * expression(CC11), 0..255. */
  lin    = velocity;
  lin    = (lin * s->channel_volume[ch]) / 127;
  lin    = (lin * s->channel_expression[ch]) / 127;
  vol255 = (lin * 255) / 127;
  if (vol255 > 255) vol255 = 255;
  s->voices[v].cur_vol = vol255;

  pan255 = (int)s->channel_pan[ch] << 1;
  if (pan255 > 255) pan255 = 255;

  start     = ss->dram_addr;
  end       = ss->dram_addr + ss->loop_end;
  loopstart = ss->dram_addr + ss->loop_start;

  SDL_DOSGusSetVoiceFreq(v, hz);
  SDL_DOSGusSetVoiceVol(v, vol255);
  SDL_DOSGusSetVoicePan(v, pan255);
  SDL_DOSGusStartVoice(v, start, end, loopstart, ss->flags);
}

static void sink_note_off(void *user, int channel, int note, int velocity)
{
  gus_sink *s = (gus_sink *)user;
  int v;
  (void)velocity;

  if (!s || !s->ready) return;
  v = find_voice(s, channel & 0x0F, note & 0x7F);
  if (v < 0) return;

  /* patch 0254: software release ramp -- the GF1 exports no hardware
   * volume-ramp engine, so an instant StopVoice on every note-off is a
   * percussive click on sustained instruments. When enabled, mark the
   * voice releasing and let on_tick step its volume down to silence over
   * GUS_RELEASE_TICKS, then stop it; the decrement is sized from the
   * note-on volume so the release duration stays ~constant. */
  if (s->release && s->voices[v].cur_vol > 0)
  {
    int step = s->voices[v].cur_vol / GUS_RELEASE_TICKS;
    s->voices[v].releasing = 1;
    s->voices[v].rel_vol   = s->voices[v].cur_vol;
    s->voices[v].rel_step  = (step > 0) ? step : 1;
  }
  else
  {
    SDL_DOSGusStopVoice(v);
    memset(&s->voices[v], 0, sizeof(s->voices[v]));
  }
}

static void sink_control_change(void *user, int channel, int controller, int value)
{
  gus_sink *s = (gus_sink *)user;
  int ch, v;

  if (!s || !s->ready) return;
  ch = channel & 0x0F;
  switch (controller)
  {
    case 7:  s->channel_volume[ch]     = (uint8_t)(value & 0x7F); break;
    case 10: s->channel_pan[ch]        = (uint8_t)(value & 0x7F); break;
    case 11: s->channel_expression[ch] = (uint8_t)(value & 0x7F); break;
    case 120: /* All Sound Off */
    case 123: /* All Notes Off */
      for (v = 0; v < s->num_voices; ++v)
      {
        if (s->voices[v].active && s->voices[v].channel == (uint8_t)ch)
        {
          SDL_DOSGusStopVoice(v);
          memset(&s->voices[v], 0, sizeof(s->voices[v]));
        }
      }
      break;
    default:
      break;
  }
}

static void sink_program_change(void *user, int channel, int program)
{
  gus_sink *s = (gus_sink *)user;
  if (!s || !s->ready) return;
  s->channel_program[channel & 0x0F] = (uint8_t)(program & 0x7F);
}

/* patch 0254: step every releasing voice's volume down one notch per
 * scheduler tick; when it reaches silence, stop and free it. */
static void sink_on_tick(void *user)
{
  gus_sink *s = (gus_sink *)user;
  int v, nv;

  if (!s || !s->ready || !s->release) return;
  for (v = 0; v < s->num_voices; ++v)
  {
    if (!s->voices[v].active || !s->voices[v].releasing) continue;
    nv = s->voices[v].rel_vol - s->voices[v].rel_step;
    if (nv <= 0)
    {
      SDL_DOSGusStopVoice(v);
      memset(&s->voices[v], 0, sizeof(s->voices[v]));
    }
    else
    {
      s->voices[v].rel_vol = nv;
      SDL_DOSGusSetVoiceVol(v, nv);
    }
  }
}

void gus_sink_silence(gus_sink *s)
{
  int v;

  if (!s) return;

  for (v = 0; v < s->num_voices; ++v)
  {
    if (s->voices[v].active)
    {
      SDL_DOSGusStopVoice(v);
      memset(&s->voices[v], 0, sizeof(s->voices[v]));
    }
  }
}

void gus_sink_close(gus_sink *s)
{
  int v;

  if (!s) return;

  for (v = 0; v < s->num_voices; ++v)
    SDL_DOSGusStopVoice(v);
  SDL_DOSGusShutdown();
  free(s);
}

void gus_sink_bind(gus_sink *s, midi_sched_sink *out)
{
  if (!out) return;
  out->note_on        = sink_note_on;
  out->note_off       = sink_note_off;
  out->control_change = sink_control_change;
  out->program_change = sink_program_change;
  out->on_tick        = sink_on_tick;
  out->user           = s;
}
