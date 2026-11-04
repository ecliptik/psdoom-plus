#!/usr/bin/env bash
# verify-patches-applied.sh -- pre-flight that each vendor's commit count
# matches its patches/*.patch count.
#
# Non-destructive. For each vendor in vendor/sources.manifest with a concrete
# pinned SHA and a corresponding patches/<name>/ directory, compares:
#   - count of patches/<name>/*.patch (top level; excludes _disabled/)
#   - count of commits in vendor/<name>/ since the pinned SHA
#   - CONTENT: the series fingerprint apply-patches.sh recorded when it last
#     applied, vs the series on disk now. Counts alone cannot see a patch
#     file edited in place -- see series-sha.sh for why that hazard became
#     live with per-port-vendored patches, and why this is a fingerprint
#     rather than a diff comparison.
#
# Mismatch means a patch landed in patches/<name>/ but was never `git am`'d
# to vendor/<name>/ (or vice versa: extra commits in vendor without
# corresponding patch files). Exits non-zero with concrete remediation.
#
# Catches the latent-failure-mode counterpart to the wave-38 stale-.obj
# root cause: the wave-38 patches DID `git am` to vendor (per the commit
# log timestamps), but the next time a patch lands in patches/<name>/
# without a manual `git am`, the build would silently produce a binary
# missing it -- `make patches` reorchestrates `apply-patches.sh`, this
# pre-flight verifies the resulting state matches expectations.
#
# Wired into the Makefile as a regular dependency of every build-stage
# convenience target (sdl3 / sdl3-mixer / sdl3-image / nxengine / all).
#
# Usage:
#   ./scripts/verify-patches-applied.sh

set -euo pipefail

# REPO_ROOT is the CALLER's repo root, not this script's own location --
# see the matching comment in fetch-sources.sh / apply-patches.sh.
REPO_ROOT="${SDL_DOS_PORT_ROOT:-$PWD}"
MANIFEST="$REPO_ROOT/vendor/sources.manifest"
VENDOR_DIR="$REPO_ROOT/vendor"
PATCHES_DIR="$REPO_ROOT/patches"

# readlink -f, not $0's dirname: a port invokes this through the symlink at
# its own scripts/verify-patches-applied.sh, so BASH_SOURCE is that symlink.
# shellcheck source=./series-sha.sh
source "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/series-sha.sh"

log() { printf '[verify-patches] %s\n' "$*" >&2; }

if [[ ! -f "$MANIFEST" ]]; then
    log "error: $MANIFEST not found"
    exit 1
fi

rc=0
while IFS= read -r line || [[ -n "$line" ]]; do
    line="${line%%#*}"
    line="${line#"${line%%[![:space:]]*}"}"
    line="${line%"${line##*[![:space:]]}"}"
    [[ -z "$line" ]] && continue

    # shellcheck disable=SC2162
    read -r name _url _ref sha <<<"$line"
    [[ -z "${name:-}" ]] && continue
    [[ "$sha" == "PIN_ME" ]] && continue

    vendor_path="$VENDOR_DIR/$name"
    patches_path="$PATCHES_DIR/$name"

    # No patches/<name>/ dir -> no patch series for this vendor -> nothing to verify.
    [[ -d "$patches_path" ]] || continue

    # Vendor tree must exist and be a git repo so we can count commits.
    if [[ ! -d "$vendor_path/.git" ]]; then
        log "$name: vendor tree missing or not a git repo at $vendor_path"
        log "$name:    fix: ./scripts/fetch-sources.sh"
        rc=1
        continue
    fi

    n_patches=$(find -L "$patches_path" -maxdepth 1 -name '*.patch' -type f | wc -l)
    n_applied=$(cd "$vendor_path" && git log --oneline "${sha}..HEAD" 2>/dev/null | wc -l)

    # Duplicate numeric prefix. The count comparison below CANNOT see this:
    # two files sharing a slot keep both counts equal, so the series passes
    # the gate while applying in an order the vendor stack does not match
    # (apply-patches.sh sorts lexically, so the tiebreak is the description
    # text, not intent). Found 2026-08-15 when a new patch was authored into
    # the 0302 slot that had already shipped in binary 1de88fcefd4a.
    dupes=$(find -L "$patches_path" -maxdepth 1 -name '*.patch' -type f -printf '%f\n' \
            | cut -c1-4 | sort | uniq -d)
    if [[ -n "$dupes" ]]; then
        log "$name: DUPLICATE PATCH SLOT(S) -- each numeric prefix must be unique"
        while read -r p; do
            [[ -z "$p" ]] && continue
            log "$name:    slot $p claimed by:"
            find -L "$patches_path" -maxdepth 1 -name "${p}-*.patch" -type f -printf '                     %f\n'
        done <<<"$dupes"
        log "$name:    fix: renumber the NEWER patch to a free slot and amend its"
        log "$name:         commit subject to match. Never reassign a slot that has"
        log "$name:         shipped -- the old number identifies a released binary."
        rc=1
    fi

    if [[ "$n_patches" -ne "$n_applied" ]]; then
        log "$name: MISMATCH -- $n_patches patch(es) in patches/$name/, $n_applied commit(s) since pinned SHA $sha"
        log "$name:    delta = $((n_patches - n_applied))"
        log "$name:    fix:  make patches      (re-applies the full series; idempotent)"
        log "$name:           OR (cd $vendor_path && git am path/to/missing.patch)  (incremental)"
        rc=1
        continue
    fi

    # Content gate. The count comparison above cannot see an in-place EDIT:
    # the counts still match, so the tree passes while vendor/<name>/ holds
    # something other than what patches/<name>/ describes. Compare against
    # the fingerprint apply-patches.sh recorded when it last applied this
    # series. See series-sha.sh for why this is a fingerprint rather than a
    # diff comparison.
    stamp="$(series_sha_path "$REPO_ROOT" "$name")"
    if [[ ! -f "$stamp" ]]; then
        log "$name: NO SERIES FINGERPRINT -- cannot confirm vendor/$name matches patches/$name/"
        log "$name:    This tree was last patched before the fingerprint existed, or"
        log "$name:    build/ was cleaned. The count gate passed, so this is very"
        log "$name:    likely fine -- but 'likely' is not a verification, and a build"
        log "$name:    cut from an unverified tree cannot anchor a benchmark result."
        log "$name:    fix:  make patches      (re-applies the series and records it)"
        rc=1
        continue
    fi

    want="$(cat "$stamp")"
    have="$(series_sha "$patches_path")"
    if [[ "$want" != "$have" ]]; then
        log "$name: SERIES CHANGED SINCE IT WAS APPLIED -- counts agree, contents do not"
        log "$name:    applied: $want"
        log "$name:    on disk: $have"
        log "$name:    A patch file was edited, renamed, or swapped after the last"
        log "$name:    'make patches'. vendor/$name still holds the OLD series, and a"
        log "$name:    build from here compiles those old sources while every patch"
        log "$name:    file on disk says otherwise."
        log "$name:    fix:  make patches      (re-applies the full series; idempotent)"
        rc=1
    fi
done < "$MANIFEST"

if [[ "$rc" -ne 0 ]]; then
    log "patches-applied check FAILED -- see above"
    exit 1
fi
exit 0
