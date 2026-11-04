# File transfer: packaging, staging, and verifying

Covers the whole path from "a build exists on the build host" to
"a verified copy is sitting where the game actually reads it on the rig,"
and the reverse for pulling results back. Useful standalone -- shipping a
build over for manual poking is a smaller, more common operation than a
full `dos-hardware-validation` campaign, and shouldn't require reading
the campaign material to do safely.

For the literal transfer mechanics (stage/send/fetch commands, FTP-over-
mTCP specifics), see vcctrl's own `vcctrl-rig-hazards` and
`vcctrl-common-workflows`, and this hub's `docs/hardware-testing.md` for
the interim CLI sequence (`vcctrl stage-file` / `send-file` / `get-file`
etc.).

## Package before you stage

Before a build ever reaches the rig:

- **Delta-only staging.** Send only what changed since the last known-good
  package, not a full re-bundle every time -- keeps transfer time down and
  makes it obvious from the transfer log what actually moved.
- **Per-run log-tag naming.** Name each run's log output with something
  that won't collide across repeated runs (a tag derived from run
  identity, not a fixed filename) -- a fixed name means the second run of
  a session silently overwrites the first run's evidence before it's been
  collected.
- **8.3-filename and CRLF discipline for anything that boots/executes on
  the DOS side** (BAT launchers especially) -- a name or line-ending that
  doesn't survive DOS's own file conventions doesn't fail loudly, it just
  doesn't do what you expected when the DOS side tries to run it.
- **sha256 + `strings` verify the binary before bundling it**, not just
  after it lands on the rig -- catching a bad build before it's staged is
  cheaper than discovering it after a transfer round-trip.
- **A minimal two-step handoff** (stage, then a single send/run command)
  keeps a manual real-hardware session low-friction for whoever's driving
  it interactively, rather than requiring a long checklist per transfer.

## Bundling a multi-file payload into one zip

Per-file staging costs a reboot pair (stage, then `send-file --return`)
*per transfer*, and a payload with subdirectories that need to pre-exist
on the target can force several such batches -- DOSSAGE's first
real-hardware pass needed 4 batches for 16 files across a couple of
subdirectories, on top of the reboot cost that's inherent to each batch.
For a small payload that's tolerable; for a larger port (more assets,
deeper directory tree) it stops being tolerable well before the payload
itself gets big in absolute terms. This section is a **recommendation,
not yet exercised on real hardware** -- the rig-infra prerequisite below
doesn't exist yet, so treat this as the plan to follow once it does, not
a proven path.

**The shape:** package the whole payload into a single zip on the build
host, transfer that one archive through vcctrl's *existing* single-file
`stage-file`/`send-file` path unchanged, then unzip it in place on the
target. This is not a new vcctrl capability -- it's the same two-reboot
cycle as any other single-file transfer, just pointed at one archive
instead of N files, so the reboot cost stops scaling with file count or
directory depth.

- **Give the zip itself a short, fixed, always-8.3-safe name** (e.g.
  `PAYLOAD.ZIP`) so it trivially survives vcctrl's own 8.3-rename step
  regardless of what's inside it -- the archive's own filename is not
  where 8.3 risk lives here.
- **8.3-safety for the *entries* inside the zip is enforced at zip-build
  time on the build host, before staging** -- not something to lean on a
  DOS-side unzip to fix on extract. A DOS unzip extracts whatever's
  already in the archive's own directory; it doesn't rename intelligently
  the way `stage-file` does for a single file. This isn't actually new
  overhead specific to this technique, though: real DOS needs 8.3-safe
  paths regardless of transfer mechanism, so a payload that isn't already
  8.3-safe is already broken on real hardware, zip or not (see "DOS 8.3
  filename mapping" above, and the doskutsu/DOSSAGE precedent below).
- **Integrity is not a new problem to solve.** Once the zip is "the file"
  being transferred, the existing sha256 round-trip (Stage -> send ->
  verify, above) already covers the whole archive end to end, unchanged.
  A DOS-side unzip (INFO-ZIP's `UNZIP.EXE` is the standard real-mode-DOS
  choice) additionally verifies each entry's own CRC32 on extract and
  fails loudly on a mismatch -- so this ends up with *two* independent
  integrity checks (sha256 of the whole archive, CRC32 per extracted
  file) rather than the one a per-file transfer has today.
- **Prerequisite that doesn't exist yet:** there is currently no DOS-side
  unzip utility anywhere in the rig's boot/NET environment. Adding one
  (INFO-ZIP `UNZIP.EXE`, real-mode DOS native, light enough for a
  486DX2-50) plus a `RUN.BAT` step that unzips before the actual test
  runs is rig infrastructure -- coordinate with whoever's driving vcctrl
  for the target rig before assuming this technique is available; a port
  can't add this itself.
- **Precedent for the packaging shape**: doskutsu's own `scripts/release.sh`
  already produces almost exactly this artifact via `make dist` ->
  `dist/doskutsu-<ver>.zip` -- a real CF-ready bundle (exe, CWSDPMI,
  license texts, support data) that's already 8.3-safe since it ships to
  real hardware today. Mirror or reuse that shape for a QA-staging zip
  rather than inventing a separate packaging convention from scratch.

## Stage -> send -> verify

1. **Stage** the local binary/package, identified unambiguously (a build
   directory can contain more than one candidate if a previous build
   wasn't cleaned -- see `dos-realhw-verification`'s stale-cache material
   for why that happens).
2. **Send with an explicit destination directory.** A transfer
   capability's default destination is often a generic inbox (e.g.
   `C:\XFER\IN`), not wherever the game actually runs from. A naive send
   silently "succeeds" while dropping the file in the wrong place, and
   whatever runs next reads stale content from the real install directory
   without any error anywhere in the chain. Look up (or ask) the live
   install directory per port explicitly; never assume the transfer
   capability infers it.
3. **Verify with a sha256 round trip** -- hash the local file, hash what
   landed, compare. A size match alone is not sufficient; two different
   builds can coincidentally match in size.

## DOS 8.3 filename mapping

A long/mixed-case filename gets mapped to DOS's 8.3, all-caps convention
on the way over. The safe failure mode is a transfer that's **refused
before typing** when a name can't map cleanly -- the dangerous failure
mode is a silent, unexpected truncation that produces a file with a
different name than intended, which then doesn't match whatever the next
step expected to find. Prefer already-DOS-safe names for anything staged,
so there's no mapping ambiguity to reason about at all.

## Same-destination collision, generalized

Two transfer operations targeting the same destination path without a
collect/verify step in between is the same shape of bug regardless of
what's being transferred -- a log dump, a result file, or an arbitrary
staged binary. If operation B can start before operation A's output has
been fetched and confirmed, B can silently overwrite what A produced
before anyone got to look at it. Build a collect-then-verify barrier
between any two transfers that could plausibly target the same path,
the same way `dos-hardware-validation`'s RUNMANIFEST section guards
against a tick-tagged dump colliding with a previous run's.
