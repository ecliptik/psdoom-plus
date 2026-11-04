#!/usr/bin/env bash
# apply-patches.sh -- apply patches/<name>/*.patch to vendor/<name>/.
#
# Patches are produced by `git format-patch` from a working branch in the
# vendor tree and numbered lexically (0001-, 0002-, ...). Ordering matters;
# patches assume earlier patches are already applied.
#
# Behavior:
#   1. For each entry in vendor/sources.manifest that has a concrete SHA
#      (not PIN_ME), `git reset --hard` the vendor tree to that SHA
#      (discarding any prior patches, so this is idempotent).
#   2. Apply every `patches/<name>/*.patch` in lexical order via `git am`.
#   3. If a patch fails to apply, abort the `git am` cleanly, print the
#      failing path, and exit non-zero.
#
# patches/<name>/ is a real, port-owned directory for every vendor,
# including a shared vendor like SDL or SDL_mixer -- scripts/new-port.sh
# seeds it with a one-time copy of the hub's current patch series at
# scaffold time (not a symlink into .sdl-dos-ports/), so a port builds
# standalone from a plain `git clone`, no hub reach-back needed for
# patches specifically. This script needs no special-casing either way;
# it applies whatever real files are in patches/<name>/. See
# docs/patch-conventions.md.
#
# SAFETY: step 1's `git reset --hard` is destructive to anything sitting
# uncommitted in that vendor tree -- including hours of real investigation
# work, not just patch-series churn (a real incident: uncommitted
# vendor-tree edits were nearly lost to a routine `apply-patches.sh` run
# invoked for an unrelated vendor's patch review). Before resetting a
# vendor tree, this script refuses if that tree has uncommitted changes,
# unless APPLY_PATCHES_FORCE=1 is set. Commit (even to a scratch branch)
# or stash before running this against a vendor tree you're actively
# editing by hand.
#
# Run AFTER scripts/fetch-sources.sh (the patches assume the vendor tree
# is populated and at the pinned SHA).
#
# Usage:
#   ./scripts/apply-patches.sh                    # apply to all vendors
#   ./scripts/apply-patches.sh nxengine-evo        # only the named vendor
#   APPLY_PATCHES_FORCE=1 ./scripts/apply-patches.sh nxengine-evo
#                                                   # override the uncommitted-changes refusal

set -euo pipefail

# REPO_ROOT is the CALLER's repo root, not this script's own location --
# see the matching comment in fetch-sources.sh. patches/<name>/ is expected
# to exist under the caller's own repo -- real, vendored files for every
# vendor, including a vendor whose series originated in the hub (SDL,
# SDL_mixer: scripts/new-port.sh seeds patches/SDL, patches/SDL_mixer with
# a one-time copy at scaffold time) -- so this script needs no
# special-casing.
REPO_ROOT="${SDL_DOS_PORT_ROOT:-$PWD}"
MANIFEST="$REPO_ROOT/vendor/sources.manifest"
VENDOR_DIR="$REPO_ROOT/vendor"
PATCHES_DIR="$REPO_ROOT/patches"

# readlink -f, not $0's dirname: a port invokes this through the symlink at
# its own scripts/apply-patches.sh, so BASH_SOURCE is that symlink.
# shellcheck source=./series-sha.sh
source "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/series-sha.sh"

FILTER=""
if [[ $# -gt 0 ]]; then
    case "$1" in
        -h|--help)
            echo "Usage: apply-patches.sh [<name>]"
            echo "  <name>  Only apply patches for the named vendor (e.g. 'nxengine-evo')"
            exit 0
            ;;
        *) FILTER="$1" ;;
    esac
fi

log() { printf '[apply-patches] %s\n' "$*" >&2; }

if [[ ! -f "$MANIFEST" ]]; then
    log "error: $MANIFEST not found"
    exit 1
fi

# Serialize concurrent invocations within the same clone. apply_one() does
# `git reset --hard` then `git am` on each vendor tree; two apply-patches runs
# racing the same tree (e.g. two agents, or `make all` overlapping a manual
# `make patches`) corrupt each other -- one run's reset can land mid-`git am`
# of the other, leaving a half-applied series + a stuck .git/rebase-apply.
# A per-clone flock makes the second invocation WAIT for the first to finish.
# Separate clones have separate vendor/ trees (and separate lock files), so
# they never contend. flock-absent (non-Linux) falls through unguarded.
LOCKFILE="$REPO_ROOT/build/.apply-patches.lock"
mkdir -p "$REPO_ROOT/build"
if command -v flock >/dev/null 2>&1; then
    exec 9>"$LOCKFILE"
    if ! flock 9; then
        log "error: could not acquire apply-patches lock ($LOCKFILE)"
        exit 1
    fi
fi

# apply_patch_dir -- collect *.patch from $2 (LC_ALL=C lexical order) and
# `git am` them against the vendor tree at $1. $3 is a human label for log
# lines. Returns 0 with nothing to do if the directory doesn't exist or is
# empty.
apply_patch_dir() {
    local vendor_path="$1"
    local patches_path="$2"
    local label="$3"
    local name="$4"

    if [[ ! -d "$patches_path" ]]; then
        log "$name: no $patches_path -- nothing to apply ($label)"
        return 0
    fi

    # LC_ALL=C forces ASCII byte-order sort. Without it, glibc's default
    # locale-aware collation treats `-` (0x2D) as punctuation that gets
    # promoted next to alphabetics -- so a filename like `0014a-...patch`
    # would sort BEFORE `0014-...patch` on en_US.UTF-8 even though ASCII
    # byte order has the reverse (0x2D < 0x61). Caught by nxengine during
    # Phase 5 attempt 4 when sdl-engine's `0014a` and `0010a` follow-up
    # patches both surfaced the bug. Renumbering those files to pure-
    # numeric slots (0020, 0021) sidestepped the immediate apply-order
    # problem; this LC_ALL=C export makes the durable fix so any future
    # contributor can use any naming scheme without locale-fragility.
    local patches=()
    while IFS= read -r -d '' p; do
        patches+=("$p")
    done < <(find -L "$patches_path" -maxdepth 1 -name '*.patch' -type f -print0 | LC_ALL=C sort -z)

    if [[ "${#patches[@]}" -eq 0 ]]; then
        log "$name: no *.patch files in $patches_path -- nothing to apply ($label)"
        return 0
    fi

    log "$name: applying ${#patches[@]} patch(es) ($label)"
    # git am reads From: headers etc.; git format-patch output is its native input.
    # Pass patches explicitly rather than via stdin so error messages reference
    # the failing file path.
    if ! (cd "$vendor_path" && git am --keep-cr "${patches[@]}"); then
        log "$name: git am failed ($label) -- last patch left conflicts in $vendor_path"
        log "       Inspect with: (cd $vendor_path && git status)"
        log "       Abort with:   (cd $vendor_path && git am --abort)"
        return 1
    fi

    log "$name: $label applied cleanly"
}

apply_one() {
    local name="$1"
    local sha="$2"
    local vendor_path="$VENDOR_DIR/$name"
    local patches_path="$PATCHES_DIR/$name"

    if [[ ! -d "$vendor_path" ]]; then
        log "$name: vendor tree not present -- run scripts/fetch-sources.sh first"
        return 1
    fi

    # Reset to pinned SHA so patch application is idempotent.
    if [[ "$sha" != "PIN_ME" ]]; then
        if [[ "${APPLY_PATCHES_FORCE:-0}" != "1" ]]; then
            local dirty
            dirty="$(cd "$vendor_path" && git status --porcelain)"
            if [[ -n "$dirty" ]]; then
                log "$name: REFUSING to reset -- vendor/$name has uncommitted changes:"
                echo "$dirty" | sed 's/^/[apply-patches]   /' >&2
                log "$name: commit or stash them first, or set APPLY_PATCHES_FORCE=1 to discard them"
                return 1
            fi
        fi
        log "$name: resetting to pinned SHA $sha"
        (cd "$vendor_path" && git reset --hard "$sha") >/dev/null
    else
        log "$name: SHA is PIN_ME, skipping reset (using whatever fetch-sources.sh left)"
    fi

    # Abort any in-progress `git am` from a previous failed run.
    if [[ -d "$vendor_path/.git/rebase-apply" ]]; then
        log "$name: cleaning up stale .git/rebase-apply from prior run"
        (cd "$vendor_path" && git am --abort) 2>/dev/null || true
    fi

    if ! apply_patch_dir "$vendor_path" "$patches_path" "patch series" "$name"; then
        return 1
    fi

    # Record what was actually applied, so verify-patches-applied.sh can
    # later catch a patch file edited in place without being re-applied --
    # which the count comparison alone cannot see. See series-sha.sh.
    local stamp
    stamp="$(series_sha_path "$REPO_ROOT" "$name")"
    mkdir -p "${stamp%/*}"
    series_sha "$patches_path" > "$stamp"
    log "$name: recorded series fingerprint $(cut -c1-12 < "$stamp")"
}

# Walk the manifest
rc=0
while IFS= read -r line || [[ -n "$line" ]]; do
    line="${line%%#*}"
    line="${line#"${line%%[![:space:]]*}"}"
    line="${line%"${line##*[![:space:]]}"}"
    [[ -z "$line" ]] && continue

    # shellcheck disable=SC2162
    read -r name _url _ref sha <<<"$line"
    [[ -z "${name:-}" ]] && continue
    if [[ -n "$FILTER" && "$FILTER" != "$name" ]]; then
        continue
    fi

    if ! apply_one "$name" "$sha"; then
        rc=1
    fi
done < "$MANIFEST"

if [[ "$rc" -ne 0 ]]; then
    log "one or more vendors had patch failures -- see above"
    exit 1
fi
log "done."
