/*
 * opl3_sink.c -- OPL3 General MIDI sink implementation. See opl3_sink.h for
 * scope, provenance, and what was deliberately left out of this hoist.
 *
 * ASCII-only.
 */

#include "opl3_sink.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Cross-vendor extern declarations for the SDL3-DOS OPL3 register
 * primitives (SDL patch 0037, SDL_dos_audio_synth.h) -- same pattern the
 * hoisted-from doskutsu source used: the public header lives under a
 * port's own vendor/SDL/src/core/dos/ tree, not on SDL's public install
 * include path, and shared/ does not vendor SDL itself (see this hub's
 * THIRD-PARTY.md), so these are declared at file scope with C linkage
 * instead of included. Any port linking this file already links a
 * DOS-patched SDL3 exporting these symbols. */
extern bool     SDL_DOSOpl3Detect(void);
extern void     SDL_DOSOpl3InitChip(void);
extern void     SDL_DOSOpl3Shutdown(void);
extern void     SDL_DOSOpl3WriteRegister(uint16_t reg, uint8_t value);
extern void     SDL_DOSOpl3VoiceWritePatch(int voice_id, const uint8_t patch[12]);
extern void     SDL_DOSOpl3VoiceNoteOn(int voice_id, uint16_t freq, uint8_t block);
extern void     SDL_DOSOpl3VoiceNoteOff(int voice_id);

enum
{
  OPL3_VOICES       = 18,
  OPL3_MIDI_CHANNELS = 16,
  OPL3_DRUM_CHANNEL  = 9
};

/* ============================================================================
 * Built-in 8-patch family-bucket GM bank (doskutsu patch 0103, with patch
 * 0171's PATCH_MALLET/PATCH_ORGAN release-rate correction already applied --
 * RR=0 means the YMF262 envelope never decays after KEY-OFF, which is a bug,
 * not a style choice worth reintroducing). 12 bytes per patch: op0 5 regs
 * (AM/VIB/EG/KSR/MULT, KSL/TL, AR/DR, SL/RR, WS), channel FB/CONN, op1 5 regs
 * (same 5 as op0), patch[11] = engine-applied transpose (0 for every built-in
 * patch; only a loaded bank uses this byte -- see opl3_sink_load_bank).
 * ============================================================================
 */

static const uint8_t PATCH_PIANO[12] = {
  0x01, 0x08, 0xE6, 0x48, 0x00, 0xF8,
  0x01, 0x00, 0xF4, 0x48, 0x00, 0x00
};

/* patch 0171 fix baked in: SL=8/RR=8 (was RR=0). */
static const uint8_t PATCH_MALLET[12] = {
  0x01, 0x10, 0xF0, 0x88, 0x00, 0xF8,
  0x01, 0x00, 0xF0, 0xC8, 0x00, 0x00
};

/* patch 0171 fix baked in: RR=8 on both operators (was RR=0). */
static const uint8_t PATCH_ORGAN[12] = {
  0x01, 0x18, 0xF0, 0x08, 0x00, 0xFE,
  0x01, 0x00, 0xF0, 0x08, 0x00, 0x00
};

static const uint8_t PATCH_GUITAR[12] = {
  0x21, 0x10, 0xE2, 0x18, 0x00, 0xFA,
  0x01, 0x00, 0xF1, 0x68, 0x00, 0x00
};

static const uint8_t PATCH_BRASS[12] = {
  0x21, 0x18, 0x91, 0x18, 0x00, 0xFC,
  0x21, 0x00, 0x71, 0x18, 0x00, 0x00
};

static const uint8_t PATCH_REED[12] = {
  0x21, 0x10, 0x71, 0x18, 0x00, 0xF8,
  0x21, 0x00, 0x91, 0x68, 0x00, 0x00
};

static const uint8_t PATCH_PAD[12] = {
  0x21, 0x18, 0x71, 0x08, 0x01, 0xF6,
  0x21, 0x00, 0x51, 0x08, 0x01, 0x00
};

static const uint8_t PATCH_DRUM[12] = {
  0x0F, 0x00, 0xFF, 0x0F, 0x07, 0xF0,
  0x0F, 0x00, 0xFF, 0x0F, 0x07, 0x00
};

/* GM program (0-127) -> family bucket (program / 8), per the GM instrument
 * family table. Buckets not explicitly listed reuse the nearest family that
 * sounds similar on 2-op FM (documented per-bucket in doskutsu's original
 * patch 0103; unchanged here). */
static const uint8_t *bucket_patch(int bucket)
{
  switch (bucket)
  {
    case 0:  return PATCH_PIANO;
    case 1:  return PATCH_MALLET;
    case 2:  return PATCH_ORGAN;
    case 3:  return PATCH_GUITAR;
    case 4:  return PATCH_GUITAR;
    case 5:  return PATCH_PAD;
    case 6:  return PATCH_PAD;
    case 7:  return PATCH_BRASS;
    case 8:  return PATCH_REED;
    case 9:  return PATCH_REED;
    case 10: return PATCH_GUITAR;
    case 11: return PATCH_PAD;
    case 12: return PATCH_PAD;
    case 13: return PATCH_GUITAR;
    case 14: return PATCH_MALLET;
    case 15: return PATCH_PIANO;
    default: return PATCH_PIANO;
  }
}

/* ============================================================================
 * MIDI note -> OPL3 (fnum, block). Canonical AdLib formula: OPL3 master
 * clock 14.318 MHz / 288 = 49716 Hz effective sample rate;
 * freq_hz = fnum * 49716 / 2^(20 - block), inverted for a fixed 12-entry
 * one-octave table at block=4 (MIDI note 60 = C4 = table[0]).
 *
 * The entries are round(f * 65536 / 49716) for C4..B4 (f = 261.63 Hz *
 * 2^(n/12)), so table[0] plays C4 at 261.7 Hz and every entry is within
 * 2 cents of equal temperament. The table used before 2026-09-30
 * ({363, 385, ..., 686}) is the common AdLib table that starts at C#:
 * indexed from C, it played every note ~89 cents sharp (363 -> 275.4 Hz
 * for C4), which a capture measured at +90 cents against the song's
 * render while a WaveBlaster capture of the same scene read -2 cents.
 * ============================================================================
 */

static const uint16_t base_fnum_table[12] = {
  345, 365, 387, 410, 435, 460,
  488, 517, 547, 580, 615, 651
};

static void note_to_fnum_block(int note, uint16_t *out_fnum, uint8_t *out_block)
{
  int rel, octave_delta, semitone, block;

  if (note < 0)   note = 0;
  if (note > 127) note = 127;

  rel = note - 60; /* MIDI note 60 = C4, reference block 4 */
  if (rel >= 0)
  {
    octave_delta = rel / 12;
    semitone     = rel % 12;
  }
  else
  {
    octave_delta = -((-rel + 11) / 12); /* floor-division toward -infinity */
    semitone     = ((rel % 12) + 12) % 12;
  }

  block = 4 + octave_delta;
  if (block < 0) block = 0;
  if (block > 7) block = 7;

  *out_fnum  = base_fnum_table[semitone];
  *out_block = (uint8_t)block;
}

/* ============================================================================
 * DOPL3v1 runtime bank format (doskutsu patches 0141 + 0232):
 *   offset  size  field
 *   0       8     magic "DOPL3v1\n" (0x0A terminator)
 *   8       1     version (1)
 *   9       1     num_programs (1..128)
 *   10      2     reserved (must be 0)
 *   12      N*12  N x 12-byte patch records, same layout as the built-in
 *                 patches above; patch[11] is a SIGNED int8 semitone
 *                 transpose applied at note-on time (DMX/GENMIDI convention:
 *                 playednote = midinote + offset).
 * ============================================================================
 */

struct opl3_sink
{
  struct
  {
    int      active;
    uint8_t  channel;
    uint8_t  note;
    uint64_t last_used;
  } voices[OPL3_VOICES];
  uint64_t voice_alloc_seq;

  uint8_t channel_program[OPL3_MIDI_CHANNELS];

  uint8_t bank[128][12];
  int     bank_programs; /* 0 = no bank loaded; built-in 8-patch bank used */
};

static const uint8_t *program_patch(const opl3_sink *s, int program)
{
  if (program < 0 || program > 127)
    return PATCH_PIANO;
  if (program < s->bank_programs)
    return s->bank[program];
  return bucket_patch(program / 8);
}

opl3_sink *opl3_sink_open(void)
{
  opl3_sink *s;
  int ch;

  if (!SDL_DOSOpl3Detect())
    return NULL;

  s = (opl3_sink *)calloc(1, sizeof(*s));
  if (!s)
    return NULL;

  SDL_DOSOpl3InitChip();

  for (ch = 0; ch < OPL3_MIDI_CHANNELS; ++ch)
    s->channel_program[ch] = 0; /* GM default: Acoustic Grand Piano */

  return s;
}

void opl3_sink_close(opl3_sink *s)
{
  int v;

  if (!s)
    return;

  /* patch 0142 (reg 0xBD chip-wide latch clear): the chip-wide tremolo/
   * vibrato/percussion latch is not touched by per-voice NoteOff and
   * survives KEY-OFF-all + Shutdown otherwise -- clear it FIRST so no
   * subsequent write can race a tremolo-modulated output tail. */
  SDL_DOSOpl3WriteRegister(0xBD, 0x00);

  /* patch 0106 (hanging-note fix): unconditional KEY-OFF on all 18 voices,
   * ignoring this module's own `active` bookkeeping -- the chip's KEY-ON
   * state is ground truth and can drift from engine-side tracking (LRU
   * steals, redundant note-offs, loop-restart re-dispatch on top of a
   * still-sustaining note). */
  for (v = 0; v < OPL3_VOICES; ++v)
    SDL_DOSOpl3VoiceNoteOff(v);

  SDL_DOSOpl3Shutdown();

  free(s);
}

int opl3_sink_load_bank(opl3_sink *s, const char *path, const char **out_reason)
{
  FILE   *fp;
  uint8_t header[12];
  uint8_t version, num_programs, reserved_lo, reserved_hi;
  size_t  payload_bytes;
  static const uint8_t k_magic[8] = { 'D','O','P','L','3','v','1', 0x0A };

  if (!s)
  {
    if (out_reason) *out_reason = "no sink instance";
    return 0;
  }

  fp = fopen(path, "rb");
  if (!fp)
  {
    if (out_reason) *out_reason = "file not found; using 8-patch bank";
    return 0;
  }

  if (fread(header, 1, sizeof header, fp) != sizeof header)
  {
    fclose(fp);
    if (out_reason) *out_reason = "header truncated; using 8-patch bank";
    return 0;
  }
  if (memcmp(header, k_magic, 8) != 0)
  {
    fclose(fp);
    if (out_reason) *out_reason = "magic mismatch; using 8-patch bank";
    return 0;
  }
  version      = header[8];
  num_programs = header[9];
  reserved_lo  = header[10];
  reserved_hi  = header[11];
  if (version != 0x01)
  {
    fclose(fp);
    if (out_reason) *out_reason = "unsupported version; using 8-patch bank";
    return 0;
  }
  if (num_programs == 0 || num_programs > 128)
  {
    fclose(fp);
    if (out_reason) *out_reason = "num_programs out of range; using 8-patch bank";
    return 0;
  }
  if (reserved_lo != 0 || reserved_hi != 0)
  {
    fclose(fp);
    if (out_reason) *out_reason = "reserved field nonzero; using 8-patch bank";
    return 0;
  }

  payload_bytes = (size_t)num_programs * 12u;
  if (fread(s->bank, 1, payload_bytes, fp) != payload_bytes)
  {
    fclose(fp);
    memset(s->bank, 0, sizeof(s->bank));
    if (out_reason) *out_reason = "payload truncated; using 8-patch bank";
    return 0;
  }
  fclose(fp);

  s->bank_programs = (int)num_programs;
  if (out_reason) *out_reason = "loaded";
  return 1;
}

int opl3_sink_bank_program_count(const opl3_sink *s)
{
  return s ? s->bank_programs : 0;
}

static int allocate_voice(opl3_sink *s)
{
  int free_voice = -1, lru_voice = 0, v;

  ++s->voice_alloc_seq;
  for (v = 0; v < OPL3_VOICES; ++v)
  {
    if (!s->voices[v].active && free_voice < 0)
      free_voice = v;
    if (s->voices[v].last_used < s->voices[lru_voice].last_used)
      lru_voice = v;
  }

  {
    int chosen = (free_voice >= 0) ? free_voice : lru_voice;
    if (free_voice < 0)
      SDL_DOSOpl3VoiceNoteOff(chosen); /* LRU steal: silence prior occupant */
    s->voices[chosen].last_used = s->voice_alloc_seq;
    return chosen;
  }
}

static int find_voice(const opl3_sink *s, int channel, int note)
{
  int v;
  for (v = 0; v < OPL3_VOICES; ++v)
  {
    if (s->voices[v].active &&
        s->voices[v].channel == (uint8_t)channel &&
        s->voices[v].note    == (uint8_t)note)
      return v;
  }
  return -1;
}

static void silence_voice(opl3_sink *s, int voice_id)
{
  if (voice_id < 0 || voice_id >= OPL3_VOICES)
    return;
  SDL_DOSOpl3VoiceNoteOff(voice_id);
  s->voices[voice_id].active = 0;
}

static void sink_note_off(void *user, int channel, int note, int velocity);

static void sink_note_on(void *user, int channel, int note, int velocity)
{
  opl3_sink *s = (opl3_sink *)user;
  int voice, play_note;
  const uint8_t *patch;
  uint16_t fnum;
  uint8_t  block;

  if (!s)
    return;

  /* GM convention: note-on velocity 0 == note-off. midi_sched already
   * converts these to NOTE_OFF events, but stay defensive in case a future
   * caller dispatches note_on directly. */
  if (velocity == 0)
  {
    sink_note_off(user, channel, note, 0);
    return;
  }

  voice = allocate_voice(s);
  s->voices[voice].active  = 1;
  s->voices[voice].channel = (uint8_t)(channel & 0x0F);
  s->voices[voice].note    = (uint8_t)(note & 0x7F);

  patch = (channel == OPL3_DRUM_CHANNEL)
              ? PATCH_DRUM
              : program_patch(s, s->channel_program[channel & 0x0F]);
  SDL_DOSOpl3VoiceWritePatch(voice, patch);

  /* patch 0232: patch[11] is a signed transpose applied to the played note
   * only -- the tracked voice note stays the ORIGINAL note so note-off
   * matching is unaffected. Inert (0) for every built-in patch; only a
   * loaded bank (e.g. DMXOPL, ~110/128 programs at -12) uses this. */
  play_note = note + (int)(int8_t)patch[11];
  note_to_fnum_block(play_note, &fnum, &block);
  SDL_DOSOpl3VoiceNoteOn(voice, fnum, block);
}

static void sink_note_off(void *user, int channel, int note, int velocity)
{
  opl3_sink *s = (opl3_sink *)user;
  int voice;
  (void)velocity; /* OPL3 note-off doesn't honor velocity; release is per-patch */

  if (!s)
    return;
  voice = find_voice(s, channel, note);
  if (voice >= 0)
    silence_voice(s, voice);
  /* No matching voice: idempotent no-op (redundant note-off, or the voice
   * was already LRU-stolen). */
}

static void sink_control_change(void *user, int channel, int controller, int value)
{
  opl3_sink *s = (opl3_sink *)user;
  int v;
  (void)value;

  if (!s)
    return;
  switch (controller)
  {
    case 120: /* All Sound Off */
    case 123: /* All Notes Off (collapsed to immediate silence -- per-voice
               * envelopes already govern release timing on OPL3) */
      for (v = 0; v < OPL3_VOICES; ++v)
      {
        if (s->voices[v].active && s->voices[v].channel == (uint8_t)(channel & 0x0F))
          silence_voice(s, v);
      }
      break;
    default:
      break; /* volume/expression/pan/etc: not dispatched at this scope */
  }
}

static void sink_program_change(void *user, int channel, int program)
{
  opl3_sink *s = (opl3_sink *)user;
  if (!s)
    return;
  /* Per GM spec: affects subsequent notes only, never retroactively
   * re-voices already-held notes. */
  s->channel_program[channel & 0x0F] = (uint8_t)(program & 0x7F);
}

void opl3_sink_bind(opl3_sink *s, midi_sched_sink *out)
{
  if (!out)
    return;
  out->note_on         = sink_note_on;
  out->note_off        = sink_note_off;
  out->control_change  = sink_control_change;
  out->program_change  = sink_program_change;
  out->on_tick         = NULL; /* no per-tick state on this backend */
  out->user            = s;
}
