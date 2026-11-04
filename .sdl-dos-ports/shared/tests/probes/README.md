# Hardware diagnostic probes

Standalone, DJGPP-only diagnostic tools for characterizing DOS-era
hardware: memory bandwidth, palette DAC behavior, VESA/Cirrus/S3 chip
identification, WaveBlaster/MPU-401/GUS detection, PC speaker, IRQ timing,
and related sound/video probes. Originally authored for doskutsu's
real-hardware QA campaign, carried here because the underlying hardware
questions they answer ("does this card's DAC accept 8-bit palette writes",
"what's this IRQ's actual rate on this CPU") are not Cave-Story-specific.

Reach for these when real-hardware behavior is unexplained by your port's
own code — they isolate whether a bug is in your port or in how a specific
card responds. See `.sdl-dos-ports/docs/testing.md` in a port repo.

## Most probes are fully standalone

Most files here build and run independently of any game engine or SDL
build — they talk to hardware directly (I/O ports, DPMI, DMA) or through
DJGPP's own runtime only.

## A few require project-specific instrumentation hooks

`audbuf.c` and `sdlprob1.c` (and a couple of others that reference the same
symbols) `extern` a small set of instrumentation counters —
`dos_port_audio_irq_count`, `dos_port_audio_irq_wall_us`,
`dos_port_audio_sfx_active_count` — that doskutsu's own patched SDL/engine
exports for exactly this purpose. These probes will not link against a
build that doesn't export those symbols under those names.

If your port wants to reuse these two probes, either:
- add equivalent instrumentation to your own SDL/engine patches exporting
  counters under the same names, or
- treat them as reference implementations to adapt rather than build as-is.

Everything else in this directory does not have this dependency.

## `opl3midi.c` needs a real compiled SDL3-DOS, not bare metal

Unlike the rest of this directory, `opl3midi.c` (the `shared/audio/
opl3_sink.{c,h}` validation probe) calls into the real SDL3-DOS OPL3
register primitives (`SDL_dos_audio_synth.h`, SDL patch 0037) rather than
talking to hardware directly -- it needs to link against an actual
`libSDL3.a` built with that patch applied (any port's own
`build/sysroot/lib/libSDL3.a` works; this hub does not vendor/build SDL
itself). See the build recipe in the probe's own header comment. It also
needs `oplmode = opl3` (or `auto`) in whatever DOSBox-X conf runs it --
this hub's own shared confs default `oplmode = none`, so a plain
`dosbox-run.sh` run will report "no chip detected" (harmlessly; every
`midi_sched` parse/dispatch witness the probe reports is unaffected) --
use a scratch copy of the conf with that one setting changed to actually
exercise the sink's register I/O.

## `wbsinkmd.c` needs a real compiled SDL3-DOS, not bare metal

Same requirement as `opl3midi.c` above, but for `shared/audio/
waveblaster_sink.{c,h}` (SDL patches 0037/0047/0080-0101, the MPU-401
primitives) instead of the OPL3 ones. Unlike `opl3midi.c`, its own `.bat`
sets `SDL_HINT_DOS_AUDIO_PROBE_MPU401=1` for that one run -- see
`wbsinkmd.c`'s own header for why this is the only place that env var
should ever be set (real-hardware lockup risk on at least one card;
DOSBox-X-only diagnostic use). This is a genuinely different probe from
the older `wbmidi.c`/`wbtest*.c` files in this same directory, which
predate `midi_sched`/`waveblaster_sink` entirely and talk to MPU-401 ports
directly with no SDL/shared-layer involvement -- kept as-is, not
superseded, since they answer a different (bare-metal hardware forensics)
question.

## Naming

Probe-owned environment variables have been renamed from doskutsu's
originals to a neutral prefix (e.g. `DOS_PORT_LOG_TAG` -> `DOS_PORT_LOG_TAG`)
since these are read by the probe's own code and are safe to rename freely.
Comments referencing `SDL_HINT_DOS_*` hints still use that name because
they document behavior of doskutsu's *current* (not-yet-renamed) SDL patch
set — see `docs/patch-conventions.md`'s neutral-hint-naming convention for
what these should become once `shared/patches/sdl3-dos/` completes its own
rename pass, and update the corresponding probe comment at that time.

## Pruning

This library was carried over close to as-is from doskutsu's QA campaign
and includes some wave-specific one-off probes alongside genuinely
general-purpose ones. It has not yet been pruned or reorganized by
general-purpose-vs-one-off — treat file names and header comments as the
guide to what a given probe actually does before relying on it.
