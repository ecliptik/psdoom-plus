#ifndef SHARED_GUS_PCM_STREAM_H
#define SHARED_GUS_PCM_STREAM_H

/*
 * gus_pcm_stream.h -- indefinite-length PCM streaming over one native
 * Gravis Ultrasound (GF1) hardware voice.
 *
 * This project's OWN design, not a doskutsu rebase -- doskutsu never built
 * PCM streaming on GF1 (its own Pixtone-on-GF1 precedent, patches/
 * nxengine-evo/0239 + fixes, uploads a small FIXED set of short SFX samples
 * ONCE at boot and plays them as ordinary one-shot voices; that shape does
 * not fit an arbitrary-length, arbitrary-count PCM clip). See dosags
 * PLAN.md's "S5 GUS architecture" section (Gap 2, and both "Correction"
 * subsections) for the full research trail this design rests on.
 *
 * THE MECHANISM: a GF1 voice plays from ONE static, already-uploaded DRAM
 * sample -- there is no "feed me more bytes as you go" primitive
 * (SDL3/SDL_dosgus.h's own header: "DMA = upload, NOT streaming"), and no
 * per-voice live playback-position read either, so a genuine hardware ring
 * is not buildable against the current shared SDL3-DOS GF1 export surface.
 * What IS buildable: two FIXED DRAM half-buffers and ONE voice, played
 * one-shot (never looped) and swapped the instant SDL_DOSGusGetState's
 * voice_active_mask reports the current half finished, with the half that
 * just finished refilled in place (SDL_DOSGusOverwriteSample, sdl3-dos
 * patch 0139) from whatever the caller has written since. This is
 * genuinely gapless only if gus_pcm_stream_pump() is called often enough to
 * catch each swap before the OTHER half also runs dry -- unlike a hardware
 * DMA ring, a slow poll cadence can miss the window and produce an audible
 * click/underrun (see gus_pcm_stream_underrun_count() below). This is a
 * real, measured risk, not something this module tries to solve
 * preemptively with more complexity (no wave-end IRQ path exists to remove
 * the polling dependency) -- DOSBox-X and real-hardware validation must
 * characterize it for whatever pump cadence a caller actually uses.
 *
 * WHY 8-BIT UNSIGNED, ALWAYS -- not a caller-chosen format: the real-
 * hardware finding this design depends on (a DMA-based upload does not
 * wedge the PicoGUS while a different voice plays, shared/tests/probes/
 * gusdmawedge.c, HW-486-66, 2026-09-13) only covers DMA uploads, and the
 * shared driver's own DMA path is gated `!is16bit` -- a 16-bit upload
 * ALWAYS silently falls back to PIO regardless of hint or size
 * (SDL_dosaudio_gus.c's own comment: "16-bit always takes PIO, sidesteps
 * the 16-bit DMA dest quirk"), which is the exact wedge-vulnerable path
 * this whole design exists to avoid. A `bits` parameter here that could be
 * misset to 16 would silently reintroduce that risk with no error --
 * closed by never offering the choice. A caller with 16-bit source audio
 * must convert to 8-bit unsigned before calling gus_pcm_stream_write().
 *
 * VOICE ALLOCATION: one voice per open stream, from the reserved SFX
 * partition (SDL_DOSGusAllocVoiceSfx(), sdl3-dos patch 0114) -- never the
 * general pool (SDL_DOSGusAllocVoice()), which is shared/audio/gus_sink.c's
 * own MIDI note-voice pool. This is what keeps a PCM stream from ever
 * stealing a voice mid-song from GUS MIDI music, by construction, with no
 * new arbitration code in either module.
 *
 * DRAM: exactly 2 * (the caller's chunk_bytes, clamped up to
 * GUS_DMA_UPLOAD_MIN if smaller) is allocated ONCE at gus_pcm_stream_open()
 * time and reused for the stream's entire lifetime via
 * SDL_DOSGusOverwriteSample() -- never re-allocated per refill, so DRAM use
 * is bounded and constant regardless of how long the stream plays (the bug
 * a naive repeated-UploadSample design would have: ~11 KB/sec of
 * permanently-consumed DRAM, exhausting the card's <=1 MB budget in around
 * two minutes for a single voice). This module never frees that DRAM
 * itself (there is no per-region free primitive, only the GF1-wide
 * SDL_DOSGusResetDram rewind) -- it lives for the session, same as GUS
 * MIDI's own per-song residency above it in the same DRAM space.
 *
 * COEXISTING WITH shared/audio/gus_sink.c: gus_pcm_stream_open() reserves
 * its two half-buffers against the shared floor (gus_dram_floor.h)
 * immediately after they're allocated, so a later gus_sink song-boundary
 * DRAM rewind can never land on top of them -- true whether this stream
 * opens before gus_sink or after it has already played several songs. GUS
 * MIDI and GUS PCM streaming may be used together in the same session.
 *
 * DOS/DJGPP: no threads, no dynamic C++, ASCII-only. Builds as C99.
 */

#include <stddef.h>
#include <stdbool.h>

typedef struct gus_pcm_stream gus_pcm_stream;

/* Opens one stream: allocates a voice from the reserved SFX partition
 * (SDL_DOSGusAllocVoiceSfx) and two FIXED DRAM half-buffers of
 * `chunk_bytes` each (clamped up to GUS_DMA_UPLOAD_MIN, 4096, if smaller --
 * below that threshold an upload silently falls back to PIO regardless of
 * the DMA hint, per this file's own header comment). Sets
 * SDL_HINT_DOS_GUS_DMA_UPLOAD=1 if the caller/game hasn't already set it to
 * something. `channels` must be 1 (mono) -- the GF1 has no stereo-voice
 * primitive; a caller with stereo source audio must down-mix before
 * calling this. `rate` is the native playback rate of the 8-bit unsigned
 * PCM data this stream will be fed (no resampling happens anywhere in this
 * module -- convert to the target rate before writing).
 *
 * Returns NULL on failure (no GF1 voice available, or DRAM exhausted) --
 * same graceful-degradation contract as the rest of this mixer; the caller
 * plays back silently rather than erroring. */
gus_pcm_stream *gus_pcm_stream_open(unsigned rate, unsigned channels, size_t chunk_bytes);

/* Appends already-8-bit-unsigned-PCM bytes into the stream's pending
 * host-side fill buffer. Returns bytes actually accepted (0 if the pending
 * buffer already holds a full, not-yet-uploaded chunk -- caller retries
 * next pump with the same still-pending data, matching
 * dos_audio_mixer_voice_write()'s own "0 means try again later" contract).
 * `s` NULL is a no-op returning 0. */
size_t gus_pcm_stream_write(gus_pcm_stream *s, const void *data, size_t bytes);

/* Bytes currently buffered host-side, not yet uploaded to either half. */
size_t gus_pcm_stream_queued(const gus_pcm_stream *s);

/* Starts/pauses playback. Pausing silences whatever half is currently
 * sounding but does NOT clear pending/staged data, so a later
 * set_active(true) has something to resume with -- but see
 * gus_pcm_stream_pump()'s own doc comment for the resulting coarse-
 * position behavior across a pause (this module tracks no sub-chunk
 * playback position, matching every other "predicted position" precedent
 * already in this mixer). */
void gus_pcm_stream_set_active(gus_pcm_stream *s, bool active);

/* Stops playback and drops all pending/staged data immediately (silence,
 * not pause) -- matches dos_audio_mixer_voice_stop()'s own "clears the
 * ring + marks inactive" contract. */
void gus_pcm_stream_stop(gus_pcm_stream *s);

/* Sets playback volume, 0.0-1.0 linear, clamped. Applied to the GF1 voice
 * immediately if a half is currently sounding, and to every half started
 * from then on. */
void gus_pcm_stream_set_volume(gus_pcm_stream *s, float volume);

/* Services one stream -- call every pump cycle (the same cadence
 * dos_audio_mixer_pump()/SDL_DOSAudioPump() already run at). If the
 * currently-playing half's voice has gone inactive (voice_active_mask) and
 * the OTHER half already holds a freshly uploaded chunk, swaps onto it
 * immediately (StartVoice) and begins overwriting the half that just
 * finished (SDL_DOSGusOverwriteSample, its FIXED address from open()) with
 * whatever is in the pending buffer. Also opportunistically tops up
 * whichever half is not currently playing whenever a full pending chunk is
 * available, independent of a just-finished swap -- this is what fills the
 * very first half before playback has started at all.
 *
 * No-op if `s` is NULL, the voice failed to allocate, or the stream is not
 * active (set_active(false)/never started). */
void gus_pcm_stream_pump(gus_pcm_stream *s);

/* Cumulative count of pump cycles where the currently-playing half finished
 * and the OTHER half did NOT yet hold a freshly uploaded chunk to swap to
 * -- i.e. the swap missed its window (this module's own precise definition
 * of "underrun": a genuinely late refill, not merely an idle/never-started
 * stream, which is never counted). Each miss leaves the voice silent until
 * a later pump() call finds the off half staged and resumes. */
int gus_pcm_stream_underrun_count(const gus_pcm_stream *s);

/* Stops the voice (returning it to the SFX partition pool) and frees `s`.
 * Does NOT reclaim this stream's own DRAM (no per-region free primitive
 * exists -- see this file's own header comment); that DRAM stays allocated
 * for the rest of the session. Safe to call once; NULL is a no-op. */
void gus_pcm_stream_close(gus_pcm_stream *s);

#endif /* SHARED_GUS_PCM_STREAM_H */
