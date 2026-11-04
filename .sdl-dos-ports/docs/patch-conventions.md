# Patch conventions

`shared/patches/` and every port repo's own `patches/<engine>/` follow the
same convention, carried forward from doskutsu.

All patches described here are local-only: we never send anything back to
the projects we patch, no PRs/issues/bug-reports — see `CLAUDE.md`'s
"Never contribute upstream" section.

## Layout

```
patches/<vendor>/NNNN-short-description.patch
```

- `<vendor>` is the directory name under `vendor/` (or, in this repo,
  the upstream project the shared patch set targets — currently `sdl3-dos`
  for `libsdl-org/SDL`, `sdl3-mixer` for `libsdl-org/SDL_mixer`).
- `NNNN` is a zero-padded, monotonically increasing four-digit slot,
  produced by `git format-patch` against the pinned upstream commit.
  Reserve slots rather than renumbering existing patches when inserting
  work — insertions get the next free number, not a renumber of history.
- Patches are generated with `git format-patch`, one concern per patch.
  Don't bundle an unrelated fix into a patch whose subject describes
  something else.

## Patches are vendored per-port, not shared

Every port owns a real, standalone copy of `patches/<vendor>/` for
*every* vendor, including a shared one like SDL/SDL_mixer.
`scripts/new-port.sh` seeds `patches/SDL/` and `patches/SDL_mixer/` with
a one-time copy of this hub's current series at scaffold time -- real
files, never a symlink into `.sdl-dos-ports/` -- so a plain `git clone`
of a port repo builds standalone: no hub reach-back needed for patches
specifically, even though the rest of `shared/` (build fragments,
`runmanifest.h`, `midi_sched`, agent charters, tools) still comes in via
the `.sdl-dos-ports/` subtree/submodule as before.

This hub's own `shared/patches/sdl3-dos/` and `shared/patches/sdl3-mixer/`
stay put as a **reference library a new port scaffolds from**, not an
authoritative series existing ports track. There is no ongoing sync: a
hub improvement made after a port is scaffolded does not reach that port
automatically. Getting a fix from one repo to another -- port to hub
(so the *next* new port gets it by default), hub to an
already-scaffolded port, or port to port -- is always a deliberate,
reviewed act, exactly like porting a fix between any two independent
repos, because that is now what they are. See "When a port discovers a
shared/-relevant fix" below for the actual mechanics of that act.

A temporary, single-investigation diagnostic or workaround is just
another numbered patch in a port's own `patches/SDL/` now -- there is no
separate shared-vs-local distinction to make. It still needs its removal
planned at landing time; see that section below, which applies uniformly
now rather than only to the hub's own series.

(This replaced an earlier shared-series-plus-`patches/<vendor>-local/`-
overlay design, retired 2026-09-01 in favor of full per-port vendoring
so a port never depends on the hub to build. If you need the old
design's reasoning, it's in this file's git history.)

## Commit / patch subject

Prefix the subject with a bracketed tag identifying what layer the patch
touches, e.g. `[SDL3-DOS]` for `shared/patches/sdl3-dos/`. Explain **why**,
not just what — a patch whose message says "fix palette bug" is far less
useful later than one that says "S3 ViRGE reports 6-bit DAC width but
accepts 8-bit palette writes; without this the top two bits of every
channel are lost on that chipset."

## Vendor pinning

`vendor/sources.manifest` (doskutsu's format, reused as-is):

```
<name>  <url>  <ref>  <sha>
```

Snapshots pinned by commit SHA, not git submodules — `scripts/fetch-sources.sh`
clones and checks out the pinned SHA, `scripts/apply-patches.sh` then
applies the numbered series on top, `scripts/verify-patches-applied.sh`
sanity-checks patch-file count against applied-commit count. All three live
in `shared/scripts/` and are fully generic — a port repo's own
`vendor/sources.manifest` just adds its engine's entry alongside the SDL3
stack entries.

## Neutral naming inside `shared/`

Because `shared/patches/sdl3-dos/` is consumed by every port, any SDL hint
or log-tag name it introduces must be project-agnostic. Use the
`SDL_HINT_DOS_*` prefix (not a per-port name like the historical
`SDL_HINT_DOSKUTSU_*`) for any new hint, and a similarly neutral log-file
naming scheme (e.g. driven by an env var the port sets, rather than a
hardcoded project name) for any new diagnostic logging. Patches carried
over from doskutsu that introduced `SDL_HINT_DOSKUTSU_*`-style names have
been renamed to this scheme (see `shared/patches/sdl3-dos/README.md`'s
"Naming debt (resolved)" section) -- don't reintroduce a per-port name for
anything new.

## When a port discovers a shared/-relevant fix or lesson

A port's `patches/SDL` and `patches/SDL_mixer` are that port's own real,
vendored files (seeded once at scaffold time, never symlinked) — editing
them only ever changes that one port's own patch series, never the
hub's. There is no `git subtree push` or submodule-commit path for
patches specifically any more (the rest of `shared/` still works that
way; just not `patches/`). Getting something shared/-relevant into the
hub is always a separate, explicit act:

**The pattern that works** (developed across doskutsu/dossage's real
cross-port collaboration): a port session that finds something
shared/-relevant — a real bug in the platform layer, a hazard worth
documenting for every port, a piece of methodology worth reusing —
reports it to whichever session is coordinating the hub (with enough
detail to verify, not just a conclusion: the port's own patch file or
diff, the actual evidence, what's confirmed vs. inferred). The hub-side
session verifies it against the actual current state of
`shared/patches/` (never take a summary on faith — this hub's own
history already includes a case where an unverified secondhand relay
would have shipped a subtly wrong hazard writeup), re-derives a real
`git format-patch` against the hub's own series rather than
hand-transcribing whatever the port session sent over (see "Verify
provenance" below), runs the same validation any `shared/` change needs
(a full patch-series replay against the pinned upstream SHA from a clean
reset, not just a diff read), gives it the next free slot in the hub's
own series, and commits it here.

**This updates the hub's reference copy for the *next* new port. It does
not reach any port already scaffolded.** A port that wants a fix landed
in the hub after it scaffolded has to pull it in by hand — copy the new
patch file(s) into its own `patches/SDL/` (or `SDL_mixer/`), same as
adopting a fix found in any other independent repo. There's no pin to
bump for patches specifically any more.

This applies to genuine fixes (a real bug in SDL3-DOS backend code, like
patch `0127`'s hardcoded-path fix) and to documentation/methodology (a
real-hardware hazard worth every port knowing, like the video/audio
hazard catalogs in `docs/video.md`/`docs/audio.md`) equally — "feedback"
flowing back is as much a first-class case as a patch is, not an
afterthought; the doc/methodology side of this was never coupled to the
patches-symlink mechanism and is unaffected by patches now vendoring
per-port. A port's own engine-specific patches (`patches/<engine>/`)
essentially never belong in `shared/` by `CLAUDE.md`'s own "nothing
game-specific" rule — the cases above are specifically about a port
discovering something true about the *shared* platform layer while
working on its own engine, not about promoting engine code itself.

## Gotchas beyond the basics above

- **`LC_ALL=C` when enumerating or sorting patch files.** Locale-aware
  collation (the shell's default) treats `-` as punctuation promoted next
  to alphabetics, so e.g. `0014a-foo.patch` can sort *before*
  `0014-foo.patch` even though ASCII byte order says the reverse --
  `shared/scripts/apply-patches.sh` exports `LC_ALL=C` specifically to
  avoid this, and any other tooling that walks a `patches/<name>/`
  directory and cares about apply order needs the same export. This is
  the same shape of bug as the `find -L` symlink-enumeration fix elsewhere
  in `shared/scripts/` -- a POSIX tool's default behavior silently isn't
  what the enumeration code assumed, and it only surfaces once something
  (a symlinked directory, a locale that isn't `C`) triggers the
  divergence.
- **The patch-cascade base trap.** Authoring a new slot-`N` patch on a
  vendor workspace that already has patches numbered `N+1` and higher
  applied on top bakes the new patch's hunks in against the *wrong* base
  -- `git format-patch` captures whatever the workspace's current state
  is, not the state right after slot `N-1`. Reset the workspace to the
  state immediately after the last patch before the new slot (or apply
  patches strictly in order and insert at the tip, not in the middle)
  before generating the new patch, or the resulting file will fail to
  apply cleanly (or worse, apply with silently wrong context) for the
  next person who runs the series from scratch.
- **A rolled-back vendor tree does not rebuild by itself unless the build
  depends on the tree's state.** Checking `vendor/SDL` out one commit back
  (to build a control without the newest patch), forward again, or editing
  it by hand changes no patch file, so a library that depends only on the
  patch files is never rebuilt. The "control" is then the real library with
  a different build stamp; dosags only caught it because the control had to
  fail and didn't. `shared/build/sdl3-dos.mk` makes each SDL library depend
  on `build/vendor-state/<vendor>.state`, which is rewritten only when the
  tree's HEAD or uncommitted diff changes. A port with its own build rules
  for a vendored library needs the same dependency.
- **A build stamp must not contain the checkout's absolute path.** If a
  stamp like `BUILD_SHA12` hashes the OUTPUT of `sha256sum FILE`, that
  output line includes FILE's path exactly as given (`<hash>
  /home/.../worktree/.sdl-dos-ports/shared/...`). The same holds for
  echoed compiler or cmake flags that contain `-I$(HUB_DIR)/...` or any
  other absolute path. So identical code built in two worktrees, or two
  clones, gets two different stamps, and the binaries differ only in
  those 12-13 stamp bytes. In dosags (2026-09-24) that looked like a code
  difference that wasn't there. Hash CONTENTS keyed by RELATIVE path, in
  a fixed order, with each file's name in the input. The one recommended
  form:
  `(cd "$ROOT" && LC_ALL=C sort <list> | xargs sha256sum) | sha256sum`.
  Each input line is `<hash>  <relative path>`, so a rename, or bytes
  moving from one file into the next, changes the stamp. A bare
  `xargs cat` of the files would not catch either. Replace absolute roots
  in echoed flags with a placeholder
  (`$(subst $(REPO_ROOT),@ROOT@,...)`). The hub's own hashes follow this
  rule:
  - `shared/scripts/series-sha.sh` folds in basenames only, plus
    contents, in `LC_ALL=C` order;
  - `sdl3-dos.mk`'s vendor-state stamp hashes git's output, whose paths
    are repo-relative.
- **Uncommitted vendor-tree work is one `apply-patches.sh` run away from
  gone.** `apply-patches.sh` hard-resets a vendor tree to its pinned SHA
  before applying the patch series -- routine, and destructive to
  anything sitting uncommitted there, including hours of real
  investigation work that hasn't been turned into a patch yet. The
  destructive run is often invoked for a completely unrelated reason (a
  different vendor's patch, or someone else verifying a `shared/` patch
  applies cleanly), so it can destroy work nobody intended to touch.
  `apply-patches.sh` refuses to reset a dirty vendor tree by default
  (`APPLY_PATCHES_FORCE=1` to override) as a safety net -- but the actual
  fix is discipline: commit (even to a scratch branch) or turn working-tree
  edits into a numbered patch at the end of each investigation slice,
  don't let real work live uncommitted in a vendor tree for hours.

## A patch series' stated rationale is not evidence

When reading another engine's patch history for precedent (e.g. citing
doskutsu's own history to justify a design choice in a new port), read
the actual gating/guard code the patches touch, not just the commit
titles or the rationale prose in later patches' comments. A real case on
this hub: a patch's own comment cited an earlier patch's stated
motivation as if it were settled fact, when a later patch in the same
series had already directly tested that motivation and found it false (a
controlled A/B showed the fix bought no correctness benefit at all, only
cost). The series' own history disproved its opening premise several
patches later — and the disproof was recorded, but easy to miss if you
stop reading at the first patch's comment. Trust what the code's actual
conditionals do (what's gated behind what flag, under what real
condition) over what any single patch's commit message claims it
accomplishes.

## Verify provenance before landing a peer-authored patch

A patch handed over through a chat/message channel (another session
pasting a diff rather than handing over a real file) can lose fidelity in
transit — trailing whitespace on blank context lines is a documented real
case, silently stripped by common text-transport paths, which can corrupt
a diff without any visible sign in the pasted text. Before landing a
peer-authored patch in `shared/patches/`:

- Prefer a real `git format-patch` output (with genuine commit metadata)
  over a hand-typed or reconstructed diff — ask the author to generate it
  from their own working checkout rather than reconstruct it yourself
  from a chat message, if you don't have the toolchain to verify it
  applies cleanly.
- If you must reconstruct one from pasted text, say so explicitly and
  flag the reconstruction as unverified until the author (or a fresh
  `apply-patches.sh`/`git am` run against a real checkout) confirms it
  applies byte-for-byte.
- Review the diff's actual content against this hub's own conventions
  (binary `fopen` mode, ASCII-only, neutral naming) before landing it —
  don't assume a peer session's own review caught everything; a real
  instance of this caught a text-mode-`fopen` bug that every prior patch
  in the same series had avoided.

## A temporary diagnostic patch needs its removal planned at landing time

**This applies wherever a diagnostic patch lands now** — the hub's own
`shared/patches/` (reference-copy landings, reviewed the same as any
other patch here) and a port's own `patches/SDL/` alike, now that every
port's series is real, vendored files rather than a shared symlink. What
follows is the real incident that established the rule.

A diagnostic patch landed in `shared/` — gated or not, cheap or not —
becomes a permanent, silent tax on every port that pins past it the
moment its investigation concludes and nobody schedules its removal. A
real instance: a temporary `DOS_Yield()` cost diagnostic, landed with its
own commit message stating "temporary, to be reverted once the
investigation concludes," measured a genuine per-frame cost
(~0.70ms/frame). The investigation concluded — the diagnostic had done
its job — but the removal patch wasn't authored in the same pass. Every
port pinning past that commit inherits the cost with nobody having
decided to pay it, and a rebuild that suddenly runs measurably slower has
no obvious reason to suspect the patch series rather than the change
someone just made.

The convention: when landing a temporary diagnostic, either land its
removal in the same session once the measurement is in hand (this hub's
own patch `0028` is precedent — land, measure, strip, all as part of one
investigation's patch sequence), or explicitly note in the landing
commit's message that a removal patch is still owed and by what
condition ("once X is measured" / "once Y concludes") — never leave it as
an implicit, unscheduled follow-up. A reverted diagnostic's own removal
patch should confirm (not just claim) that the resulting binary is
byte-identical to what the pin produced before the diagnostic ever
landed.

## Negative results are worth keeping

`shared/patches/sdl2-compat-notes/` carries forward doskutsu's write-up of
why a static `sdl2-compat`-on-DJGPP approach didn't work — keep documenting
dead ends like this so the next port doesn't re-spend the time proving the
same approach fails.
