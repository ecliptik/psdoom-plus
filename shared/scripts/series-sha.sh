#!/usr/bin/env bash
# series-sha.sh -- content fingerprint of a patch series. Sourced (not run)
# by apply-patches.sh and verify-patches-applied.sh.
#
# WHY THIS EXISTS
#
# verify-patches-applied.sh's original gate compared COUNTS: number of
# patches/<name>/*.patch vs number of vendor commits since the pin. That is
# blind to an in-place EDIT -- change a patch file without adding or removing
# one and both counts still match, so the gate passes while vendor/<name>/
# does not contain what patches/<name>/ says it does.
#
# That went from theoretical to live with the 2026-09-01 per-port-vendoring
# migration. A port's patch series used to be a symlink into the hub's shared
# tree, which nobody edited casually; it is now an ordinary editable file in
# the port's own repo, which is the whole point of the migration. Worse, the
# build's dependency graph now (correctly, since db81f67) keys on those files,
# so an in-place edit DOES trigger a rebuild -- of the un-re-applied vendor
# tree. Green build, wrong sources, and nothing on screen to say so.
#
# WHY A FINGERPRINT RATHER THAN COMPARING DIFFS
#
# The obvious check -- compare each patch file's `git patch-id` against the
# patch-id of the vendor commit in the same position -- does not work. Tried
# and rejected 2026-09-02: patch-id hashes the normalized diff, but it is
# still sensitive to how the diff was GENERATED. dossage's own series has
# patches written with zero context lines (`@@ -205,2 +205,44 @@`) sitting
# beside a vendor tree whose `git show` emits the default three (`@@ -203,6
# +203,48 @@`). Same change, same resulting tree, different patch-id. On a
# known-good tree that check reported two false CONTENT MISMATCHes. Any
# diff-shaped comparison inherits that fragility.
#
# So fingerprint the SERIES ITSELF instead, and record what was applied at
# apply time. Representation-independent: it answers "are these the same
# bytes that were last applied?", which is exactly the hazard, and nothing
# about how a patch was formatted can perturb it.
#
# The stamp deliberately lives under build/, NOT inside vendor/<name>/. A
# stamp file written into the vendor tree would show up as untracked in
# `git status --porcelain`, which is precisely what apply-patches.sh's own
# refuse-if-dirty guard keys on -- it would make the next run refuse to
# reset a tree that its own previous run dirtied.

# series_sha <patches_dir>
#   Prints a sha256 over every top-level *.patch in <patches_dir>, in the
#   same LC_ALL=C order apply-patches.sh applies them, with each filename
#   folded in so a pure rename (which changes apply order) is caught too.
#   Prints "no-series" when the directory does not exist.
series_sha() {
    local dir="$1"
    if [[ ! -d "$dir" ]]; then
        printf 'no-series\n'
        return 0
    fi
    {
        local p
        while IFS= read -r -d '' p; do
            printf '%s\0' "${p##*/}"
            cat "$p"
            printf '\0'
        done < <(find -L "$dir" -maxdepth 1 -name '*.patch' -type f -print0 \
                 | LC_ALL=C sort -z)
    } | _series_sha_hash
}

# series_sha_path <repo_root> <vendor_name>
#   Where the stamp for one vendor lives.
series_sha_path() {
    printf '%s/build/patch-series/%s.sha\n' "$1" "$2"
}

_series_sha_hash() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum | cut -d' ' -f1
    else
        # macOS / BSD
        shasum -a 256 | cut -d' ' -f1
    fi
}
