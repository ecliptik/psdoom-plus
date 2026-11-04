# gusdmawedge -- GF1 DMA-upload-vs-active-voice wedge probe

Standalone DJGPP DOS probe (pure C, no SDL, no engine) answering one specific
question left open by the sdl-dos-ports S5 GUS PCM-streaming research
(dosags `PLAN.md`'s "S5 GUS architecture" section, 2026-09-13): **does a GF1
DMA-based sample upload wedge the real PicoGUS when a DIFFERENT voice is
already actively playing?**

Sibling of `gustone.c`/`gusdet.c`/`gusdump.c` -- same style, same safety
discipline described in `GUS-PROBES.md`.

## Why this exists

doskutsu's own real-hardware GUS campaign (see that repo's own
`memory/gus_campaign_picogus_diag_wedge.md` session memory) found the real
PicoGUS WEDGES (hangs, needs a physical power cycle) when a GF1 port-READ is
concurrent with an active voice -- confirmed 4 separate ways, all involving
PIO-based reads/pokes. The shared SDL3-DOS GF1 driver
(`vendor/SDL/src/audio/dos/SDL_dosaudio_gus.c`) even documents this on its
own one-shot test-tone comment: a forever-looping voice "keeps reading DRAM
and contends with the engine's gameplay `.pat` PIO poke -> wedge on the
PicoGUS."

That driver also has an **opt-in** DMA-upload path (`gus_upload_dma()`,
`SDL_HINT_DOS_GUS_DMA_UPLOAD=1`, default OFF, comment: "Promote to default
once g2k-validated" -- that validation never happened before this probe).
Its own completion-wait loop polls the host 8237 status port first, but
falls through to a GF1 port-READ (`gus_r8(GUS_REG_DMA_CTRL)`) if the 8237
hasn't signalled yet -- i.e. it can still touch a GF1 register while the
transfer (and any other active voice) is in flight. Whether that shares the
PIO wedge risk was unknown before this probe existed.

**Real-hardware result, HW-486-66, 2026-09-13 (`DMATEST=1 REPEAT=3`):
confirmed safe.** All 3 attempts returned `poll_ok=1`, the post-hoc DRAM
verify showed 0/64 mismatches, the machine returned to a normal prompt. One
clean pass at one configuration (single voice, 4096-byte chunk, 3 reps) --
not proof for every condition, but real evidence from the actual
failure-prone hardware. Full log: `GUSDMAWE.LOG` (8.3-truncated on the DOS
side), reproduced in dosags `PLAN.md`'s "Real-hardware finding" section.

That result only covers uploading to a FRESHLY bump-allocated address,
though (`SDL_DOSGusUploadSample`'s only mode). Building `gus_pcm_stream` on
it surfaced a second gap: that allocator has no reuse primitive, so a
double-buffered stream that refills the same two half-buffers forever would
permanently consume new DRAM on every refill. The fix is a new export,
`SDL_DOSGusOverwriteSample()` (sdl3-dos patch `0139`) that writes to a
CALLER-CHOSEN address instead of bump-allocating one, reusing the exact same
transfer mechanics `gus_upload_dma()`/`gus_upload_pio()` already use. It
should inherit the DMATEST finding by construction (same transfer, different
destination-address source) -- but that is an inference, not yet a
measurement. `OVERWRITETEST` is the confirming extension.

## What it does

- **SAFE mode (default)**: bring up the GF1 (same bounded, proven-safe
  sequence `gustone.c` uses), upload + start a **looping** voice-0 tone,
  play it, stop it, exit. Exercises everything except the risky operations --
  validates the harness itself with zero hang risk.
- **`DMATEST=1`** (**real-hardware-CONFIRMED-safe as of 2026-09-13, see
  above** -- still real-hardware-only, still needs an explicit go-ahead
  before re-running): once voice 0 is confirmed looping, attempts a chunked
  DMA upload of a second sample to a FRESH DRAM region, mirroring
  `gus_upload_dma()`'s own register sequence byte-for-byte (8237 program ->
  GF1 `DMA_ADDR`/`DMA_CTRL` -> poll 8237 status THEN GF1 `DMA_CTRL` bit
  `0x40`) while voice 0 keeps sounding.
- **`OVERWRITETEST=1`** (the NEW experiment, **real-hardware-only, explicit
  opt-in, real hang risk, NOT YET RUN on real hardware**): writes a second
  region ("region B") once during bring-up, before voice 0 starts -- the same
  safe pre-voice window DMATEST's own tone upload uses -- establishing it as
  an address already written to earlier in the run. Once voice 0 is looping,
  OVERWRITES region B (same DMA register sequence, different destination)
  REPEAT times while voice 0 (which never reads region B) keeps sounding.
  This is the actual usage shape `SDL_DOSGusOverwriteSample()`/
  `gus_pcm_stream` need: one region plays, the other gets refilled in place.

## The log lines to watch

```
gusdmawedge: ABOUT TO ATTEMPT DMA UPLOAD WHILE VOICE 0 IS ACTIVE ...
gusdmawedge: ABOUT TO OVERWRITE REGION B ... WHILE VOICE 0 IS ACTIVE ...
```

- **Wedge**: whichever of these is attempted LAST is the LAST line in
  `GUSDMAWEDGE.LOG` (and on-screen). The machine hangs; no further output, no
  exit, needs a physical power cycle. For `OVERWRITETEST`, confirms
  overwrite-in-place does NOT inherit DMATEST's safety by construction after
  all -- do not build `gus_pcm_stream` on `SDL_DOSGusOverwriteSample` without
  a different mechanism.
- **Survived**: a line beginning `gusdmawedge: DMA upload attempt N/M
  RETURNED, poll_ok=...` (DMATEST) or `gusdmawedge: OVERWRITE attempt N/M
  RETURNED, poll_ok=...` (OVERWRITETEST) appears next. Refutes the
  immediate-hang hypothesis for that one attempt. Not proof for all
  conditions -- repeat with `REPEAT=` and vary chunk exposure before trusting
  it generally, per this campaign's "one clean pass is evidence, not proof"
  discipline.
- Either way, voice 0 is stopped and each tested region's DRAM is verified in
  a SAFE, bounded, no-active-voice read afterward (mirrors `GUSDUMP`'s own
  NOINIT-safety framing). For `OVERWRITETEST`, the verify checks against the
  LAST overwrite's own pattern (`0x3C ^ ...`), not the initial pre-voice fill
  (`0x5A ^ ...`) -- a survived-but-stale-data result (still reading the
  initial pattern) would mean the overwrite silently no-op'd rather than
  genuinely succeeding, a real, different failure mode worth distinguishing
  from a clean pass.

## Usage

```
GUSDMAWEDGE                            SAFE mode -- no risky attempt
GUSDMAWEDGE DMATEST=1                  DMA-to-fresh-address (confirmed safe 2026-09-13)
GUSDMAWEDGE DMATEST=1 REPEAT=3         repeat the concurrent-DMA attempt 3x
GUSDMAWEDGE OVERWRITETEST=1            overwrite-an-already-used-address (NOT yet run)
GUSDMAWEDGE OVERWRITETEST=1 REPEAT=3   repeat the overwrite attempt 3x
GUSDMAWEDGE DMATEST=1 OVERWRITETEST=1 REPEAT=3   both experiments in one run
```

Shared knobs (same convention as `gustone.c`): `V=<n>` (active voices,
default 14 = 44100Hz, avoids the unrelated 28-voice dead-zone), `HZ=<n>`
(tone pitch), `DUR=<ms>`, `BASE=<hex>`, `IRQ=<n>`, `DMA=<n>`, `LATCH=0|1`.

Operator setup (g2k, PicoGUS v2.0, jumpers IRQ7+DMA3), same as every other
GUS probe:
```
SET ULTRASND=240,3,3,7,7
pgusinit /mode gus
pgusinit /gusdma 12
```

**Neither `DMATEST=1` nor `OVERWRITETEST=1` may be run on real hardware
without the operator's own explicit, fresh go-ahead with the hang risk
stated plainly, each time** -- these are deliberately risky diagnostics, not
routine smoke tests, per this project's standing real-hardware-action rule.
`DMATEST=1`'s own prior go-ahead does not carry forward to `OVERWRITETEST=1`
-- it is a different operation against different code, never run on real
hardware before.

## Build

```
i586-pc-msdosdjgpp-gcc -O2 -Wall gusdmawedge.c -o GUSDMAWEDGE.EXE
```

Builds clean, zero warnings (DJGPP cross-compiler, confirmed this session).

## DOSBox-X validation status -- INCONCLUSIVE, environment issue found (not a probe defect)

DOSBox-X cannot prove or disprove the wedge itself (no real PicoGUS firmware
quirk to reproduce -- see `dosbox_not_proxy`); it can only confirm the probe
builds, runs, and its log format is sane before it ever touches real
silicon. That check did **not** complete in this environment:

- A `[gus]`-enabled DOSBox-X conf was constructed (gus=true, gusbase=240,
  gusirq=7, gusdma=3) since neither `dosbox-x.conf` nor `dosbox-x-fast.conf`
  enables GUS emulation by default.
- Running `GUSDMAWEDGE.EXE` (any mode) via `dosbox-run.sh` produced no
  `STDOUT.TXT` at all -- the DPMI client crashes before any output.
- **Isolated to `disable()`/`enable()` themselves, not to anything in this
  probe**: a trivial 5-line control program that only calls `disable()`/
  `enable()` around two `printf`s crashes identically. The pre-existing,
  previously-proven `gustone.c` (unmodified, this session's own rebuild)
  crashes the same way under the exact same harness. Tried both
  `dosbox-x.conf` and `dosbox-x-fast.conf` -- same result either way.
- Conclusion: this is a pre-existing DJGPP-toolchain / CWSDPMI / DOSBox-X
  interaction issue in this environment affecting `disable()`/`enable()`
  (likely a DPMI privilege-level mismatch for the CLI/STI instructions
  CWSDPMI needs to grant), **not a defect in `gusdmawedge.c`** -- it affects
  the entire existing `gustone`/`gusdet`/`gusdump` probe family equally, none
  of which could be re-validated under DOSBox-X in this environment either.
  Out of scope to fix here; flagging for whoever next touches DOSBox-X
  validation of this probe family. The build itself (the only thing fully
  within this task's scope) is clean.

Confirmed recurring for the `OVERWRITETEST` extension too (2026-09-13,
`dosbox-run.sh --exe GUSDMAWEDGE.EXE`, SAFE mode -- "no STDOUT.TXT produced")
-- same pre-existing environment issue, not re-investigated further per the
above.

## Exact command to run on the rig (once the operator gives a fresh go-ahead for the hang risk)

```
GUSDMAWEDGE.EXE OVERWRITETEST=1 REPEAT=3
```

(`DMATEST=1` already ran clean on 2026-09-13 -- see above; re-run it only if
revisiting that finding specifically. `OVERWRITETEST=1` is the untested one
`gus_pcm_stream` actually depends on.) Then read `GUSDMAWEDGE.LOG` (or, if
the machine hung, note that the last line on disk is the "ABOUT TO
OVERWRITE..." announcement and the machine needs a physical power cycle to
recover).
