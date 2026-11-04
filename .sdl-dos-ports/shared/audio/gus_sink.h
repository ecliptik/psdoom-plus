#ifndef SHARED_GUS_SINK_H
#define SHARED_GUS_SINK_H

/*
 * gus_sink.h -- native Gravis Ultrasound (GF1) wavetable General MIDI sink
 * for shared/audio/midi_sched.
 *
 * Plain-C rebase of doskutsu's MidiBackendGus (vendor/nxengine-evo/src/
 * sound/MidiBackendGus.{h,cpp}, patches/nxengine-evo/0238 + fixes 0243
 * [per-instrument freq diag], 0244 [.pat root-freq unit auto-detect], 0245
 * [multisample best-sample bring-up fix, superseded below], 0248 [ordering:
 * arm playback after on_song_start -- N/A here, see ordering note below],
 * 0249 [StopAllVoices across song boundary -- NOT carried, see divergence
 * note below], 0250 [GUS_NO_REAP diagnostic killswitch], 0254 [software
 * note-off release ramp], 0255 [per-note multisample residency, supersedes
 * 0245], 0261 [out-of-range drum note clamp]) onto the 4-callback
 * midi_sched_sink contract (see midi_sched.h) plus its optional on_tick
 * hook (added alongside this sink, for the software release ramp) -- same
 * relationship opl3_sink.c/waveblaster_sink.c already have to their own
 * doskutsu originals.
 *
 * Unlike opl3_sink (register-mapped FM synthesis) or waveblaster_sink (a
 * near-direct MIDI byte pass-through to external wavetable hardware), this
 * module owns real DRAM-resident sample data: GM instruments are General
 * MIDI `.pat` files (Gravis Ultrasound patch format) read from the caller's
 * ULTRADIR and uploaded into the GF1 card's on-board sample RAM, then
 * played on the GF1's own hardware voices. GM `.pat` files are NEVER
 * bundled by this module or any caller -- they stay user-supplied, read at
 * runtime from ULTRADIR, exactly like doskutsu's own bring-up. Calls into
 * the shared SDL3-DOS GF1 export surface (SDL patches 0112-0114/0117,
 * SDL3/SDL_dosgus.h: SDL_DOSGusInit/Shutdown/GetState/UploadSample/
 * ResetDram/AllocVoice/StartVoice/StopVoice/SetVoiceFreq/Vol/Pan) for all
 * hardware access -- this module owns GM mapping, the .pat parser, and
 * voice allocation only, never touches a GF1 register directly.
 *
 * PER-SONG RESIDENCY -- read this before calling anything: a GM .pat
 * instrument set for a real song can be tens of MB; the GF1 card carries at
 * most ~1 MB of on-board DRAM. Every instrument therefore loads LAZILY,
 * per song, bounded by what a single song's own note/program-change events
 * actually reference (mirroring doskutsu's on_song_start). This means the
 * sink's lifecycle has a THIRD stage beyond open/close that opl3_sink and
 * waveblaster_sink do not need:
 *
 *   1. gus_sink_open()          -- ONCE per session. Detects + resets the
 *                                   GF1, sizes its DRAM. Comparatively
 *                                   expensive (DRAM peek/poke sizing);
 *                                   never repeat this per song.
 *   2. gus_sink_prepare_song()  -- ONCE per song/clip, in MAIN-LOOP context
 *                                   (file I/O + floating point), called
 *                                   with the midi_sched instance for the
 *                                   song about to play, and MUST be called
 *                                   before midi_sched_start()/
 *                                   midi_sched_tick() on that instance --
 *                                   the IRQ/main-loop note-dispatch path
 *                                   does zero file I/O by design and can
 *                                   only ever play an already-resident
 *                                   sample. Quiesces this sink's own
 *                                   previously-playing voices, frees the
 *                                   prior song's DRAM region, and uploads
 *                                   exactly the instruments the new song's
 *                                   own parsed events reference.
 *   3. gus_sink_close()         -- ONCE per session, at final audio
 *                                   shutdown.
 *
 * Like waveblaster_sink (and unlike opl3_sink's own per-song close/reopen
 * pattern), gus_sink is a SESSION-LIFETIME SINGLETON: never close and
 * reopen this between songs -- call gus_sink_prepare_song() instead. GF1
 * hardware bring-up has no equivalent to waveblaster_sink's one-shot
 * cold-init ordering constraint (the GF1 lives at its own ULTRASND port,
 * unrelated to the SB16 ISA-bus timing WaveBlaster's MPU-401 cold-init
 * depends on) -- gus_sink_open() may be called lazily, at any point.
 *
 * COEXISTING WITH shared/audio/gus_pcm_stream.c -- RESOLVED via
 * shared/audio/gus_dram_floor.{c,h}: gus_sink_open() and
 * gus_sink_prepare_song() both rewind the GF1 DRAM bump allocator on every
 * song boundary (this module keeps nothing persistently resident of its
 * own), but rewind to gus_dram_floor_get() rather than a literal 0 -- the
 * lowest address any coexisting session-lifetime allocation (a
 * gus_pcm_stream's own two fixed half-buffers) has reserved, regardless of
 * whether that reservation happened before or after this sink opened. This
 * is a live, dynamic floor, NOT a one-time snapshot the way doskutsu's own
 * Pixtone-on-GF1 precedent (patches/nxengine-evo/0239's persist_mark) gets
 * away with -- that precedent only ever had to handle "the SFX bank is
 * fully populated once at boot, then MIDI starts," whereas here a
 * gus_pcm_stream can open (and so raise the floor) at ANY point in the
 * session, including after this sink has already played several songs. See
 * gus_dram_floor.h for the full protocol. GUS MIDI and GUS PCM streaming
 * may now be used together in the same session; using either alone is,
 * and always was, unaffected.
 *
 * DIVERGENCE FROM THE DOSKUTSU SOURCE -- deliberate, not an oversight: this
 * module's per-song voice quiesce (inside gus_sink_prepare_song()) stops
 * only voices THIS module's own tracking believes active, which by
 * construction is always the music-only [0, voices - sfx_reserve) slice
 * (SDL_DOSGusAllocVoice() itself never returns an index outside that
 * range -- see SDL_dosgus.h). doskutsu's own patch 0249 additionally calls
 * SDL_DOSGusStopAllVoices() at this same point, because Cave Story's
 * Pixtone SFX and its MIDI music share one session-long lifecycle where
 * "a song changed" is an acceptable moment to silence everything. A port
 * whose PCM clips have an INDEPENDENT lifecycle from its MIDI music
 * channel (e.g. AGS's own speech/ambient/SFX channels, wired in dosags's
 * own dos_audio_mixer) must NOT do that -- an unrelated ambient loop or
 * speech line must not cut out just because a new MIDI clip started. This
 * module therefore intentionally omits patch 0249's broader call; a caller
 * whose own PCM voices are NOT already isolated to the SFX-reserved
 * partition would need to reintroduce it.
 *
 * DOS/DJGPP: no threads, no dynamic C++, ASCII-only. Builds as C99.
 */

#include <stdint.h>

#include "midi_sched.h"

typedef struct gus_sink gus_sink;

/* Bring up the native GF1 (SDL_DOSGusInit(): parses ULTRASND, resets the
 * chip, sizes its on-board DRAM, programs the active-voice count). Returns
 * NULL if no GF1 responds -- caller falls back to another backend or none.
 * Call ONCE per session; never per song (see file header). */
gus_sink *gus_sink_open(void);

/* Per-song lazy instrument residency -- call ONCE per song/clip, from
 * MAIN-LOOP context (file I/O + floating point; never the IRQ/tick note
 * path), with the midi_sched instance for the song about to play, and
 * BEFORE calling midi_sched_start()/midi_sched_tick() on that same
 * instance (see file header's numbered lifecycle). Quiesces this sink's
 * own previously-playing voices, rewinds the GF1 DRAM allocator (keeping
 * nothing resident across songs -- this module bundles no persistent SFX
 * bank of its own), then walks `m`'s parsed events (via
 * midi_sched_collect_instruments()) and uploads exactly the melodic
 * programs and percussion notes that song references, each read from
 * `<ULTRADIR>\<name>.pat` or `<ULTRADIR>\MIDI\<name>.pat` (Gravis stock
 * naming) via the SDL_HINT_DOS_GUS_ULTRADIR hint (default "C:\ULTRASND").
 * A missing/corrupt/oversized .pat leaves that one instrument silent
 * (logged) without failing the call -- matching every other "gracefully
 * do nothing" contract in this hub. Safe to call repeatedly (each call is
 * a fresh song boundary); `s` or `m` NULL is a no-op. */
void gus_sink_prepare_song(gus_sink *s, const midi_sched *m);

/* Stop every voice this sink currently believes active (all-notes-off,
 * effectively) WITHOUT tearing the sink down -- mirrors
 * waveblaster_sink_silence()'s own role for a caller (dosags's
 * dos_midi_backend_end_song()) that needs to guarantee no note is left
 * physically sounding when a song stops, but must NOT close/reopen this
 * sink (see the file header's own "session-lifetime singleton" rule --
 * gus_sink_open()'s DRAM/voice-count sizing is comparatively expensive and
 * must never repeat mid-session). Does not touch DRAM residency at all
 * (unlike gus_sink_prepare_song(), which also frees/reuploads it) -- the
 * next gus_sink_prepare_song() call still does that part when a new song
 * actually starts. Safe to call anytime after gus_sink_open(); `s` NULL is
 * a no-op. */
void gus_sink_silence(gus_sink *s);

/* True teardown: stop every voice this sink owns, SDL_DOSGusShutdown(),
 * free `s`. Call ONCE, at final session/engine audio shutdown -- never
 * between songs (use gus_sink_prepare_song() instead; see file header).
 * Safe to call once; NULL is a no-op. */
void gus_sink_close(gus_sink *s);

/* Populate `out` with function pointers bound to `s` (including the
 * optional on_tick hook this sink uses for its software note-off release
 * ramp -- see midi_sched_sink's own doc comment), ready to pass to
 * midi_sched_set_sink(). */
void gus_sink_bind(gus_sink *s, midi_sched_sink *out);

#endif /* SHARED_GUS_SINK_H */
