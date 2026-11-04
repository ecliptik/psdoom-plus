#ifndef SHARED_GUS_DRAM_FLOOR_H
#define SHARED_GUS_DRAM_FLOOR_H

/*
 * gus_dram_floor.h -- the one piece of shared state that keeps
 * shared/audio/gus_sink.c (native GF1 GM MIDI) and shared/audio/
 * gus_pcm_stream.c (GF1 PCM streaming) from corrupting each other's DRAM
 * when both are alive in the same session.
 *
 * THE PROBLEM THIS SOLVES: the GF1's DRAM allocator (SDL_DOSGusUploadSample)
 * is a bump allocator with no per-region free -- the only way to reclaim
 * space is SDL_DOSGusResetDram(keep_bytes), which rewinds the WHOLE
 * allocator back to `keep_bytes` and invalidates everything above it.
 * gus_sink calls this on every song boundary (its own per-song .pat
 * residency is deliberately ephemeral -- see gus_sink.h). gus_pcm_stream
 * allocates its two fixed half-buffers ONCE and depends on them staying
 * valid for its entire lifetime (see gus_pcm_stream.h) -- a SESSION-LIFETIME
 * allocation, not a per-song one. Neither module can safely hardcode a
 * boundary between "ephemeral, gus_sink's to rewind" and "session-lifetime,
 * never rewind" DRAM, because the two modules can come up in EITHER order
 * and gus_pcm_stream can open new streams at ANY point mid-session (AGS's
 * own audio channels are independent -- unlike doskutsu's Pixtone-on-GF1
 * precedent, patches/nxengine-evo/0239, which only ever had to handle "the
 * SFX bank is fully populated once at boot, then MIDI starts" -- a single
 * fixed snapshot point that does not exist here).
 *
 * THE PROTOCOL: any module that allocates GF1 DRAM meant to survive for the
 * rest of the session (not just until the next song boundary) calls
 * gus_dram_floor_reserve() with the address one past its own allocation's
 * last byte, as soon as that allocation succeeds. Any module that performs
 * a GF1-wide DRAM rewind (gus_sink's own SDL_DOSGusResetDram() call sites)
 * passes gus_dram_floor_get() instead of a literal 0, so it never rewinds
 * below whatever has already been reserved -- regardless of which module
 * reserved it, or when. The floor only ever rises (there is no
 * corresponding "release" -- matching gus_pcm_stream's own documented "this
 * module never frees its DRAM" contract; a lower floor would imply
 * something below it became safe to overwrite again, which is never true
 * for a session-lifetime allocation).
 *
 * WHY A SEPARATE MODULE rather than gus_sink.c calling into
 * gus_pcm_stream.h directly: this keeps the two sibling modules from
 * needing to know about each other by name -- gus_sink only needs "the
 * lowest safe rewind point," and any future GF1-DRAM-consuming module
 * could reserve against the same floor without gus_sink.c changing at all.
 * Deliberately NOT a general allocator (no reserve-then-release, no
 * per-caller accounting) -- this hub's own "no premature abstraction" rule:
 * the one real thing every current and foreseeable caller needs is a single
 * monotonic watermark, so that is all this provides.
 *
 * DOS/DJGPP: no threads (so no locking needed -- every call happens from
 * ordinary main-loop/init context, never an ISR), no dynamic C++,
 * ASCII-only. Builds as C99.
 */

#include <stdint.h>

/* The current floor: the lowest address any GF1-wide DRAM rewind may pass
 * to SDL_DOSGusResetDram(). Starts at 0 (nothing reserved yet). */
uint32_t gus_dram_floor_get(void);

/* Raises the floor to `through_addr` if that is higher than the current
 * floor; a no-op if `through_addr` is at or below it (never lowers the
 * floor -- see this file's header comment for why). Call once, right after
 * a session-lifetime DRAM allocation succeeds, with the address one past
 * its last byte. */
void gus_dram_floor_reserve(uint32_t through_addr);

/* Resets the floor to 0. Call ONCE, at final session/engine audio
 * shutdown, alongside tearing down every module that reserved against it
 * (gus_sink_close(), every gus_pcm_stream_close()) -- never mid-session;
 * a stray reset while a session-lifetime allocation is still alive would
 * silently let a later gus_sink song-boundary rewind overwrite it again. */
void gus_dram_floor_reset(void);

#endif /* SHARED_GUS_DRAM_FLOOR_H */
