# Video

## Resolution hierarchy

```
320x200   best
320x240   excellent
320x400   acceptable
640x480   Pentium-oriented
higher    not an initial DOS target
```

doskutsu targets 320x240. A port whose native resolution is 320x200 (many
DOS-era and DOS-recreation games, e.g. OpenJazz) exercises the VESA path
slightly differently and is worth testing deliberately rather than assumed
identical.

## Presentation path

Preferred:

```
game software framebuffer
       |
 single conversion if necessary
       |
 VESA framebuffer
```

Avoid multiple software scaling stages. Prefer native resolution, no
filtering, integer pixels, direct blit, and dirty rectangles where the
game's redraw pattern benefits from them (a mostly-static-screen game like
an adventure engine benefits far more than a scrolling action game). If
your engine renders a small image into a larger surface (e.g. a centered
sub-image), **rect-limit every stage that touches the surface, not just
one** — a blit call and a present/window-update call are separate APIs
each needing their own rect argument; limiting only one still leaves the
other doing full-surface work every frame. See `optimization.md`'s
rect-limiting note for a real-hardware measurement of just how much this
costs when missed.

Upstream's own backend documentation
([README-dos](https://wiki.libsdl.org/SDL3/README-dos)) is explicit that
**tear-free rendering depends on hardware page-flipping being available,
and page-flipping is not universal** — it's an LFB-mode capability, not
something banked mode gets. A card forced into banked mode by capability
(no linear framebuffer at the mode in use — see the Mach64 case in
"Real hardware can diverge..." below) may not have a tear-free path
available at all under this backend's current design, independent of
whatever vsync-wait code exists in the banked path itself. This
reframes "why is there a stable tear on this card" from "something is
broken" to "check whether page-flipping was ever an option here" before
assuming a fixable bug.

Separately, `SDL_HINT_DOS_ALLOW_DIRECT_FRAMEBUFFER` (a genuine **upstream**
SDL3 hint, not something `shared/` introduced — confirmed present in
unpatched SDL3's own `SDL_dosframebuffer.c`) trades away vsync entirely
for speed: per its own documentation, setting it skips normal surface
copying and "performs page flipping without vblank synchronization,"
with "no vsync" and "potentially slow readback on real hardware" as its
stated, accepted trade-offs. If a port's video hint configuration touches
this one (directly or via `shared/patches/sdl3-dos/0010`'s
`SDL_HINT_DOS_PREFER_LFB` interaction with it), tearing is expected
behavior for that path, not a defect.

**Decide the tearing trade explicitly, by genre, and record the
decision** -- it is a judgment call the operator should make once rather
than a question each slice re-opens. dosags took this path by default
after measuring what it buys on a 486 (present cost 15.95 -> 3.44 ms on
one candidate, and the whole speed ordering between presentation modes
collapsing once the per-rect path was reachable), with the operator's
reasoning recorded as: "tearing is fine, these are adventure games" --
mostly-static screens, no scrolling, no camera motion, so a tear is
rarely visible and never mid-action. **A port with scrolling or fast
motion must not inherit that decision unexamined.** Note also that a
camera photo cannot clear tearing on its own: an exposure integrates
over roughly a CRT refresh, so a transient tear blurs rather than
freezes -- photos can show tearing is not gross or continuous, and
nothing stronger. Judge it in motion, or judge it by genre.

## No low-res mode on the card: present unscaled, never scale to fill

Some cards cannot produce 320x200/320x240 VESA modes at all (the ATI
Mach64 CT/ET cannot double-scan -- see "Real hardware can diverge..."
below and `HARDWARE.md`). The engine then lands in a larger mode, and the
tempting default -- scale the game frame up to fill it -- is the single
most expensive thing a port can do on a 486. Two ports have now measured
this on the same Mach64:

- **doskutsu** (320x240x8) first ran at **2.2 fps** on this card, and
  reached a **27.6 fps** baseline after presenting *unscaled*: the
  320x240 picture centered in the 640x480 surface, the flush scoped to
  the logical rect (`center-oversized: flush rect=320x240@160,120
  bytes=76800`), margins cleared once, writes going straight to VRAM.
  (`doskutsu/qa-results/2026-08-13-r2-mach64/`, `2026-08-21-mach64-tilemap/`.)
- **dosags** (AGS; Trilby's Notes, 320x240x8 to the engine) took the
  scale-to-fill default (AGS's "fullscreen desktop" = 640x480, 2x): every
  real present cost ~76 ms -- ~34 ms for the 2x scale blit plus ~42 ms to
  push 307,200 bytes -- against 76,800 bytes for the game's own frame,
  exactly doskutsu's frame size. Hand-optimizing the
  scaler (integer fast path, word stores) won real fps but only polishes
  a step that should not exist. (dosags `PLAN.md`, "40/40 plan".)

**FIRST: check what the card in the machine actually enumerates. This
whole section is about a card that CANNOT do low-res modes, which is the
worst case, not the normal one.** A card with a real 320x200/320x240
mode should simply SET it -- full screen, 64,000-76,800 bytes, no scale,
no centring, no margins, strictly better than every rung below.
doskutsu's own logs show exactly that on this rig's other cards
(`qa-results/2026-08-11-DX266-full/602SDL.LOG`,
`2026-08-13-r2-POD83/GE3SDL.LOG`: `0x01F9 320x200 bpp=8`,
`0x01F8 320x240 bpp=8`, plus 15/16/24/32-bit 320x240). **Select the
presentation mode per card from the live enumerated list, never once
globally** -- a default chosen on a Mach64 is wrong for a ViRGE.

**Candidate ladder when the card genuinely has no low-res mode, best
first. Status is stated per rung -- do not quote an unproven rung as
fact:**

1. **Classic VGA Mode 13h, for 320x200 8-bit games only.** Full-screen
   picture, 64,000 bytes per frame, no VESA involved (the shared backend
   always enumerates a synthetic Mode 13h entry). Reasoning: a vendor's
   "no double scanning" note is about the VESA/accelerator modes; Mode
   13h runs on the legacy VGA core, which does its own line doubling,
   and every VGA-compatible card must run it for ordinary DOS games.
   **STATUS: reasoning plus one piece of prior evidence -- doskutsu's
   SETUP.EXE mode-13h video bench was validated on this Mach64
   (`doskutsu/docs/internal/MACH64-TRIAGE.md`); from THIS backend it is
   confirmed only on DOSBox-X (dosags, 2026-09-10).** Verify it sets,
   displays correctly (use a camera, not only VGA capture -- see the
   stale-frame hazards in `dos-hardware-validation`), and restores the
   console on exit. **Check the engine's own reported native resolution
   before planning on this rung, not the game's catalogue entry:** dosags
   found that none of its fixtures qualifies -- Trilby's Notes is listed
   as 320x200 but AGS's compiled letterbox option makes it 320x240 to the
   engine (`Game native resolution: 320 x 240 (8 bit)
   letterbox-by-design` in every log), and its other 320x200 titles are
   16- or 32-bit. Mode 13h is 8-bit, 320x200 exactly; a 320x240 game is
   doskutsu's case and needs rung 2 or 3.
2. **Centered, unscaled, in the SMALLEST enumerated mode that contains
   the game rect.** On this Mach64's UniVBE list that is 512x384x8 (mode
   0x01F3; 0x01D4 at 16-bit). The flush is the same size as rung 3, but
   a 320-wide picture fills ~62% of the screen width instead of 50%.
   **STATUS: untested from dosags (2026-09-17), queued for its bake-off.
   doskutsu DID run here: its only clean comparison is 28.3/28.4 fps at
   512x384 (UniVBE, LFB) against 26.9/26.8 at 640x480 (card ROM, banked)
   -- at most +1.4 fps and confounded (provider, LFB and mode all changed
   together). Its operator retired 512x384 for a PRODUCT reason, not a
   technical one: the mode exists only in UniVBE's list, so shipping it
   would require every user to install that third-party TSR. A port
   whose target machine already runs UniVBE can use it; a port that must
   run on the bare card ROM cannot.**
   Known history to respect: doskutsu's 320x240 request once landed on a
   512x384 closest-match on this chip and exposed the stride-caching bug
   fixed in `shared/patches/sdl3-dos/0125`; and a closest-match result
   is a property of one card + BIOS + UniVBE config, not of the chip
   model (see below). Read the enumerated list per machine; pin the mode
   explicitly rather than trusting closest-match.
3. **Centered, unscaled, in 640x480.** doskutsu's exact shape on this
   card. **STATUS: proven on real hardware (doskutsu).** The known-good
   fallback; picture is 50% of screen width.

Rules that apply to every rung:

- **Scope the flush to the game's own rect** (rect-limit every stage --
  see "Presentation path" above and `optimization.md`) -- **and check
  that the backend path you are on honours the rect list at all.** In
  `SDL_dosframebuffer.c` the banked path and the
  `SDL_HINT_DOS_ALLOW_DIRECT_FRAMEBUFFER=1` path copy per rect; the
  NORMAL LFB path never reads `rects`/`numrects` and copies the whole
  surface every call (verified in dosags's vendored copy, 2026-09-17).
  On that path `SDL_UpdateWindowSurfaceRects` and dirty rectangles buy
  nothing. doskutsu sets the hint (plus `SDL_HINT_DOS_PREFER_LFB=1`);
  dosags ran with both at 0. Both can be set from the environment
  (`SDL_DOS_ALLOW_DIRECT_FRAMEBUFFER`, `SDL_DOS_PREFER_LFB`), so probe
  the effect with zero code first. Trade-offs are the documented ones
  above: no vsync, no SDL cursor compositing.
- **An unscaled, centred present can be SLOWER than scale-to-fill until
  the port's present code is adapted.** In dosags both the skip-unchanged
  check and the integer fast blit required the destination to be the
  whole window, and a bordered destination triggered a full-window fill
  on every present -- so centring alone loses two optimizations and gains
  a fill. Fix the present path first, then compare.
- **Clear the margins once, not per present.** dosags found AGS issuing
  a full-window black fill on every present of a letterboxed 640x360
  game, ~15 ms that no timing bracket covered. A game that is already
  1:1 letterboxed is halfway to rung 3 -- finish the job.
- **Keep the scaled path available as an option**, never as the default
  on the 486 tier.
- **Let the operator choose between rungs from measurements AND a
  picture** of how each one looks on the real monitor. Picture size is a
  taste call; bytes per frame is not.
- **Re-measure per card, and expect the mechanism to differ, not just
  the numbers.** The direct-framebuffer/rect-aware path is not the same
  code on every card: the Cirrus CL-GD5430 has a real LFB-aperture
  defect and `shared/patches/sdl3-dos/0019` auto-disables LFB on that
  chip, so it lands on the BANKED path -- which is per-rect aware, but
  carries two open hazards recorded in `HARDWARE.md` (the unfixed
  bank-select settle-delay/readback gap, and a reproduced
  multi-hundred-ms flush stall on that exact card). A presentation
  result is a fact about one card + BIOS + VBE provider, not about the
  port.
- **A dosags measurement of all four rungs on a Mach64 (2026-09-18,
  BENCH8 anim12, real hardware, 8-bit).** Keep the two present metrics
  apart -- conflating them cost this campaign a day chasing a
  "discrepancy" that was two different terms:

  | rung | `present_blit` | `present_update_window` | `fps_p50` |
  |---|---|---|---|
  | Mode 13h 1:1 | 4.22 | 7.84 | 40.00 |
  | 512x384 centred | 4.22 | 15.95 | 26.89 |
  | 640x480 centred 1x | 4.23 | 21.60 | 19.72 |
  | 640x480 2x scaled | 28.22 | 24.31 | 11.83 |

  **The blit is flat across the first three rungs and only explodes at
  2x** -- the only rung that replicates pixels. What separates the top
  three from each other is entirely `present_update_window`, tracking the
  WINDOW SURFACE size and not the picture: 64,000/196,608/307,200 bytes
  -> 7.84/15.95/21.60 ms, a straight line. That confirms on hardware that
  the normal LFB path copies the whole surface and ignores a scoped rect.
  **So the general rule is output resolution, and replication is a
  2x-only penalty stacked on top of it** -- an earlier reading of this
  same table named the horizontal-replication blit as *the* target, which
  is true of the current default and false as a general statement. Do not
  carry the blit framing to a card whose default rung differs.
- **The fps column above is render passes, not displayed frames**, and an
  earlier version of this entry drew a wrong conclusion from it: that the
  top three rungs all delivered the same ~13.5 genuinely-new frames/s and
  their gap was pure headroom. **Retracted.** The fixture's own animation
  delay sets a hard ceiling of 20 distinct frames/s, and against that
  ceiling the rungs differ sharply in how much they actually deliver --
  Mode 13h displayed 19.6/s (98% of the ceiling) where the full-window
  path managed 14.8/s (74%). The gap was not headroom; the slower path
  was dropping content. **A render-pass count cannot tell you which of
  those two situations you are in** -- see `docs/optimization.md` on
  labelling fps with real-versus-skipped presents, and pair every fps
  figure with a completeness ratio against the content's own frame rate.

**Measure the bus; do not infer its ceiling from your own present path.**
A dosags session concluded "~7 MB/s, so full-frame pushes can never beat
~12 fps on this hardware" from its own 307,200-bytes-in-41.9-ms figure.
doskutsu's Mach64 campaign had already measured this same card taking
76,800 bytes in 4.77 ms (~16 MB/s, on the Pentium OverDrive 83, through
the banked window -- and its own authors declined to call that a
bandwidth limit, "an argument from a numerical coincidence"). The
7.3 MB/s was a property of one code path on one CPU, not of the card.
The claim was retracted. Before any design or any "physically out of
reach" statement rests on a bandwidth number, run a bandwidth probe: a
plain dword copy of each frame size you care about into the LFB (and
into A000 under Mode 13h) on the actual target CPU.

## What `shared/patches/sdl3-dos/` already handles

The hard-won VESA/VGA chip-specific work — Cirrus CL-GD5430 banked-blit
quirks, S3 ViRGE linear-framebuffer handling, DAC 6-bit vs. 8-bit
palette-width detection, banked-vs-LFB flush path selection, direct-CRTC
page flip — lives in the shared SDL3 DOS backend patches. A new port should
not need to re-derive any of this; it inherits it by pinning the same
patched SDL3.

## Real hardware can diverge from DOSBox-X on window surface format and pitch

Two distinct hazard classes can produce the same visual symptom — stable
corruption/tearing that's clean under DOSBox-X and only shows up on real
hardware — for different reasons. Both are worth knowing before assuming
a hardware defect: one is a confirmed, closed root cause from doskutsu's
own history; the other is a real-hardware finding on dossage/Passage with
an architecturally-sound fix already applied, pending real-hardware
reconfirmation that it's the complete story.

**Window surface pixel format is not guaranteed, and DOSBox-X can hide a
wrong assumption about it.** SDL1.2's `SDL_SetVideoMode(w, h, bpp, flags)`
let a port force a specific bit depth. SDL3's `SDL_CreateWindow` has no bpp
parameter — the real window surface's pixel format tracks whatever VESA
mode SDL's closest-mode matching actually picked, which can differ between
DOSBox-X's software VESA BIOS and a real card's VESA BIOS for the
*identical* logical resolution request, and can differ card-to-card on
real hardware too. A port whose rendering writes raw pixels (`Uint32*` or
similar) assuming a specific format — the natural direct port of an
SDL1.2 engine that used to request 32bpp XRGB explicitly — can render
correctly under DOSBox-X and corrupt every pixel on real hardware, because
DOSBox-X happened to pick a mode compatible with the assumption and the
real card didn't. This is exactly the "single conversion if necessary"
presentation path already recommended above, but treat it as a
**correctness requirement, not just a performance option**: always render
into your own explicitly-created known-format surface (e.g.
`SDL_PIXELFORMAT_XRGB8888`) and blit-convert onto the real window surface
at present time, rather than writing raw pixels into the window surface
directly. (Found on dossage/Passage via a real-hardware Mach64 pass,
2026-08-26 — architecturally fixed in that port's own engine code as its
patch 0003. **Confirmed as the complete fix**: a direct pixel-diff of a
post-fix real-hardware capture against the DOSBox-X reference screenshot
came back at 2.85/255 mean difference, consistent with JPEG noise, not a
defect. The pre-fix saved frame was independently re-checked and confirmed
genuinely, severely broken — near-total fine-grained garbage across nearly
the whole frame — so this was a real bug with a real fix, not a
non-issue. No second video hazard was involved; see the triage note
below for how that got clarified.)

**A faster alternative to render-then-blit-convert: write directly in the
window surface's real, queried format.** The render-into-known-format-
then-blit-convert path above is safe, but the blit-conversion pass turned
out to be the single largest per-frame cost in dossage/Passage's later
performance investigation on the same real Mach64 hardware (~145-160ms/
frame before rect-limiting, still ~25-37ms/frame after — see
`optimization.md`'s rect-limiting note). The actual bug in the original
XRGB8888 finding was never "writing directly into the window surface" —
it was writing an *unverified, assumed* format. Querying the window
surface's real pixel format at runtime (`SDL_GetPixelFormatDetails`) and
packing each pixel directly into *that* format (`SDL_MapSurfaceRGB`),
writing straight into the window surface with no intermediate surface or
blit pass, is equally safe — because it's format-aware instead of
format-assuming — and eliminates the blit entirely. Confirmed against
SDL's own reference behavior before trusting it: the 3-bytes-per-pixel
byte-order convention was checked against `SDL_FillSurfaceRect3`
(`SDL_fillrect.c`) rather than assumed (little-endian, low-byte-first,
matching a plain memcpy of a `Uint32`'s low N bytes on x86). Real-hardware
confirmed clean via careful multi-checkpoint color scrutiny, no
corruption, and delivered a real ~38% flip-cost reduction on its own
(6.90 → 8.18fps). Fall back to the render+blit path above for any pixel
format this direct-write approach doesn't explicitly handle (non-2/3/4
bytes-per-pixel, e.g. indexed) — **this is an alternative for a port
that wants to avoid the blit cost, not a replacement recommendation**;
either approach is correctness-safe, so the choice is a performance
trade-off, not a correctness one, unlike the original raw-`Uint32`-write
bug that started this thread.

**Diff against the known-good reference before reaching for hardware
register diagnostics.** After the XRGB8888 fix above landed, a real-hardware
Mach64 retest still showed a banded, non-uniform region on the right side
of the screen. Read visually against expectations of "clean gameplay," this
looked like corruption, and it drove a genuinely careful five-patch
diagnostic chase at the shared SDL3-DOS layer — bank-switch chunk-content
and post-write readback (`SDL/0128`-`0131`), a legacy VGA Graphics
Controller register dump (`SDL/0132`), a raw CRTC offset register read
(`SDL/0133`), and a standardized VBE 4F06 Get Scan Line Length call
(`SDL/0134`) — all of which came back clean or internally consistent, ruling
out bank-switching, legacy VGA read/write-mode state, and stride/pitch
mismatches one by one with real evidence. The actual answer only surfaced
when the "corrupted" capture was pixel-diffed directly against this
project's own `docs/screenshots/dossage-gameplay.png` DOSBox-X reference
(2.85/255 mean difference — the banded region was Passage's *intended*
right-side visual content, not a defect at all). **Lesson for the next
real-hardware-only video symptom that looks wrong on sight: do the direct
pixel-diff against a known-good reference screenshot first**, before
spending register-level diagnostic effort — it's cheaper than a single one
of the probes above and would have closed this case immediately. This
doesn't make the probes wasted work: they're retained in `shared/` as
hint-gated (`SDL_HINT_DOS_BANK_CHUNK_PROBE`; zero cost when unset),
chip-agnostic diagnostic tooling precisely because ruling out those layers
cleanly is a real, reusable result for whatever the *next* investigation
turns out to be.

**A stale cached VRAM pitch across an in-place mode-set is a *closed*
hazard at the shared layer, not one a new port needs to defend against
itself.** `src/video/dos/SDL_dosframebuffer.c`'s `CreateWindowFramebuffer`
populates `fb_state` (cached pitch, pixel pointer, a `pitches_match` flag)
once; the flush routine (`DOSVESA_UpdateWindowFramebuffer`) trusted those
cached fields for the framebuffer's whole life, even across a *later*
mode-set that changed the hardware's real VRAM pitch on the same surface
pointer (no new `CreateWindowFramebuffer` call, so nothing re-triggered a
refresh). Confirmed on a Mach64 215CT: the card's own UniVBE-printed
banner states plainly that Mach64-CT/-ET silicon cannot double-scan, so
320x200/320x240 aren't available modes at all — doskutsu's engine
requesting 320x240 landed on a 512x384 closest-match at first mode-set,
then a later mode-set (reached with the native-mode pin disabled, to
actually measure at 640x480) moved the hardware to 640x480 in place while
the cached pitch stayed at 512, shearing every subsequent flush
progressively down the frame. Fixed generically in
`shared/patches/sdl3-dos/0125` (re-derives the flush's cached pitch from
the live surface whenever it no longer matches, at the cost of one
pointer deref plus one int compare per flush on the common no-op path) —
this lives at the framebuffer layer, not gated to Mach64, so any port
pinning the current shared patch series inherits the fix on every card,
not just the one it was found on. It only resurfaces as a live hazard if
a port's *own* engine code separately caches a pitch/stride value outside
of what SDL3-DOS itself tracks. (0125 fixes the shear/correctness only —
it does not make an undersized composed surface fill an oversized mode;
that's a separate, unrelated concern.)

**A closest-mode-match result is not a fixed fact for a given chip
model, and the mode-set log line reports the resolved mode, not the
app's raw request — both easy to misread.** doskutsu's own Mach64
215CT history above landed on a 512x384 closest-match for a 320x240
request. A later, independent real-hardware measurement on nominally
the same chip family (dossage/Passage, same "cannot do 320x200/240"
UniVBE-confirmed constraint) landed on 640x480 for the same logical
request instead — a different result on nominally the same hardware
class, most likely because a different enumerated-mode list (different
BIOS/VBE version, different configured mode table) changed which
mode SDL's closest-match algorithm considered "closest." **Treat one
measurement's closest-match result as a data point for that specific
config, not a universal fact about the chip model** — re-verify rather
than assuming a prior measurement still holds when hardware, BIOS
version, or UniVBE config differs even slightly.
Separately: `DOSVESA_SetDisplayMode` (`SDL_dosmodes.c`) logs the
`SDL_DisplayMode*` it resolved to *after* SDL's own closest-match
algorithm already ran — so a log line reading e.g. "requested
id=... 640x480" does **not** mean the app asked for 640x480, it means
closest-match landed there for whatever the app actually requested.
This tripped up real-hardware round-trips on dossage/Passage more than
once before being caught by reading the source instead of assuming the
log line's plain-English reading was literal. Read that log line as
"resolved mode," never as "requested mode," and check the source at
that call site if the distinction matters for a specific investigation.

**The diagnostic technique that actually broke the case, worth reusing
directly rather than re-deriving:** the investigating session's first
theory (an offered LFB silently declining to a banked fallback that drew
nothing) turned out to be an artifact of a `uvconfig` run that had been
interrupted mid-write, leaving a half-written `UVCONFIG.DAT` advertising a
mode the card couldn't actually produce — re-running `uvconfig` to
completion and re-confirming via UniVBE's own printed banner retracted
that theory outright (see `dos-rig-operations`/vcctrl's rig-hazards
material on why a card swap or an interrupted config run needs a full
re-verify, not just an assumption it took). With configuration
independently confirmed correct, the same corruption signature
reproduced — and the actual cause was found by **logging the code's own
cached value directly beside a freshly-queried live value at the exact
moment of the flush**, not by inferring a mismatch from the visual
symptom: `pitches_match=0 src_pitch=512 vram_pitch=640` is a direct
before/after comparison instrumented in the code, and it's a technique
worth reusing verbatim for any similar "renders wrong shape on real
hardware only" investigation. A second, independent corroborating tell
sealed it: a "640x480" measurement cell read numerically identical to the
512x384 control cell (same drawn area, same drawcall count) — meaning the
engine was still doing 512x384 work while believing it was at 640x480.
That kind of exact-match-to-the-wrong-control number is a strong tell a
real hardware defect would not produce.

**Triage order:** a real-hardware-only, DOSBox-X-clean video
corruption/tearing symptom should be triaged as "probably a port-side
format or stride assumption, or a stale rig configuration" **before**
"probably a hardware/VESA defect" — on this hub's history so far, every
hypothesis that reached for a hardware defect first was wrong.

**No confirmed precedent on Cirrus (CL-GD5430/5434) or S3 ViRGE** for the
stride/pitch hazard specifically — doskutsu's own extensive real-hardware
history has no recorded incident on either chip. Best-supported reason
(inference, not proven): both support native 320x240 directly, so a
port's mode request is satisfied at the first mode-set with no later
corrective mode-set to different geometry — the specific trigger (a
closest-match miss, *then* a second mode-set to genuinely different
pitch) never arises for them. Since 0125 is a generic framebuffer-layer
fix rather than Mach64-gated, it already protects those cards too if they
ever hit an in-place geometry change for some other reason — "not
observed" means the trigger condition hasn't occurred for them, not that
the bug class is unprotected there. The window-surface-format hazard
above is a different story: it's not chip-specific at all, and can occur
on any card whose auto-selected mode's format doesn't match what a port's
rendering code assumes.

**For contrast: Cirrus's one confirmed genuine hardware defect.** Not
every real-hardware-only video symptom traces back to port-side or
caching bugs — on the g2k rig's Cirrus card (labeled CL-GD5434 throughout
most of `shared/`'s own patch history, though the physical silkscreen
confirms CL-GD5430; HWiNFO's chip-ID misdetects it, and no prior
measurement is invalidated by the mislabel, just the name used for it),
the LFB aperture and the VRAM region the display generator actually reads
from are genuinely decoupled at the hardware level under UniVBE 6.7:
writes through the LFB aperture land in VRAM cells the display generator
never reads, while every BIOS-level readback (display-start GET/SET,
the mode-set return code) reports correctly — the corruption is below the
BIOS interface, invisible to any check phrased in terms of BIOS state.
Confirmed via 100+ post-flush LFB writes producing zero visible screen
change. Fixed in `shared/patches/sdl3-dos/0019` (auto-detect Cirrus,
force-disable LFB, route through the banked `A000:0` window instead).

`0019` itself needed a follow-up, `0020`: it flipped the framebuffer
layer's write path to banked but left the BIOS-level mode-set still
requesting LFB, so the BIOS programmed LFB hardware while the FB layer
wrote through the banked window — the two layers disagreed, producing a
blank screen rather than even partial rendering. The general lesson: a
two-layer decision (BIOS mode-set choice vs. FB-layer write-path choice)
has to be gated on the *same* condition, checked at *both* layers, or you
get exactly this shape of defect — everything reports correctly at the
BIOS level, the screen is still wrong. Both patches are closed at the
shared layer; no port needs to re-derive this.

## Banked-mode bank-select needs a settle delay + readback verification (open, unfixed)

**A real, generalizable shared-backend gap, found via S3 ViRGE but not
specific to that chip.** The shared backend's VESA bank-select write is
unguarded at every call site: `SwitchBank()` (`SDL_dosframebuffer.c`) and
the mode-set-time banked framebuffer-blank loop (`SDL_dosmodes.c` -- a
separate, textually-duplicated far-call/`INT 10h AX=4F05` implementation
of the same bank-select pattern) both issue the write and immediately
trust it took effect, with zero settle delay and no post-write readback
check at either site. `current_bank` is a fresh local initialized to `-1`
at the top of every per-flush caller, so the first row/chunk of every
single frame forces a genuine hardware bank-switch regardless of whether
the hardware was already sitting on that bank from the previous frame.

Found on dossage/Passage via a real-hardware S3 ViRGE (86C375 Rev B)
round, 2026-08-30/31: on ~50-60% of launches, DOSSAGE.EXE booted into a
garbled/static-noise title screen instead of the real title art. The
process was confirmed NOT hung -- it was sitting in the engine's normal
blocking input-wait exactly as designed, with audio playing throughout;
the garbling was a bad render, not a stuck process. Mode-set was
byte-identical between garbled and clean runs (deterministic mode
selection, ruled out as the variable). `SDL_HINT_DOS_PREFER_LFB=1`
(forcing LFB, which never calls `SwitchBank()` at all) fixed this symptom
completely in a real-hardware trial -- 5/5 clean launches vs. 3/5 garbled
on the banked-mode baseline -- strong, if indirect, evidence that the
unguarded bank-select write is a genuine correctness gap on at least this
chip.

**Physically plausible mechanism (consistent with the evidence gathered,
not independently proven):** posted I/O writes on this era of graphics
hardware can be acknowledged by the bus before the target chip has
actually latched the new bank value internally, so a write burst starting
immediately after `SwitchBank()` returns could land some or all of its
bytes in the *previous* bank rather than the intended one -- a misrouted
write, not a simple failed write. This matters because it explains why the
symptom in the section below *spreads* rather than flickers, if the same
mechanism turns out to be involved: a misrouted write corrupts bytes
outside the region a later, correctly-landed frame would naturally
overwrite, so damage could accumulate in never-revisited VRAM instead of
self-healing next frame. There is a textual precedent, verified against
the actual patch, for "a bank-switched write doesn't land where it
should" already in this codebase's own history: `shared/patches/sdl3-dos/0029`
fixed a real Cirrus missing-bottom-strip regression where a bank-1
`dosmemput` wasn't reaching VRAM in practice despite correct on-paper
bank/offset math -- but that instance's root cause was a code-generation
issue (`SDL_FORCE_INLINE` expansion at the call site producing a bank-1
write that didn't land, fixed with explicit loop-carried locals instead
of modular-arithmetic recomputed per-iteration), not a hardware timing
race. Worth knowing the *symptom shape* isn't unprecedented here even
though the cause differed that time -- and worth not assuming the two are
the same bug just because the symptom rhymes.

**Not yet fixed at the shared layer, and deliberately not rushed.** Worked
around for dossage/Passage by forcing LFB (`SDL_HINT_DOS_PREFER_LFB=1`),
which avoids the banked path entirely -- **this is a workaround, not a
fix**; the settle-delay/readback-verification gap itself is still open in
`shared/`, unfixed, and remains a latent correctness risk for any port or
card that takes the banked path (see `HARDWARE.md`'s table). The
diagnostic delay primitive a real fix would reuse already exists --
`SDL_HINT_DOS_BANK_CHUNK_PROBE_DELAY` (patch `0131`, an `inportb(0x80)`-based
delay) -- but it's wired only into a diagnostic chunk-probe readback,
never into the real write path. Wiring an equivalent bounded
readback-verify retry into production `SwitchBank()` is an open fix
candidate needing its own review and testing before landing, rather than
every port discovering this gap independently by forcing LFB around it.

## A same-card control run is the fastest way to rule "hardware defect" in or out

dossage/Passage's same 486DX2-66 + S3 ViRGE campaign also hit a second,
separate runtime symptom: visual corruption that progressively spread
during sustained gameplay -- a small artifact appearing roughly 45-85
seconds in, near-total corruption by ~120 seconds, flat fps throughout the
run (ruling out a stall/hitch as the trigger). Distinct from the
launch-time bank-select issue above: it survived a power-cycle and
reproduced identically under both banked *and* forced-LFB video paths --
which by itself rules out video-mode/bank-switching as the mechanism,
since forced LFB never calls `SwitchBank()` at all and still showed it
equally.

**Reaching for "hardware defect" here would have been wrong** -- the
triage order this hub already documents above (port-side/stale-config
before hardware) held. A real-hardware control run of doskutsu itself, on
the *same physical card*, ran clean for 3.5+ minutes -- direct evidence
the card itself is fine, without needing to fully explain the symptom
first. Several concrete hypotheses were then chased and ruled out with
real evidence rather than assumed: a doskutsu-config comparison (LFB +
16bpp was a real partial mitigation, but ruled out as *the* cause), loop-
yield timing (ruled out via A/B), a pitch-cache/window-recreation theory
(ruled out -- no live trigger found), and audio-IRQ frequency (inconclusive,
and if anything pointed the wrong way -- doskutsu's own organya backend
interrupts *more* often than dossage's, not less, so higher IRQ rate alone
doesn't explain a symptom doskutsu didn't show). The symptom eventually
stopped reproducing after an unrelated session restart/interruption and
stayed resolved across many subsequent real-hardware runs. **Record this
honestly as: real, reproduced, eventually stopped reproducing, root cause
not conclusively identified** -- neither "fixed" (the mechanism was never
proven) nor "hardware defect" (the control run and the LFB-independence
both argue against it). If this resurfaces on a future port or a future
S3 ViRGE session, start from this list of ruled-out hypotheses rather than
re-deriving them, and lean on the same-card doskutsu-control-run technique
early -- it's cheaper than chasing the symptom's own mechanism first and
it directly answers the one question ("is this the card?") that most
determines where to look next.

## Two traps in pixel code instantiated across several color depths

A blit or conversion routine templated over 8/16/24/32bpp will be
compiled several times, and both traps below are silent in most of those
instantiations — which is what makes them dangerous. A test suite
exercising the common depths passes.

**Derive a pixel count from pointer difference, never from `sizeof`.**
At 24bpp a pixel is three bytes behind an `unsigned char*`, so
`sizeof(PIXEL)`-based arithmetic is correct at 8, 16 and 32 and silently
wrong at 24 alone. One instantiation failing out of four is the worst
possible failure distribution: every arm a casual test covers is green.
A run-length blit in one dosags patch hit exactly this.

**Never use a pointer-valued macro in a multi-declarator declaration.**
`PIXEL_PTR rs = s, rd = d;` where the macro expands to `unsigned short*`
declares `rs` as a pointer and **`rd` as a plain integer**, because the
`*` binds to the first declarator only. Here the compiler rejected the
initialization; with a compatible type it would not have, and the result
is memory corruption rather than a wrong number. **Use a `typedef`**,
which removes the whole class rather than requiring anyone to remember
it.

## Reduced color depth

8-bit or 16-bit software surfaces are strongly preferred over 32-bit on
486/Pentium-class hardware — see `optimization.md`. Full-screen pixel
copies and pixel-format conversion are consistently the first or second
most expensive thing on this class of hardware.

This is also a correctness-risk reduction, not just a performance one.
[DevilutionX](https://github.com/diasurgical/DevilutionX) — an unrelated
engine also porting to DOS via SDL3 — sets
`DEVILUTIONX_DISPLAY_PIXELFORMAT SDL_PIXELFORMAT_INDEX8` explicitly for
its DOS build, sidestepping packed-multi-byte pixel-format conversion
(the exact class of code implicated, though not yet confirmed as root
cause, in dossage/Passage's real-hardware Mach64 investigation above)
entirely. 8-bit indexed VGA/VESA banked-mode paths are the most
thoroughly real-hardware-proven code in `shared/patches/sdl3-dos/`; a
port that can accept indexed color inherits that maturity directly,
where a 32-bit-truecolor port exercises less-trodden conversion paths on
whatever format a given card's BIOS happens to pick.
