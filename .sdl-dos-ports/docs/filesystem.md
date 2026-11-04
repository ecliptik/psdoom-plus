# Filesystem

DOS constraints must be handled explicitly — most desktop-era SDL games
assume at least one of these in a way that breaks on DOS.

## Audit for

- drive letters
- backslashes vs. forward slashes
- current working directory assumptions
- long filenames (many DOS setups are 8.3-only)
- case-sensitivity assumptions
- per-user config directories
- `$HOME`, `%APPDATA%`, XDG paths, registry, system-wide config dirs
- Unicode filenames
- temp directories
- file locking
- symlinks

## Recommended DOS policy

```
GAME\
    GAME.EXE
    CWSDPMI.EXE
    GAME.DAT
    <CONFIG>.CFG
    SAVE\
```

Keep configuration and saves local to the game directory (or a child
directory of it). Do not depend on `$HOME`, `%APPDATA%`, `XDG_CONFIG_HOME`,
the registry, or any system-wide config directory — none of these exist in
a meaningful form on DOS.

If the engine's file-format assumptions require long filenames, detect
support explicitly and fail with a clear message rather than silently
truncating. Prefer 8.3-safe filenames throughout the base engine port where
practical — it avoids an entire class of transfer/CF-card/real-hardware
friction later (see `docs/hardware-testing.md`).

## What DJGPP file calls actually cost (dosags, 2026-09-27)

Counted with a link-time wrap of `__dpmi_int` (every DOS call the program
makes) on DOSBox-X at fixed cycles; these are call counts, not times. On
a 486 with a CF card each directory scan is in the several-millisecond
class, so the counts are what to cut. Source: dosags `docs/lab/io.md`.

- **`fread` has no direct path.** It always goes through the FILE buffer
  (`_filbuf` plus `memcpy`), which is filled in transfer-buffer-sized
  units (16 KB by default). A large read therefore overshoots the data
  it wanted (a 215 KB clip pulled 229 KB in 18 reads) and copies every
  byte twice. For large reads, `read()` on the file descriptor after
  one exact `lseek`, then `fseek` to resync the FILE, reads exact bytes.
- **A bigger transfer buffer is not a clean win on its own.** `stubedit`
  caps `bufsize` at 0xFE00; at that size the same clip took 10 reads but
  261 KB, because the FILE buffer grows with it. Combined with direct
  reads it becomes one: 4 reads at exact bytes.
- **`stat()` of a missing file is about 13 DOS calls, including 4
  `findfirst` directory scans.** Engines that probe for loose files
  before their package (AGS does, on every first play of a clip) pay
  this each time. An existence test needs one call: INT 21h AX=4300h
  (get attributes, or LFN 7143h), which reads the same attribute byte
  `stat()` derives existence and `S_IFDIR` from.
- **`fseek` followed by `ftell` is two `lseek` calls**, and DJGPP's
  `fseek` itself tells first. A stream wrapper that tracks its own
  position cuts a sequential refill from three calls to one.
- **TZ lookups are file probes too:** see `docs/timing.md` (TZ section).

