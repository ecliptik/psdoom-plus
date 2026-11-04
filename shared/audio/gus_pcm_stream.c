/*
 * gus_pcm_stream.c -- native Gravis Ultrasound (GF1) PCM streaming
 * implementation. See gus_pcm_stream.h for scope, provenance, the
 * 8-bit-only rationale, and the DRAM-lifetime contract callers MUST honor.
 *
 * ASCII-only.
 */

#include "gus_pcm_stream.h"
#include "gus_dram_floor.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Cross-vendor extern declarations for the SDL3-DOS GF1 export surface
 * (SDL patches 0112-0114/0117/0139, SDL3/SDL_dosgus.h) plus SDL_Get/SetHint
 * -- same pattern gus_sink.c/opl3_sink.c/waveblaster_sink.c already use for
 * their own primitives: shared/ does not vendor SDL itself (see this hub's
 * THIRD-PARTY.md), so these are declared at file scope with C linkage
 * instead of included. Any port linking this file already links a
 * DOS-patched SDL3 exporting these symbols.
 *
 * gus_hw_state mirrors SDL_DOSGusState (SDL3/SDL_dosgus.h) field-for-field
 * -- same type, deliberately renamed locally so this translation unit
 * never needs the real header (see gus_sink.c's own identical comment). */
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
extern bool     SDL_DOSGusGetState(struct gus_hw_state *out);
extern bool     SDL_DOSGusVoiceActive(int voice);
extern uint32_t SDL_DOSGusUploadSample(const void *data, uint32_t len, int is16bit);
extern bool     SDL_DOSGusOverwriteSample(uint32_t dram_addr, const void *data, uint32_t len, int is16bit);
extern int      SDL_DOSGusAllocVoiceSfx(void);
extern void     SDL_DOSGusStartVoice(int voice, uint32_t start, uint32_t end, uint32_t loopstart, uint32_t flags);
extern void     SDL_DOSGusStopVoice(int voice);
extern void     SDL_DOSGusSetVoiceFreq(int voice, uint32_t playback_hz);
extern void     SDL_DOSGusSetVoiceVol(int voice, int linear_vol);
extern void     SDL_DOSGusSetVoicePan(int voice, int pan);
extern const char *SDL_GetHint(const char *name);
extern bool     SDL_SetHint(const char *name, const char *value);

#define GUS_BAD_ADDR ((uint32_t)0xFFFFFFFFu)

/* GUS_DMA_UPLOAD_MIN, verbatim from SDL_dosaudio_gus.c: DMA upload only
 * engages for 8-bit uploads at or above this many bytes; anything smaller
 * silently falls back to PIO regardless of the DMA hint. A half-buffer
 * smaller than this would silently reintroduce PIO's wedge risk with no
 * error -- see gus_pcm_stream_open()'s own clamp. */
#define GUS_PCM_STREAM_MIN_CHUNK ((size_t)4096u)

/* GM percussion/pan-center convention reused here for a mono source with no
 * panning concept of its own -- 0..255, 128 = dead center. */
#define GUS_PAN_CENTER 128

struct gus_pcm_stream
{
  int      voice;              /* SDL_DOSGusAllocVoiceSfx() result, or -1 */
  uint32_t buf_addr[2];        /* FIXED DRAM addresses, set once at open() */
  bool     staged[2];          /* half i holds an uploaded chunk not yet started */
  int      cur;                /* half currently assigned to the voice, or -1 */
  size_t   chunk_bytes;
  uint32_t rate;
  bool     active;
  int      volume_255;
  uint8_t *pending;             /* host-side fill buffer, chunk_bytes long */
  size_t   pending_len;         /* bytes written into pending so far */
  int      underruns;
};

static void gus_pcm_stream_start_half(gus_pcm_stream *s, int half)
{
  uint32_t start = s->buf_addr[half];
  uint32_t end   = start + (uint32_t)s->chunk_bytes - 1u;

  SDL_DOSGusSetVoiceFreq(s->voice, s->rate);
  SDL_DOSGusSetVoiceVol(s->voice, s->volume_255);
  SDL_DOSGusSetVoicePan(s->voice, GUS_PAN_CENTER);
  /* One-shot, never looped -- looping a half would keep it "active"
   * forever and this module would never see it finish. Gaplessness comes
   * from swapping to the OTHER half the instant this one's mask bit
   * clears, not from hardware looping. */
  SDL_DOSGusStartVoice(s->voice, start, end, start, 0u);
  s->staged[half] = false;
  s->cur = half;
}

/* Uploads whatever is in the pending buffer into `half`'s FIXED address,
 * IF a full chunk is ready -- a no-op otherwise (caller's next write()
 * will eventually complete one). Leaves `pending` intact on a driver-side
 * failure (DRAM bounds -- should not happen against this stream's own
 * fixed, already-validated addresses, but SDL_DOSGusOverwriteSample's own
 * contract can still refuse) so a later retry sees the same data. */
static void gus_pcm_stream_refill(gus_pcm_stream *s, int half)
{
  if (s->pending_len < s->chunk_bytes)
    return;
  if (!SDL_DOSGusOverwriteSample(s->buf_addr[half], s->pending,
                                  (uint32_t)s->chunk_bytes, 0 /* 8-bit unsigned, always */))
    return;
  s->staged[half] = true;
  s->pending_len = 0;
}

gus_pcm_stream *gus_pcm_stream_open(unsigned rate, unsigned channels, size_t chunk_bytes)
{
  gus_pcm_stream *s;
  uint8_t *silence;
  uint32_t addr0, addr1;
  int voice;
  const char *dma_hint;
  struct gus_hw_state st;

  if (rate == 0 || channels != 1)
    return NULL; /* GF1 has no stereo-voice primitive; caller must down-mix first */

  if (chunk_bytes < GUS_PCM_STREAM_MIN_CHUNK)
    chunk_bytes = GUS_PCM_STREAM_MIN_CHUNK;

  /* Bring up the GF1 if nothing else has yet -- a real bug found in
   * review, not a hypothetical: this module used to assume something
   * else (in dosags, shared/audio/gus_sink.c's own MIDI backend) would
   * always call SDL_DOSGusInit() first. If a game plays a PCM sound
   * effect before ever touching MIDI, that assumption is false and every
   * voice would fail forever, silently, even with a real working GF1
   * present. Checking GetState() first (rather than calling Init()
   * unconditionally) makes this safe to call whether or not gus_sink has
   * already brought the card up -- SDL_DOSGusInit() itself does a hard
   * chip reset, so calling it a SECOND time while GUS MIDI notes are
   * already sounding would audibly cut them off. */
  if (!(SDL_DOSGusGetState(&st) && st.valid))
  {
    if (!SDL_DOSGusInit())
      return NULL;
  }

  /* Only set the hint if the caller/game hasn't already picked a value --
   * never override an explicit "0" some other part of the session set on
   * purpose. */
  dma_hint = SDL_GetHint("SDL_HINT_DOS_GUS_DMA_UPLOAD");
  if (!dma_hint || dma_hint[0] == '\0')
    SDL_SetHint("SDL_HINT_DOS_GUS_DMA_UPLOAD", "1");

  /* Reserve the two fixed half-buffers up front, filled with 8-bit
   * unsigned PCM silence (0x80) -- this is the ONE time this stream's own
   * DRAM is allocated (SDL_DOSGusUploadSample); every later refill reuses
   * these same two addresses via SDL_DOSGusOverwriteSample. */
  silence = (uint8_t *)malloc(chunk_bytes);
  if (!silence)
    return NULL;
  memset(silence, 0x80, chunk_bytes);
  addr0 = SDL_DOSGusUploadSample(silence, (uint32_t)chunk_bytes, 0);
  addr1 = (addr0 != GUS_BAD_ADDR)
              ? SDL_DOSGusUploadSample(silence, (uint32_t)chunk_bytes, 0)
              : GUS_BAD_ADDR;
  free(silence);
  if (addr0 == GUS_BAD_ADDR || addr1 == GUS_BAD_ADDR)
    return NULL; /* DRAM exhausted -- graceful degradation, no voice claimed yet */

  /* This stream's two half-buffers are a SESSION-LIFETIME allocation (see
   * this file's own header comment) -- raise the shared floor to protect
   * them from a later shared/audio/gus_sink.c song-boundary DRAM rewind,
   * regardless of whether gus_sink has opened yet or opens later. See
   * gus_dram_floor.h for the full protocol. Must happen before this
   * function can return success (i.e. before any caller could start
   * relying on addr0/addr1 staying valid). */
  gus_dram_floor_reserve(addr1 + (uint32_t)chunk_bytes);

  voice = SDL_DOSGusAllocVoiceSfx();
  if (voice < 0)
    return NULL; /* reserved SFX partition exhausted; the two half-buffer
                  * uploads above are not reclaimed (no per-region free
                  * primitive exists -- see this module's own header) */

  s = (gus_pcm_stream *)calloc(1, sizeof(*s));
  if (!s)
  {
    SDL_DOSGusStopVoice(voice); /* release back to the pool */
    return NULL;
  }
  s->pending = (uint8_t *)malloc(chunk_bytes);
  if (!s->pending)
  {
    SDL_DOSGusStopVoice(voice);
    free(s);
    return NULL;
  }

  s->voice       = voice;
  s->buf_addr[0] = addr0;
  s->buf_addr[1] = addr1;
  s->chunk_bytes = chunk_bytes;
  s->rate        = (uint32_t)rate;
  s->cur         = -1;
  s->active      = false;
  s->volume_255  = 255;
  return s;
}

size_t gus_pcm_stream_write(gus_pcm_stream *s, const void *data, size_t bytes)
{
  size_t room, n;

  if (!s || !data || bytes == 0)
    return 0u;
  room = s->chunk_bytes - s->pending_len;
  if (room == 0)
    return 0u; /* pending already holds a full, not-yet-uploaded chunk */
  n = (bytes < room) ? bytes : room;
  memcpy(s->pending + s->pending_len, data, n);
  s->pending_len += n;
  return n;
}

size_t gus_pcm_stream_queued(const gus_pcm_stream *s)
{
  return s ? s->pending_len : 0u;
}

void gus_pcm_stream_set_active(gus_pcm_stream *s, bool active)
{
  if (!s)
    return;
  if (!active && s->active && s->cur >= 0)
  {
    /* Pause: silence the currently-sounding half but keep pending/staged
     * data so resuming has something to play. This does NOT preserve
     * exact playback position within that half -- the next pump() after
     * resuming will find the voice inactive (we just stopped it) and
     * treat it as finished, advancing to whatever is staged next. Same
     * coarse "predicted position, not exact" precedent
     * OpenAlSource::Pause() already accepts elsewhere in this mixer. */
    SDL_DOSGusStopVoice(s->voice);
  }
  s->active = active;
}

void gus_pcm_stream_stop(gus_pcm_stream *s)
{
  if (!s)
    return;
  if (s->cur >= 0)
    SDL_DOSGusStopVoice(s->voice);
  s->cur          = -1;
  s->staged[0]    = false;
  s->staged[1]    = false;
  s->pending_len  = 0;
  s->active       = false;
}

void gus_pcm_stream_set_volume(gus_pcm_stream *s, float volume)
{
  if (!s)
    return;
  if (volume < 0.0f) volume = 0.0f;
  if (volume > 1.0f) volume = 1.0f;
  s->volume_255 = (int)(volume * 255.0f + 0.5f);
  if (s->cur >= 0)
    SDL_DOSGusSetVoiceVol(s->voice, s->volume_255); /* live-update the sounding half too */
}

void gus_pcm_stream_pump(gus_pcm_stream *s)
{
  bool cur_sounding;
  int off;

  if (!s || !s->active || s->voice < 0)
    return;

  /* SDL_DOSGusVoiceActive (sdl3-dos patch 0140), not SDL_DOSGusGetState --
   * this runs every pump cycle (every rendered frame while a half is
   * sounding), and GetState's full scan over EVERY busy GF1 voice (not just
   * this stream's own) was measured as the dominant real-hardware cost of
   * the whole S5 GUS PCM path (PLAN.md, S5 GUS perf regression finding).
   * VoiceActive does the identical hardware read for just this one voice. */
  cur_sounding = (s->cur >= 0) && SDL_DOSGusVoiceActive(s->voice);

  if (s->cur < 0 || !cur_sounding)
  {
    int next     = (s->cur < 0) ? 0 : (1 - s->cur);
    int finished = s->cur;

    if (s->staged[next])
    {
      gus_pcm_stream_start_half(s, next);
      if (finished >= 0)
        gus_pcm_stream_refill(s, finished);
    }
    else if (s->cur >= 0)
    {
      /* The half that was playing finished and nothing else is staged to
       * swap to -- this module's own precise definition of "underrun":
       * the swap missed its window. The voice is already silent (the
       * hardware stopped it on its own, one-shot with no loop flag); s->cur
       * is deliberately left as-is so the NEXT pump() that finds `next`
       * staged resumes from exactly this same branch. */
      s->underruns++;
    }
    /* else: s->cur < 0 and nothing staged yet -- stream never started;
     * not an underrun, just not fed yet. */
  }

  /* Opportunistically keep the OFF half topped up whenever a full pending
   * chunk is ready, independent of a just-finished swap -- this is what
   * fills the very first half before playback has started at all, and
   * keeps steady-state playback ahead of the next swap. Harmless no-op if
   * the half above was already refilled this same call (pending_len is
   * already 0 by then). */
  off = (s->cur < 0) ? 0 : (1 - s->cur);
  if (!s->staged[off])
    gus_pcm_stream_refill(s, off);
}

int gus_pcm_stream_underrun_count(const gus_pcm_stream *s)
{
  return s ? s->underruns : 0;
}

void gus_pcm_stream_close(gus_pcm_stream *s)
{
  if (!s)
    return;
  if (s->cur >= 0)
    SDL_DOSGusStopVoice(s->voice);
  free(s->pending);
  free(s);
}
