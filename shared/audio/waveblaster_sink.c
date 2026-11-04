/*
 * waveblaster_sink.c -- WaveBlaster / MPU-401 General MIDI sink
 * implementation. See waveblaster_sink.h for scope, provenance, and the
 * one-shot cold-init contract this module's callers must honor.
 *
 * ASCII-only.
 */

#include "waveblaster_sink.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/* Cross-vendor extern declarations for the SDL3-DOS MPU-401 register
 * primitives (SDL patches 0037/0047/0080-0101, SDL_dos_audio_synth.h) --
 * same pattern opl3_sink.c already uses for the OPL3 primitives: the
 * public header lives under a port's own vendor/SDL/src/core/dos/ tree,
 * not on SDL's public install include path, and shared/ does not vendor
 * SDL itself (see this hub's THIRD-PARTY.md), so these are declared at
 * file scope with C linkage instead of included. Any port linking this
 * file already links a DOS-patched SDL3 exporting these symbols. */
extern bool     SDL_DOSMpu401Init(uint16_t port_base);
extern void     SDL_DOSMpu401WriteByte(uint8_t byte);
extern void     SDL_DOSMpu401Shutdown(void);
extern uint16_t SDL_DOSMpu401GetBLASTERPort(void);

enum
{
  WB_MIDI_CHANNELS = 16
};

/* MIDI status nibble constants (high nibble = event type; low nibble =
 * channel 0-15). */
static const uint8_t MIDI_NOTE_OFF       = 0x80;
static const uint8_t MIDI_NOTE_ON        = 0x90;
static const uint8_t MIDI_CONTROL_CHANGE = 0xB0;
static const uint8_t MIDI_PROGRAM_CHANGE = 0xC0;

/* MIDI Control Change numbers used by waveblaster_sink_silence(). */
static const uint8_t CC_ALL_SOUND_OFF = 0x78; /* 120 -- immediate cut */
static const uint8_t CC_ALL_NOTES_OFF = 0x7B; /* 123 -- graceful release */

struct waveblaster_sink
{
  int placeholder; /* no per-instance state: the shared layer owns the
                     * single-instance MPU-401 port state (see
                     * SDL_dos_audio_synth.c); this struct exists only so
                     * callers get an opaque, non-NULL handle to check and
                     * pass around, matching opl3_sink's API shape. */
};

static void send3(uint8_t status, uint8_t data1, uint8_t data2)
{
  SDL_DOSMpu401WriteByte(status);
  SDL_DOSMpu401WriteByte((uint8_t)(data1 & 0x7F));
  SDL_DOSMpu401WriteByte((uint8_t)(data2 & 0x7F));
}

waveblaster_sink *waveblaster_sink_open(void)
{
  waveblaster_sink *s;
  uint16_t port;

  port = SDL_DOSMpu401GetBLASTERPort();
  if (!SDL_DOSMpu401Init(port))
    return NULL;

  s = (waveblaster_sink *)calloc(1, sizeof(*s));
  if (!s)
  {
    /* Match opl3_sink_open()'s own fail-safe posture: never leave the
     * port initialized with no owning handle. SDL_DOSMpu401Shutdown() is
     * idempotent and safe here. */
    SDL_DOSMpu401Shutdown();
    return NULL;
  }

  return s;
}

void waveblaster_sink_silence(waveblaster_sink *s)
{
  int ch;

  if (!s)
    return;

  for (ch = 0; ch < WB_MIDI_CHANNELS; ++ch)
  {
    send3((uint8_t)(MIDI_CONTROL_CHANGE | ch), CC_ALL_SOUND_OFF, 0x00);
    send3((uint8_t)(MIDI_CONTROL_CHANGE | ch), CC_ALL_NOTES_OFF, 0x00);
  }
}

void waveblaster_sink_close(waveblaster_sink *s)
{
  if (!s)
    return;

  waveblaster_sink_silence(s);
  SDL_DOSMpu401Shutdown();
  free(s);
}

static void sink_note_on(void *user, int channel, int note, int velocity)
{
  (void)user;
  send3((uint8_t)(MIDI_NOTE_ON | (channel & 0x0F)), (uint8_t)note, (uint8_t)velocity);
}

static void sink_note_off(void *user, int channel, int note, int velocity)
{
  (void)user;
  send3((uint8_t)(MIDI_NOTE_OFF | (channel & 0x0F)), (uint8_t)note, (uint8_t)velocity);
}

static void sink_control_change(void *user, int channel, int controller, int value)
{
  (void)user;
  send3((uint8_t)(MIDI_CONTROL_CHANGE | (channel & 0x0F)), (uint8_t)controller, (uint8_t)value);
}

static void sink_program_change(void *user, int channel, int program)
{
  /* Program-change is a 2-byte MIDI message (status + program; no second
   * data byte) -- cannot use send3(). */
  (void)user;
  SDL_DOSMpu401WriteByte((uint8_t)(MIDI_PROGRAM_CHANGE | (channel & 0x0F)));
  SDL_DOSMpu401WriteByte((uint8_t)(program & 0x7F));
}

void waveblaster_sink_bind(waveblaster_sink *s, midi_sched_sink *out)
{
  if (!out)
    return;
  out->note_on        = sink_note_on;
  out->note_off       = sink_note_off;
  out->control_change = sink_control_change;
  out->program_change = sink_program_change;
  out->on_tick        = NULL; /* no per-tick state on this backend */
  out->user           = s;
}
