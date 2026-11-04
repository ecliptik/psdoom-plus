# Input

## Common targets

```
keyboard
PS/2 mouse / DOS mouse driver
gameport joystick when the game actually uses one
```

`shared/patches/sdl3-dos/` already carries gameport joystick support
(calibration persistence, direct port axis read, per-side scaling,
diagnostics) inherited from doskutsu. Avoid building a new controller
abstraction layer per port — the SDL3 DOS backend's keyboard/mouse/joystick
surface is the target; wrap only as much as the game's existing input
abstraction requires to compile.

Per upstream's own backend documentation
([README-dos](https://wiki.libsdl.org/SDL3/README-dos)): keyboard is
IRQ1-driven with extended-scancode (`0xE0` prefix) support; mouse is the
INT 33h driver, reporting relative motion via mickeys (not absolute
screen coordinates — this is why native-resolution direct mapping,
mentioned below, is the right default rather than something to second-
guess); joystick reads axes via BIOS INT 15h and buttons via direct port
`0x201` reads, matching the direct-port axis-read fix already in
`shared/patches/sdl3-dos/0103`.

## Mouse-driven games (e.g. adventure engines)

- Mouse movement, left/right button, and keyboard shortcuts are usually
  sufficient.
- Disable or simplify any mouse-confinement/warping behavior the original
  engine implements for windowed desktop platforms — it doesn't apply here.
- Native resolution coordinates should map directly; avoid an extra
  scaling/remapping layer between SDL mouse events and game-space
  coordinates.

## What to avoid

Controller abstraction complexity (rumble, multiple simultaneous pads,
hot-plug) unless the specific game genuinely needs it for its core
gameplay loop. Most DOS-era-appropriate ports don't.

## Gameport joystick hazards (from doskutsu's own campaign)

- **A naive discharge-timing axis read is a real fps floor -- already
  fixed at the shared layer, not something to rediscover.** The classic
  way to read a DOS gameport axis polls port `0x201` until the RC
  discharge completes, blocking for the full discharge time on every
  read (~80ms/flip in doskutsu's measurement). `shared/patches/sdl3-dos/0103`
  replaces this with a bounded direct-port read (the discharge-timing
  loop is capped at a tunable iteration ceiling instead of polling to
  completion), taking axis-read cost to effectively free. Any port
  pinning the current shared patch series already has this fix; it's
  listed here so nobody re-diagnoses "joystick input costs 80ms/flip" as
  a new discovery.
- **A raw settings-struct load can silently clobber runtime-computed
  input mappings.** Default axis-to-action maps set correctly at
  input-init time can get wiped by a settings file loaded *after* init
  (a raw struct dump using a sentinel like `jaxis=-1` for "unset")
  overwriting them back to unmapped. Confirmed via an axis trace showing
  SDL reporting real analog values while the game's own action-axis
  lookup returned the unset sentinel. Any default mapping that must
  survive a loaded save file has to be re-asserted *after* settings-load,
  not just set once at init.
- **Keyboard and analog-axis input can fight if a per-frame axis reset
  isn't coexistence-aware.** A "reset stale analog axis state" step run
  every frame can wipe a currently-held keyboard direction too, if both
  input sources are funneled through the same action-axis representation
  without the reset step distinguishing them. Needs explicit logic: don't
  let an axis-state reset clobber a keyboard key that's still held.
- **DOS gameport backends genuinely have no HAT.** SDL3-DOS's gameport
  reports 2 axes / 4 buttons / 0 hats -- there is no digital-hat/D-pad
  concept on this hardware interface at all. A gamepad's D-pad will
  present as either the 2 analog axes or as buttons depending on the
  physical pad, and 4 total buttons is tight if face buttons are also
  needed. Design a default control scheme assuming no hat exists on this
  platform, not as a fallback case to handle only if one isn't detected.
- **A scripted DOSBox-X calibration test validates mapping, not
  hardware-timing dynamics.** A scripted test typically holds one
  constant deflection, so it never exercises the press/release dynamics
  of a real spring-return stick. Split joystick testing along this line:
  the mapping/logic half (axis-to-action, keyboard coexistence) is
  platform-independent and DOSBox-X is a faithful proxy for it; the
  hardware-timing half (the actual analog read behavior) needs real
  hardware.
- **PicoGUS's "USB joystick support enabled" firmware feature can fool
  any port's joystick-detection code into thinking a real gameport
  joystick is present when none is plugged in.** This is a generic
  PicoGUS-firmware hazard, not specific to any one port or engine --
  found on this hub's rig via a boot-profile env var
  (`DOSKUTSU_USE_JOYSTICK`) colliding with the phantom device and killing
  keyboard menu input until cleared, but the underlying trigger (PicoGUS
  presenting a USB joystick device that isn't real hardware) applies to
  any port's own detection logic just as easily. If joystick input
  behaves unexpectedly (menu input dies, an unplugged joystick reads as
  present) on a PicoGUS-equipped rig, check the PicoGUS boot log for USB
  joystick support being enabled before assuming an engine-side bug. See
  `docs/audio.md`'s GUS/PicoGUS section for other PicoGUS-firmware
  quirks and `shared/tests/probes/GUS-PROBES.md`.
