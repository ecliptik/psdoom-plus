# RUNMANIFEST-style structured logs

A single grep-able block a DOS port's engine emits once per run, carrying
every metric a validation cell needs -- so collection is "grep the block"
instead of manual log archaeology or hand arithmetic across scattered log
lines. `shared/include/runmanifest.h` is a ready-to-use, header-only
implementation of this -- schema v1, 18 fields, environment auto-detect,
an `env_block_sha12` config fingerprint -- adapted from doskutsu's own
`include/runmanifest.h` (MIT-licensed for `shared/`; doskutsu's own copy
stays GPL-3.0). Include it and call `runmanifest_emit()` at clean
shutdown rather than reimplementing the pattern from scratch.

## Shape (schema v1, exact field set from `shared/include/runmanifest.h`)

```
[RUNMANIFEST-BEGIN]
schema_version=1
environment=realhw
binary_sha12=04388290f735
wave_tag=PLAY0
scene=title
env_block_sha=a1b2c3d4e5f6
started_utc=2026-05-14T00:42:05Z
duration_s=58
exit_code=0
fps_p50=58.30
fps_p95=54.10
audio_vital_status=NA
regime=NA
banner_required_hit=12
banner_required_total=12
banner_forbidden_hit=0
banner_optional_hit=3
critical_count=0
warn_count=1
[RUNMANIFEST-END]
```

This is the actual shared schema now, not an illustrative example --
match these field names for any port using `shared/include/runmanifest.h`
directly. A port with genuinely different needs (per-stage fps
breakdowns beyond `fps_p50`/`fps_p95`, a PRNG seed for deterministic
replay, a TAS-replay identifier) can still add its own extra `key=value`
lines inside the same `BEGIN`/`END` block -- the header's `runmanifest_t`
covers the common core, not an exhaustive or closed set. The properties
that matter, whether using the shared header or a from-scratch
equivalent, are:

- **One block, emitted unconditionally at a defined point** (e.g. clean
  exit or a specific milestone), so collection doesn't need to guess
  whether the run got far enough to log anything.
- **Delimited with a literal `BEGIN`/`END` marker pair** so it's
  trivially greppable out of a log file that may also contain unrelated
  diagnostic noise.
- **`key=value` lines, one per line**, so extraction is a grep + split, not
  a parser.
- **`environment=` is mandatory** -- this is what lets a downstream
  consumer (a benchmark aggregator, a QA script) distinguish a DOSBox-X
  run from a real-hardware run without out-of-band bookkeeping. See the
  env-var note below for how this gets set.
- **`build_sha12=` (or equivalent) is mandatory** -- a content-based build
  fingerprint, not a `git describe`, so results stay attributable to an
  exact build even across rebuilds of identical source (see
  `shared/build/sdl3-dos.mk`'s note on why `SDL_REVISION` is pinned to a
  deterministic string rather than `git describe` for the same reason).

## Wiring `environment=` via this hub's shared/ hooks

`shared/include/runmanifest.h`'s `runmanifest_env_override()` reads
`DOS_PORT_ENVIRONMENT` directly -- the same neutral variable
`shared/tools/dosbox-launch.sh` already sets as its default, so a port
using the shared header gets correct `environment=` classification for
free, with no extra wiring, as long as launches go through
`dosbox-launch.sh` (DOSBox-X) or set `DOS_PORT_ENVIRONMENT` directly in
the rig's own launch profile for real hardware. (doskutsu's own,
separate, pre-shared-header `include/runmanifest.h` reads its legacy
`DOSKUTSU_ENVIRONMENT` instead -- that's specific to doskutsu's own repo,
unrelated to the shared header described here.)

If a port's engine can't use the shared header directly and hand-rolls
its own environment detection reading some other variable name, pass
`LAUNCH_EXTRA_SET="<PORT>_ENVIRONMENT=dosbox-x"` (or the real-hardware
equivalent, set directly in the rig's launch profile) so the manifest's
`environment=` field still reads correctly. Don't leave this to an
auto-detect default in either case -- defaulting to "realhw" when unset
is the correct fallback for actual real hardware but silently wrong for
an emulator run that forgot to set the override.

## Wiring `build_sha12=` via `RUNMANIFEST_FLAGS`

`shared/build/sdl3-dos.mk` deliberately does not bake a build-fingerprint
macro into its shared `CMAKE_COMMON` (see the migration plan's "Known
naming/wiring mismatches" #4 for the reasoning: only the engine stage
consumes it, so it doesn't belong in a flag block shared by all four
build stages). A port wanting this field composes its own
`CMAKE_CXX_FLAGS="$(NOSIMD_FLAGS) $(RUNMANIFEST_FLAGS)"` in its own
engine-stage recipe, where `RUNMANIFEST_FLAGS` is that port's own make
variable defining the fingerprint macro (e.g.
`-DPORT_BUILD_SHA12=$(shell ...)`), not something this shared fragment
exports for you.

## Wiring `env_block_sha=` via your own allowlist

`runmanifest_compute_env_block_sha12()` takes a NULL-terminated,
alphabetically-sorted array of env-var names as a parameter -- it
deliberately does not ship a built-in list, because every port has its
own set of measurement-affecting hints. Build your own covering whatever
`SDL_HINT_*`/`DOS_PORT_*`/engine-specific env vars your own port actually
varies across cells, pass it in, and the field lets cross-cell analysis
catch "two cells claiming the same config produced different env blocks"
without grepping each variable by hand. A port with no such vars yet can
pass an allowlist of just the ones it expects to eventually need --
`env_block_sha=000000000000` (the documented empty-block sentinel) is a
valid, honest answer, not an error.

## Extraction

Grep between the markers, split on `=`, done:

```sh
sed -n '/\[RUNMANIFEST-BEGIN\]/,/\[RUNMANIFEST-END\]/p' run.log \
  | grep '=' \
  | while IFS='=' read -r k v; do printf '%s\t%s\n' "$k" "$v"; done
```

Feed this into campaign aggregation (collect `per_loop_fps=` etc. across
cells for the ABBA verdict table -- see `abba-methodology.md`) and into
post-hoc confirmation of what a cell actually did (does `environment=`
match what this cell's arm expected?).

**Do not use a RUNMANIFEST field as a cell's `expect_log` gate.** The
block is emitted late -- typically at or after clean exit -- so anything
that disrupts normal teardown (a quit-path hang, a crash on the way out,
any exit-path bug unrelated to the lever under test) can suppress it on a
run that was otherwise completely correct. Gating live cell validity on
it turns an unrelated bug into a false "invalid cell." Use RUNMANIFEST for
metrics extraction and post-collect cross-checking; use an early,
reliably-emitted line (a boot banner, an init-time log line) for
`expect_log`. See `cell-protocol.md`'s `expect_log` section for the
concrete failure this caused and what to do instead when a manifest is
missing.
