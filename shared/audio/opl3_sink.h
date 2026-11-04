#ifndef SHARED_OPL3_SINK_H
#define SHARED_OPL3_SINK_H

/*
 * opl3_sink.h -- OPL3 (YMF262) General MIDI sink for shared/audio/midi_sched.
 *
 * Plain-C rebase of doskutsu's MidiBackendOpl3 (vendor/nxengine-evo/src/
 * sound/MidiBackendOpl3.{h,cpp}, patches/nxengine-evo/0103 + fixes 0106
 * [hanging-note shutdown], 0141 [runtime patch-bank loader], 0142 [reg 0xBD
 * chip-wide latch clear], 0171 [PATCH_MALLET/PATCH_ORGAN release-rate fix],
 * 0232 [patch[11] transpose + 8.3 bank filename]) onto the 4-callback
 * midi_sched_sink contract (see midi_sched.h) -- same relationship
 * midi_sched.c already has to MidiScheduler.cpp: same GM-program-to-OPL3
 * mapping, same voice allocator, same register math, rewritten as a
 * standalone C module with no C++/MidiBackend/Logger/ResourceManager
 * dependency and no engine-specific drum-note table (doskutsu's later
 * patches 0298/0307 authored a per-drum-note envelope table tuned to its
 * own org2mid Organya-to-MIDI drum mapping -- that is game-specific and is
 * intentionally NOT carried here; this sink uses one generic drum patch for
 * the whole percussion channel, matching the original patch 0103 shape).
 *
 * Calls into the shared SDL3-DOS OPL3 register primitives (SDL patch 0037,
 * SDL_dos_audio_synth.h: SDL_DOSOpl3Detect/InitChip/Shutdown/WriteRegister/
 * VoiceWritePatch/VoiceNoteOn/VoiceNoteOff) for all register I/O -- this
 * module owns GM mapping + voice allocation only, never touches an OPL3
 * port directly.
 *
 * Also intentionally omitted (same scope line midi_sched.h itself draws for
 * the scheduler): the IRQ-tick / DPMI-lock machinery doskutsu's later
 * patches (0168 Lever 2b, 0234) added for ISR-context dispatch. This sink
 * ticks from the main loop only, exactly like midi_sched_tick().
 *
 * DOS/DJGPP: no threads, no dynamic C++, ASCII-only. Builds as C99.
 */

#include <stdint.h>

#include "midi_sched.h"

typedef struct opl3_sink opl3_sink;

/* Probe for an OPL3 (or OPL2) chip via the AdLib timer-1 detect sequence
 * and, on success, initialize it (18 x 2-op mode, all voices silenced).
 * Returns NULL if no chip responds -- caller falls back to another backend
 * or none. */
opl3_sink *opl3_sink_open(void);

/* Chip-safe teardown: clears the chip-wide register 0xBD tremolo/vibrato/
 * percussion latch, unconditionally KEY-OFFs all 18 voices (ignoring this
 * module's own active-voice bookkeeping -- the chip's KEY-ON state is
 * ground truth and can drift from it), then restores OPL2-compatible chip
 * state. Frees `s`. Safe to call once; NULL is a no-op. */
void opl3_sink_close(opl3_sink *s);

/* Attempt to load a full GM patch bank (DOPL3v1 format -- see
 * gen-opl3-bank.py / opl3bank.dat) from `path`, replacing the built-in
 * 8-patch family-bucket fallback for whichever programs the bank covers.
 * Returns 1 on success, 0 on any failure (missing file, bad magic/version/
 * size) -- the built-in bank keeps working either way. When `out_reason`
 * is non-NULL, *out_reason is set to a short, static, human-readable
 * failure/success description. */
int opl3_sink_load_bank(opl3_sink *s, const char *path, const char **out_reason);

/* Number of programs currently covered by a loaded bank (0 if none loaded
 * -- every program then falls back to the 8-patch family bucket). */
int opl3_sink_bank_program_count(const opl3_sink *s);

/* Populate `out` with function pointers bound to `s`, ready to pass to
 * midi_sched_set_sink(). */
void opl3_sink_bind(opl3_sink *s, midi_sched_sink *out);

#endif /* SHARED_OPL3_SINK_H */
