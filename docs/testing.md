# Testing

Three environments, in increasing order of authority:

```
DOSBox-X  ->  86Box  ->  real hardware (vcctrl)
```

## DOSBox-X: the default automation gate

Minimum pipeline any port should have, adapted from doskutsu:

```
fetch -> patch -> compile -> link -> package -> DOSBox-X launch ->
verify expected log / screenshot / exit
```

`shared/tools/{dosbox-launch.sh,dosbox-run.sh,dosbox-teardown.sh}` and
`shared/tests/{smoketest,sdl3-smoke,sdl3-mixer-smoke,sdl3-image-smoke,
dpmi-lfn-smoke}/` give a new port a generic DJGPP/CWSDPMI/SDL3 bring-up
smoke test for free — run these before writing any game-specific test.

Recommended smoke checks for a port's own suite: executable starts, SDL
initializes, video mode opens, an input event reaches the engine, the audio
device initializes, an asset loads, the game reaches its title/first room,
a save file writes and reloads, clean exit. If the game has deterministic
input, a replay-based smoke test is worth the investment.

**Anchor a smoke/logback stop condition to a full, unique line format,
never a bare substring.** A smoke runner's stop condition grepped for
`calib-exit`; a diagnostic's own boot-time help text, describing what its
markers mean to a human reader, happened to contain that literal
substring too. The runner matched the help text ~6 seconds after boot and
killed the session -- the resulting truncated run looked exactly like a
real hang at a specific tick, and cost real debugging time (including a
binary flip-flop) before the actual cause (the runner's own stop
condition, not the binary) was found. Fix: match a full, distinctive line
shape (e.g. `[calib-exit] tick=`), never a bare word or phrase that
unrelated banner/help text could also contain. Corollary for anyone
writing a diagnostic's own boot banner: don't put a bracketed
marker-name literal inside its *help text* at all -- describe it in
prose instead, so the help text can't accidentally become grep bait for
a later stop condition or content-validity check.

For the day-to-day mechanics and judgment calls of working in this tier
-- which of the three DOSBox-X tools to reach for, the emulator-only
escape hatches already baked into the shipped confs and why they must
never leak to a real-hardware run, what a clean DOSBox-X pass does and
doesn't prove, and what local timing observation can (and can't) be used
for -- see `shared/skills/dos-emulator-workflow/`. This is the default
mode for most of a port's life, not just a fallback when a rig isn't
available.

## 86Box: a second correctness opinion

doskutsu also validates against 86Box as an independently-implemented
emulator, catching bugs that happen to be masked by one specific emulator's
quirks. Not required for every commit, but worth running before declaring a
milestone (title screen, playable, etc.) reached.

## Real hardware: authoritative

DOSBox-X and 86Box are automation, not proof. Real hardware determines
actual performance and device compatibility — see `docs/hardware-testing.md`
for how that's currently done via vcctrl, and `HARDWARE.md` for the
reference machine matrix. `/HARNESS-STANDARD.md` at this hub's root is
the formal contract underneath all of this tiering — what a test harness
run must be able to prove about itself before anyone trusts it, agent or
human. `shared/skills/dos-hardware-validation` and `dos-realhw-verification`
are its practical application on this hub's own vcctrl-driven rig.
`docs/harness-lessons.md` holds the dated incidents behind that skill's
`harness-invariants.md` checklist -- read it when a checklist line needs
its reason.

Passing DOSBox-X (or even 86Box) is not proof a build is actually correct
on real hardware — a clean smoke can pass while never exercising the
hardware I/O path a fix depends on, and a clean `strings` grep proves a
symbol was compiled in, not that it ran. See
`shared/skills/dos-realhw-verification/` for the failure shapes this has
actually taken (stale build caches, emulator-vs-hardware I/O divergence,
build-host tooling traps) and the discipline for debugging a bug that's
specific to real hardware.

Never commit an audio capture of a game's music (QA `.ogg`/`.wav`/`.opus`/
`.flac`/`.mp3` from a harness run, in any repo): it is the game's
copyrighted music. Keep it local and commit only its sha256 and the
numbers read from it (dosags had to purge them from its whole history,
2026-09-25).

## Never contribute upstream

If a probe or smoke test surfaces a bug in DOSBox-X, 86Box, or any other
external tool this harness depends on, that stays a workaround/note in our
own docs — never a PR, issue, or bug report against that project. See
`CLAUDE.md`'s "Never contribute upstream" section.

## Probe library

`shared/tests/probes/` holds standalone, DJGPP-only, no-SDL, no-engine
hardware-characterization tools (memory bandwidth, palette DAC, VESA/
Cirrus/S3 chip identification, WaveBlaster/MPU-401/GUS detection, PC
speaker). Reach for these when real-hardware behavior is unexplained by the
game/engine code — they isolate whether a bug is in your port or in how a
specific card responds.
