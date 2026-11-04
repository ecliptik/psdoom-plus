# Audio

## Hardware backends already proven (doskutsu, inherited via `shared/`)

- Sound Blaster 16 compatible PCM (DMA).
- OPL2 / OPL3 FM synthesis.
- MPU-401 / General MIDI / WaveBlaster.
- Gravis UltraSound / PicoGUS.

`shared/audio/midi_sched.{c,h}` is the canonical SMF (Standard MIDI File)
parser + tick scheduler — backend-agnostic via a sink callback struct
(note on/off, CC, program change). Do not write a second MIDI scheduler
per port. The per-device hardware backends themselves (OPL2/OPL3, GUS)
are not separate `shared/audio/` files today — the actual device-specific
logic lives in `shared/patches/sdl3-dos/` (e.g. `0112`-`0114` for native
GUS/GF1 support) as part of the SDL3-DOS backend, not as standalone
engine-side backend source a port links against directly.

## Decode cost vs. render cost

Music decoding can be more expensive than game rendering on a 486. Plan for
multiple tiers rather than one fixed audio pipeline:

```
Pentium-class:  decoded digital soundtrack (e.g. OGG/Vorbis if affordable)
486:            lower-rate preconverted audio, OR MIDI arrangement,
                OR an optional no-music performance mode
```

Independent confirmation from an unrelated engine on the same platform:
[DevilutionX's own DOS build notes](https://github.com/diasurgical/DevilutionX/blob/master/docs/building.md)
record that real-time MP3 decoding drops their game to ~4.5fps on DOS
(their `dr_mp3` decoder compiles and runs, it's just too slow), and their
documented fix is the same shape as above — ship WAV-only audio in the
DOS build's data files rather than attempting compressed-audio decode at
all.

Hardware MIDI is the highest-leverage option on slow hardware: moving music
playback to an external synthesizer removes essentially all CPU cost from
the host. If a game supports MIDI at all, hardware MIDI should be the
recommended 486 configuration.

### First plays: a load, not just a decode (dosags, 2026-09-26)

A game that plays short WAV effects from its package pays for each clip
once: the first play opens the package member, reads it (through DJGPP's
16 KB transfer buffer, so a 215 KB clip is about 14 real-mode `INT 21h`
reads) and decodes it into the port's PCM cache; later plays are cache
hits and cost next to nothing. On dosags' 16-bpp gate fixture (486DX2-66,
CF card) one walk click cost 201 ms with sound off and 505 ms with sound
on, and a sound-on timing probe put the first play of the walk's
13 KB footstep clip on that exact tick; DOSBox-X at fixed cycles
measures the same first play at 22 ms (open 4, read 7, decode 5), so the
486 multiplier is large and the read/decode split on real hardware is
still being measured (HYPOTHESIS until a 486 split is recorded).

Two levers, and they are not equivalent:

- **Make the first play cheaper** (fewer, larger reads; a cheaper first
  decode). This shrinks the cost and is the one to try first.
- **Warm the cache ahead of time** (decode a room's frame-linked sounds
  at room entry). This is exact and a real player-facing gain, but it
  only moves the cost into the room load. If the fixture's room load
  runs before the measured window (a `--startr`-style direct start), the
  gate figure improves because the cost left the window, not because it
  went away: report the moved cost beside the gain (see
  `docs/harness-lessons.md`, "The warm-up that moved the stall out of
  the window").

The profiler cannot answer this with sound on if it hangs with an audio
device open (dosags' did), so name first-play costs with a bracket probe
around play/load/decode instead.

## Phasing a new port's audio work

1. **PCM only** — WAV/SFX via Sound Blaster, no compressed audio.
2. **MIDI** — OPL3, MPU-401/WaveBlaster, GUS/PicoGUS via `shared/audio/`.
3. **Compressed audio** (OGG/Vorbis, etc.) only if profiling shows the CPU
   budget allows it; provide a lower-rate transcoded fallback for slow CPUs.
4. Additional formats only when a specific high-value game requires them.

Never let compressed-audio feature completeness degrade 486 performance —
it should always be an opt-in tier, not the default path.

## Audio vitals: check that the Sound Blaster is still alive

A lost Sound Blaster IRQ is fatal for the rest of the run, and every
per-IRQ counter misses it. If one SB interrupt is lost -- for example a
nested entry that DJGPP's interrupt wrapper hands to the previous handler
or drops (see `docs/architecture.md`, "An ISR that re-enables interrupts
must mask its own IRQ first") -- the DSP never gets its acknowledgement
and never interrupts again. Audio dies silently, and the game still runs
to a normal exit. `silent_irqs`, `underruns` and friends all stay at 0,
because they count only IRQs that happen. (dosags lab evidence:
LAB-JOURNEYS DLY-v1b, lab/journeys e4156cf9.)

So every sound-on cell also checks that the SB was alive for the whole
run. Two parts, proven on dosags' lab ratchet (77241735):

1. **The song clock keeps pace with the wall clock.** A MIDI song clock
   that only SB IRQs advance, compared with real seconds from the RTC,
   must read >= 0.90. Healthy runs read 0.956-0.983; the dead run read
   0.213. A clock advanced by anything else (uclock, the main loop)
   cannot see the death, so it has to be the IRQ-driven one.
2. **The IRQs that played audio are about as many as usual.** pcm_irqs
   (the IRQs that played ring data) must be >= 0.9 x that fixture's own
   baseline. Don't derive the expectation from run time / IRQ period:
   pcm_irqs counts only ring-playing IRQs, not every IRQ that should
   have fired, so it depends on how much sound the fixture plays.

Keep a stored dead-run manifest as a fixture for the check's control: fed
through the same parser, it must fail both parts (see `shared/tools/
ratchet.sh`'s `control:`).

**What a port needs to emit.** The hub's `shared/include/runmanifest.h`
(schema v1) has no field for either value:
- `audio_vital_status` is a single port-defined string;
- there is no song-clock or IRQ-count field.
dosags packs both into that string: the IRQ-driven song clock against the
RTC as `clk=<song ms>/<RTC s>s`, and `pcm_irqs=<n>`. A port adopting the
check emits the same two values, inside `audio_vital_status` or as extra
`key=value` lines in the manifest block (which the schema allows). The
checker then computes clk_ms / 1000 / rtc_s. This is a recorded GAP in
the shared schema, not yet a field of it.

## Real-hardware audio hazards (from doskutsu's own campaign)

Sourced from a long real-hardware campaign across SB16/Vibra16,
WaveBlaster/MIDI, AdLib OPL2, OPL3, and GUS/PicoGUS — cite `dosskutsu` for
exact patch numbers/code locations if a claim below needs deeper
verification than what's noted here. Organized architecture-wide first
(relevant to any port using this shared backend), then per-device.

### Architecture-wide

- **Cooperative-scheduler audio starvation is a real, hard floor, not a
  bug to chase.** Below roughly 25 fps wall-clock on this platform, the
  audio thread doesn't get enough yield-slices to keep software synthesis
  ahead of DMA consumption — music pitch drops and tempo slows (synthesis
  effectively running at half-rate), SFX gets scratchy from buffer
  underruns. Confirmed empirically at ~21fps wall-clock producing audibly
  broken music on every test cell. Any port doing non-trivial software
  synthesis inside the audio callback needs to know this is an
  architectural ceiling of the cooperative-scheduler model on this
  platform, not something a code fix removes.
- **Upstream's own numbers, and a real mitigation for anyone below
  them.** SDL's own DOS backend documentation (upstream
  [README-dos](https://wiki.libsdl.org/SDL3/README-dos), not a `shared/`
  addition) states the ring buffer holds ~45ms of audio and that "games
  running 22 fps or above require no intervention; below 20 fps, adding
  `SDL_Delay(0)` mid-loop helps maintain buffer levels" — matching
  `shared/patches/sdl3-dos/0023`'s own sizing comment ("4 chunks is
  ~45ms at 44100Hz, enough headroom for 22fps frame times") and
  consistent with the ~21-25fps empirical floor above. **Any port whose
  target frame rate sits below ~20-22fps by design, not as a bug, should
  add an `SDL_Delay(0)` call inside its main loop as a matter of course**
  — this costs nothing on a fast machine (it's a yield, not a sleep) and
  is upstream's own documented answer to exactly this class of port.
- **A port whose frame limiter never calls `SDL_Delay` anywhere gets
  total, permanent silence — not degraded audio, not underruns, zero
  `PlayDevice`/`WaitDevice` activity ever, regardless of what any audio
  callback computes.** This is a stronger and more absolute trap than the
  buffer-headroom guidance above: it's not about *how much* yield time the
  audio pump gets, it's that the pump this backend's DOS audio devices use
  is scheduled cooperatively at `SDL_Delay` call sites specifically (see
  `SDL_dosaudio_sb.c`'s own `DOS_Yield` commentary) — a frame-pacing sleep
  that bypasses `SDL_Delay` (e.g. an engine's own portable sleep
  abstraction backed by a bare `usleep()` on its DOS target, as opposed to
  routing through SDL) never reaches that scheduling point at all, so the
  pump never runs, ever. Confirmed on dossage/Passage: minorGems'
  `Thread::staticSleep()` on its DOS backend
  (`ThreadDOS.cpp`) called plain `usleep()`, never touching SDL, so
  `SDL_HINT_DOS_SILENCE_HISTO=1` showed zero `PlayDevice` emits across
  ~1200 expected calls over 28 real seconds despite `OpenDevice`
  succeeding cleanly — independent of two separate rewrites of the
  callback's own audio-generation logic, since neither rewrite was ever
  the problem. Fixed entirely in that port's own engine code (one call
  site, `Thread::staticSleep` → `SDL_Delay`); no `shared/` change was
  needed or appropriate, since routing a port's own sleep/frame-pacing
  primitive through SDL is squarely that port's responsibility, not
  something the shared backend can detect or fix from underneath an
  engine's own threading abstraction. **Any port whose engine has its own
  sleep/thread/frame-pacing primitive (a portability shim, a threading
  library, a ported engine's own `Sleep()`/`usleep()` wrapper) must audit
  that it ultimately calls `SDL_Delay` on this backend — not just a
  platform-native sleep — or audio can be silently, totally dead for the
  entire session with no error, warning, or partial-failure signal
  anywhere.** doskutsu never hit this because NXEngine-evo's own main loop
  already called `SDL_Delay` directly; the absence of a prior incident
  here reflects that its engine happened to route through SDL already, not
  that this hazard doesn't apply to it.
- **`SDL_Delay` yielding alone is necessary but not sufficient — a
  correctly-yielding port can still underrun on real hardware only,
  because DOSBox-X doesn't faithfully reproduce a single render-gap
  outlasting ring depth.** This backend exports `SDL_DOSAudioPump()`
  (`<SDL3/SDL_dosaudio_pump.h>`, `shared/patches/sdl3-dos/0067`+) for
  exactly this: an engine-callable, explicit mid-gap ring-service call —
  caller pattern `while (SDL_DOSAudioPump()) { }` — meant to be called at
  per-frame strategic points in the main loop, not relied on as a
  side-effect of yielding. It exists because doskutsu itself hit a
  real-hardware-only underrun *with correct `SDL_Delay` yield behavior
  already in place* — a single unusually-long render gap can outlast ring
  depth regardless of the average frame rate, a failure mode DOSBox-X's
  timing doesn't reproduce, so it won't show up in emulator-only testing.
  Confirmed as a real, additional fix on dossage/Passage: after the
  `SDL_Delay` fix above, DOSBox-X's own `PlayDevice` activity was still a
  mixed silent/noisy ratio; adding a per-frame `SDL_DOSAudioPump()` call
  to the main loop (dossage patch 0007) took it to fully non-silent
  (100/100 noisy in the observed window). **Any port doing non-trivial
  audio on this backend should call `SDL_DOSAudioPump()` from its main
  loop, not just call `SDL_Delay` somewhere and assume cooperative
  scheduling alone covers ring health** — `SDL_Delay` gets the pump
  *scheduled at all* (see the trap above); `SDL_DOSAudioPump()` is the
  belt-and-suspenders call that keeps it fed across gaps a DOSBox-X test
  pass won't catch. **Real-hardware confirmed on dossage/Passage.**
  Follow-up finding, though: an unconditional `while (SDL_DOSAudioPump())
  { }` called every frame at a sub-floor frame rate (Passage targets
  15fps, below the ~21-25fps floor two bullets up) hit the defense-in-depth
  32-iterate cap on ~99.5% of calls (424/426 in one measured run) — a
  near-max catch-up burst every single frame is real CPU cost, not just a
  diagnostic curiosity, on hardware already tight on frame budget. Fixed
  by pumping to a modest **target ring depth** each frame instead of
  looping until the ring is full or the cap trips — dossage's fix dropped
  the cap-trip rate to ~3% (24 total) with audio still confirmed audible
  (`vcctrl_audio_verdict` AUDIO_PRESENT throughout, same dB range) and no
  regression, frame rate steady. **Prefer a target-depth top-up over an
  unconditional run-to-cap loop** when calling `SDL_DOSAudioPump()` every
  frame, especially for a port targeting a frame rate at or below the
  cooperative-scheduler audio floor documented above.
- **The silence-detect throttle (`SDL_HINT_DOS_SILENCE_DETECT`,
  `shared/patches/sdl3-dos/0054` + `0056`) has a real fps cost whose shape
  depends on the workload, and whether its killswitch is safe depends on
  the port.** Default-ON, `PlayDevice` scans each mixed chunk for
  all-silence and, on silence, skips the ring write and sleeps
  `SDL_Delay(10)` as a throttle. Two ports have now measured it costing
  two different things. doskutsu's wave-35 A/B attributed a continuous
  +3.34 ms per flip to the scan on an always-busy mix (recorded in
  `0056`'s own commit message). dossage/Passage on a 486DX2-50 paid
  nothing steady-state, but during the quiet passages of its looping
  music track, where nothing gets written to the ring, every cooperative
  pacer yield paid the 10 ms sleep -- three ~20 s windows per life,
  aligned to the song's loop points, enough to fail a 14.90 fps KPI line
  by 0.08-0.18 fps until `SDL_HINT_DOS_SILENCE_DETECT=0` was set before
  `SDL_Init` (`docs/benchmarks/mach64-215ct-486dx2-50-round2-2026-09-04.md`
  and `round3` there, 2026-09-04). Treat the hint's cost as
  workload-shaped -- a small continuous tax on busy audio, a periodic
  larger one on audio with real silence in it -- and A/B it per port
  rather than quoting either number. **The killswitch is only safe for a
  port that does not depend on the silence-detect path for SFX
  correctness:** `0056`'s own message warns that under the OPL3/AdLib
  music backend the shared SfxSynth IRQ-mix never propagates without it,
  so doskutsu's SFX go silent with `=0`; Passage's audio is a plain
  sample stream with no SfxSynth, which is why `=0` was a clean fix
  there. Check which of those your port is before copying the fix.
- **The same 10 ms throttle used to sleep the app's own pump -- and
  removing that bought far less than it looked.** Until `0146`, a port
  that calls `SDL_DOSAudioPump()` from its game loop slept that loop 10 ms
  on every silent buffer. `0146` (from dosags SDL 0213) skips the sleep on
  the pump path only; the device thread keeps it. On a 486, dosags saw the
  pump fall from 8.1 ms to 0.4 ms, but the tick rate moved only 39.000 ->
  39.135. The sleep had mostly fallen in idle time. Take `0146` for the
  pump path's correctness, and measure your own KPI before counting on
  it. See `docs/optimization.md`'s worked example ("D2").
- **A worse variant of the above can hang indefinitely, and it's
  CPU-tier-specific.** If the audio thread is in-band/non-silent and never
  yields back while the main thread parks at a per-flip yield, the ring
  can starve *without* ever hitting ring-full — so a silence-throttle
  safety net that only fires on ring-full never fires, and the port hangs
  forever. Confirmed to reproduce on a 486DX2-66 but not a faster Pentium
  OverDrive on the identical board — pure timing-window sensitivity.
  **Test any cooperative-scheduler audio path on the slowest target CPU,
  not just the fastest/dev machine** — a hang that never reproduces on a
  fast box can still ship broken to slow ones.
- **A PIT-driven audio pump can silently corrupt SDL's own clock.** DJGPP's
  `uclock()` — the only timebase behind `SDL_GetTicks`/`SDL_Delay`/
  `SDL_GetPerformanceCounter` on this backend — reads PIT channel 0. A
  music pump that reprograms PIT ch0 for its own timing (to drive playback
  without relying on a DMA-IRQ callback) can freeze SDL's clock for up to
  ~55ms at a time, then jump — corrupting any performance measurement
  taken while the pump is active. This produced a false "GUS/AdLib costs
  42% fps" finding that was purely a broken ruler; the real numbers,
  recovered from flip-counts against wall-clock `time()` instead, showed
  no such gap. See `shared/patches/sdl3-dos/0122` for the pump-aware SDL
  timebase mitigation already in the shared backend. **If a port builds
  its own PIT-driven pump: either use a channel DJGPP's timebase doesn't
  read, or explicitly document that `SDL_GetTicks`-family calls are
  untrustworthy while it's active and measure via frame/flip counts
  instead** — this is the same "your measurement instrument can lie"
  class of hazard `dos-hardware-validation` warns about for rig
  configuration; it applies to in-process timing too.
- **DJGPP format-string argument-count mismatches are a hard terminate,
  not a silent misprint.** A logging call with 5 `{}` placeholders and 4
  arguments threw an uncaught `fmt::format_error` -> `std::terminate` ->
  instant quit-to-DOS, at exactly the moment a GUS SFX-upload loop
  completed — this looked exactly like a hardware/DRAM-exhaustion crash
  for multiple diagnostic iterations before someone actually counted the
  placeholders. Audit format-string argument counts specifically on
  DJGPP; a glibc/modern-platform build of the same bug would have just
  misprinted, not crashed, so this class of bug can survive host-side
  testing and only surface as a "hardware crash" on the DOS target.
- **MIDI dispatched from the main loop is only as smooth as the frame
  rate** (proven on real hardware, dosags 2026-09-23). A scheduler ticked
  once per game-loop iteration plays every event due by the wall clock,
  all at once. When rendering is slow, the fixed-timestep catch-up ticks
  run back to back, so events go out in one batch per rendered frame. On
  a 486DX2-66 at 640x480, a 6 fps intro sent notes in clumps about
  157 ms apart: 465 of 483 lateness gaps were 150-200 ms, and 72% of
  events were more than 50 ms late. The average tempo was still right,
  within 1% against the RTC. The operator heard it as music that
  "speeds up randomly". Main-loop stalls add bursts on top. Tick MIDI
  from something steady, such as the SB IRQ hook (`SDL_DOSMidiTickRegister`,
  `0072`, which doskutsu uses) or a timer that doesn't disturb `uclock()`
  (see the PIT hazard above). Measure it with a lateness field (due time
  against dispatch time), not by ear.
- **No graceful silent-degrade by default.** With no usable sound device,
  `SDL_INIT_AUDIO` failing is fatal by default — the process exits clean
  to DOS rather than continuing without sound. An "audio device absent,
  run silent" path needs an explicit opt-out a port wires itself; it
  doesn't fall out of SDL failing gracefully on its own.

### GUS / PicoGUS

- **PicoGUS's "USB joystick support enabled" firmware feature can fool
  joystick-detection code, not just audio.** See `docs/input.md`'s
  gameport joystick hazards section -- this is a firmware-level quirk of
  the card, filed under audio here only because that's where PicoGUS
  facts are tracked, not because it's audio-specific.
- **GF1 DRAM is hard-capped at 1 MB** on both real GUS hardware and
  PicoGUS (a 20-bit address space, not a driver limit). A port doing both
  streamed SFX and GM-instrument wavetable music needs an explicit budget
  split between an SFX sample bank and per-song instrument residency —
  `shared/patches/sdl3-dos/0114` partitions the voice pool this way
  (`SDL_HINT_DOS_GUS_SFX_VOICES`, default 4, reserved as the *top* slice so
  an SFX burst can never claim a voice the music mixer already owns,
  clamped to <=1/4 of total voices at device open). This is a real,
  audible trade-off (richer music vs. a few late SFX getting silently
  budget-skipped), not something to tune away.
- **PicoGUS firmware has an exactly-28-voice special case.** Its
  `SCALE_22K_TO_44K` behavior (default-on) forces 28-voice GUS output to
  actual 44.1kHz even when a driver's own rate math assumed 22050Hz at
  that voice count — so 28 voices specifically plays at the wrong internal
  rate/pitch, while 27 or 29 voices work fine. Undiscoverable without
  reading the PicoGUS firmware source directly; if a port's GUS
  voice-count/rate math is naive, avoid the literal number 28 as a
  default.
- **GF1/PicoGUS diagnostic port reads wedge the card.** Any GF1
  register peek/status/readback issued purely as a diagnostic — even from
  main-thread context with no voice active — reliably hangs a PicoGUS,
  confirmed across several independent attempts (a looping test-tone
  during a poke, a DPMI-IRQ-context readback, a main-context readback,
  even heavy log-only instrumentation during song-load). **Never add a
  GF1 port READ to a PicoGUS diagnostic.** Validate the command stream in
  DOSBox-X instead — it's software-determined, identical to real hardware
  for that purpose, and the emulator can't wedge.
- **Large GM patch sets are a trap on a 1MB card.** EAWPATS (built for
  RAM-rich softsynths) has individual patches over 1MB each — a song that
  plays 5-8 instruments under a compact set (Pro Patches Lite) dropped to
  2-3 under EAWPATS, with the rest silently failing DRAM upload. Prefer a
  compact/complete GM patch set for real GUS hardware over the
  biggest-sounding one.
- **MIDI content with out-of-range drum notes silently vanishes** if a
  drum-name/patch lookup table only spans the standard GM percussion range
  (27-87) and the source MIDI uses non-standard channel-10 notes outside
  it — lookup returns null and the note is silently dropped with no log
  and no fallback. Clamp out-of-range drum notes to the nearest defined GM
  percussion instead of dropping them.

### SoundBlaster / SB16

- **DSP-version-based 16-bit-DMA commitment breaks SB-compatible-but-not-
  really cards.** Committing to 16-bit DMA (and hard-erroring on a missing
  high-DMA `H` `BLASTER` field) whenever the reported DSP version is >=4
  breaks PicoGUS-in-SB-mode, which reports a DSP v4-class version while
  only actually supporting 8-bit DMA — see
  `shared/patches/sdl3-dos/0106` for the runtime fallback (force 8-bit
  rather than hard-failing `OpenDevice` on a missing `H`).
- **`BLASTER` env values must match the card's physical jumpers, not
  firmware config.** A firmware config utility tells the card's own
  firmware what to expect, but does not move physical IRQ/DMA lines. The
  mismatch symptom is nasty: DSP detects fine, `OpenDevice` succeeds, but
  zero IRQs ever fire — looks exactly like a driver bug, is actually a
  config/hardware mismatch. `BLASTER` I/D/H values are load-bearing
  hardware facts, not preferences to tune.
- **The silence gate shortens sounds that contain silence** (proven on
  real hardware, dosags 2026-09-22/23). `0054`'s "Shape A" check drops
  every mixed chunk that is all silence in the *device* format, and it
  doesn't know whether a sound is still playing. A clip's own silent
  stretches were dropped, so each effect came out short: 9.0 s instead of
  9.3 s, and 3.1 s instead of 3.5 s, on a PicoGUS. The card also idled
  through those stretches, which shows up as bursts of silent IRQs. On
  the 8-bit path it also drops quiet audio: anything under about -48 dBFS
  converts to all 0x80, which the check reads as silence. The fix is
  opt-in: `0143`'s `SDL_DOSAudioSetContentActive(bool)`, which the port
  holds true while a sound is still owed to the device, so silent chunks
  are written instead of skipped. A port that never calls it keeps the
  old behaviour. A port whose sounds contain silence should call it. The
  witness is chunk accounting: IRQs that played data must equal the
  chunks the played clips need. It came out exact on hardware with the
  fix (1432/1432, 1554/1554).
- **`SDL_DOSAudioPump()` could write over audio the card hadn't played
  yet.** The pump iterates without checking for room, and the background
  thread's `WaitDevice` checks for room without holding the lock. Either
  writer could lap the ISR's read cursor, so a clip was consumed at
  mixing speed. `0142` moves the room check into `GetDeviceBuf`, which
  both writers pass through under the lock. `0141`'s overwrite counter is
  the witness. It read 38 and 19 overwrites on DOSBox-X before the fix, 0
  after, and 0 on real hardware since.
- **"`SDL/0063` WaitDevice ring-drain timeout -- IRQ-5 wedged" can be
  false.** It also fires when the main thread's pump keeps the ring full
  for more than a second, with IRQs firing normally (hub probe for
  `0142`). Before `0142` that timeout was a third way to write into a
  full ring. Don't read the message as proof the IRQ stopped; check the
  IRQ counters.

### WaveBlaster / MIDI daughterboard

- **WB's MIDI clock rides the SB IRQ.** If that IRQ never fires (see the
  jumper-mismatch trap above), the MIDI scheduler tick never advances —
  but init/transport writes still leak out and can blink the
  daughterboard's MIDI-activity LED, which looks like partial success and
  can send debugging down the wrong layer.
- **WB loudness can't be fixed by GM Master Volume / CC7 boosting if the
  source MIDI already sits near the CC7 ceiling** — additional up-scaling
  just hits the same 127 ceiling and does nothing audible. Check the
  actual MIDI CC7 content before assuming a mixer/gain bug explains a
  quiet wavetable board.
- **The real loudness lever on a real card is the analog Line-In/CD mixer
  input**, not any digital FM/music volume control — WB audio enters
  through the analog mixer path on real SB16 hardware, invisible to a
  purely digital gain stage.

### AdLib / OPL2 vs. OPL3

- **AdLib is OPL2, not "OPL3 minus the second bank."** Writing
  OPL3-bank-2 registers on real OPL2 silently does nothing; correct AdLib
  output needs a genuinely separate OPL2 code path, not a conditional
  inside the OPL3 path.
- **A pure AdLib card has no DSP/DMA at all — PCM SFX architecturally
  cannot play through it.** SFX on an AdLib-only setup needs an entirely
  different device (doskutsu uses PC-speaker square-wave beeps via PIT
  channel 2, gated off while the OPL PIT pump is active to avoid a channel
  conflict).
- **Drum/percussion selection can be indexed by track slot, not by the
  instrument program field**, depending on the source format — scanning
  the wrong field gives a plausible-looking but wrong picture of which
  drums are actually in use.
- **"Missing music" isn't always a playback bug — check the actual note
  timeline first.** One track opened with ~7.7s of channel-10 percussion
  before the first melodic note, in a scene that only lasted 6-8s total —
  every MIDI-rendered backend legitimately spent the whole scene on drums,
  and a shared single noise-burst envelope for every drum slot made that
  sound like near-silence. The fix was per-slot drum envelopes, needed in
  both the OPL3 and OPL2 code paths separately since AdLib has its own
  distinct drum-rendering logic.
