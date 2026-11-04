#ifndef SHARED_WAVEBLASTER_SINK_H
#define SHARED_WAVEBLASTER_SINK_H

/*
 * waveblaster_sink.h -- WaveBlaster / MPU-401 General MIDI sink for
 * shared/audio/midi_sched.
 *
 * Plain-C rebase of doskutsu's MidiBackendWaveBlaster (vendor/nxengine-evo/
 * src/sound/MidiBackendWB.{h,cpp}, patch 0102) onto the 4-callback
 * midi_sched_sink contract (see midi_sched.h), the same relationship
 * opl3_sink.c already has to MidiBackendOpl3 -- dropped the C++/MidiBackend/
 * Logger dependency, kept the mechanism: this module owns no GM-program
 * mapping or voice allocation of its own (unlike opl3_sink), since a real
 * WaveBlaster/MPU-401 wavetable synth (or an emulated one, e.g. PicoGUS's
 * WaveBlaster mode) does its own GM voicing in hardware -- every callback
 * here is a near-direct MIDI byte pass-through via the shared MPU-401
 * primitives (SDL patch 0037/0047/0080-0101, SDL_dos_audio_synth.h:
 * SDL_DOSMpu401Init/WriteByte/Shutdown/GetBLASTERPort). This module owns
 * ONLY the byte-framing (3-byte vs. 2-byte messages, 7-bit data masking)
 * and the song-boundary all-notes-off/all-sound-off cleanup; all port I/O,
 * flow-control pacing, and the safe/unsafe access patterns live in the
 * shared SDL layer.
 *
 * IMPORTANT -- one-shot cold-init, unlike opl3_sink: read this before
 * calling waveblaster_sink_open() from anywhere new. The underlying
 * SDL_DOSMpu401Init() defaults to a "cold-init" sequence (SDL/0098) that
 * MUST run on a COLD ISA bus -- specifically BEFORE the SB16 PCM device
 * opens (SDL_DOSAudioSB_IsDeviceHot() becomes true at that point and
 * SDL_DOSMpu401Init() unconditionally declines once it is). This is a
 * real-hardware safety property, not a style preference: a hot-bus MPU-401
 * status read is documented (SDL_dos_audio_synth.c's own W22-WB-D/iter-6
 * history) to stall the ISA bus indefinitely on at least one real card.
 * Consequence for a caller: waveblaster_sink_open() must run ONCE, EARLY
 * in a port's own audio bring-up, strictly before whatever opens the PCM
 * device (e.g. before dos_audio_mixer_init()-style calls) -- never lazily
 * at first-MIDI-clip-play time the way opl3_sink_open() safely can be.
 * A second waveblaster_sink_open() call after the bus has gone hot will
 * fail (returns NULL) even with real WaveBlaster hardware present; this
 * is SDL_DOSMpu401Init()'s own documented decline, not a bug here.
 *
 * Consequence for song-boundary handling too: because open must happen
 * only once, this module deliberately does NOT expose a symmetrical
 * "close and reopen for the next song" pattern the way a naive port might
 * expect from opl3_sink's close()/open() pair. Use waveblaster_sink_close()
 * only at true session/engine shutdown; use waveblaster_sink_silence()
 * between songs (Stop(), a loop restart, a backend swap) to mute any
 * sustaining notes without tearing down the MPU-401 port state -- see each
 * function's own doc comment below.
 *
 * DOS/DJGPP: no threads, no dynamic C++, ASCII-only. Builds as C99.
 */

#include <stdint.h>

#include "midi_sched.h"

typedef struct waveblaster_sink waveblaster_sink;

/* Resolve the MPU-401 port from the BLASTER env var's Pxxxx field (via
 * SDL_DOSMpu401GetBLASTERPort(), default 0x330) and cold-init it (via
 * SDL_DOSMpu401Init()). Returns NULL if init declines -- which, per the
 * shared layer's own "trust the user" contract, can mean no MPU-401/
 * WaveBlaster hardware present, OR that this was called too late (the SB16
 * PCM device is already open/hot -- see the one-shot cold-init note above;
 * check the caller's own init order first before assuming absent
 * hardware). Caller decides fallback (another backend, or none).
 *
 * MUST be called before anything opens the SB16 PCM device this session
 * (see the file header's cold-init note); at most once per session. */
waveblaster_sink *waveblaster_sink_open(void);

/* Mute any sustaining notes (MIDI CC 120 All-Sound-Off + CC 123
 * All-Notes-Off on every one of the 16 channels) WITHOUT tearing down the
 * MPU-401 port state. This is the right call between songs -- a loop
 * restart, an explicit Stop(), a backend swap while WB stays selected --
 * since a real waveblaster_sink_close()/waveblaster_sink_open() cycle
 * would try to cold-init a second time and fail once the PCM device is
 * already hot (see file header). Safe to call repeatedly; NULL is a
 * no-op. */
void waveblaster_sink_silence(waveblaster_sink *s);

/* True teardown: waveblaster_sink_silence()'s own cleanup, then
 * SDL_DOSMpu401Shutdown() (MPU-401 reset, restoring BIOS-default state),
 * then frees `s`. Call this ONCE, at final session/engine audio shutdown
 * -- never between songs (use waveblaster_sink_silence() instead; see
 * file header). Safe to call once; NULL is a no-op. */
void waveblaster_sink_close(waveblaster_sink *s);

/* Populate `out` with function pointers bound to `s`, ready to pass to
 * midi_sched_set_sink(). */
void waveblaster_sink_bind(waveblaster_sink *s, midi_sched_sink *out);

#endif /* SHARED_WAVEBLASTER_SINK_H */
