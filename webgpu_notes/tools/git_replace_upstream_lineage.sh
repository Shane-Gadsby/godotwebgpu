#!/usr/bin/env bash
# Reconnect this fork's re-parented history to upstream Godot's, using git replace refs.
#
# Why this is needed: see webgpu_notes/TASKS.md Phase 16. A whole-history rewrite in this
# repository re-parented 82,225 upstream Godot commits. Their content is byte-for-byte
# identical to upstream's -- same tree, author, dates and message -- but a commit's hash
# covers its parents, so every one of them has a different hash from upstream's copy, and
# `git merge-base` against godotengine/godot falls back to a commit from November 2015.
# Merging a new upstream release then wants to replay ~85,000 commits instead of ~2,800.
#
# What this does: for each "Bump version to <X>-stable" commit in this fork's history, find
# the upstream commit with an identical tree and create a `git replace` ref mapping ours to
# theirs. Git then treats them as the same commit, and merge-base lands on the real release
# boundary. Nothing is rewritten: replace refs are additive, local, and removed with
# `git replace -d <sha>`.
#
# Pairs are matched on tree equality, not on a hardcoded list, so this stays correct as more
# releases are merged and refuses to invent a mapping it cannot verify.
#
# Usage:   ./webgpu_notes/tools/git_replace_upstream_lineage.sh [--dry-run | --delete]
#
# Note: `refs/replace/*` is NOT fetched or pushed by default, and pushing it to this
# repository is currently refused (GitHub returns 403 on receive-pack for that namespace from
# a scoped token). So run this in each clone rather than expecting to fetch the refs. If a
# push ever succeeds, others can pick them up with:
#     git fetch origin '+refs/replace/*:refs/replace/*'

set -euo pipefail

MODE=${1:-apply}

if ! git rev-parse --git-dir >/dev/null 2>&1; then
    echo "error: not inside a git repository" >&2
    exit 1
fi

# Upstream must be present; add it if the remote is missing but do not fetch silently.
if ! git remote get-url upstream >/dev/null 2>&1; then
    echo "error: no 'upstream' remote. Add it with:" >&2
    echo "    git remote add upstream https://github.com/godotengine/godot.git" >&2
    exit 1
fi

# Release bumps live on upstream's release branches as well as master, so consider both.
UPSTREAM_REFS=()
for r in upstream/master upstream/4.7 upstream/4.8 upstream/4.6; do
    git rev-parse --verify --quiet "$r" >/dev/null && UPSTREAM_REFS+=("$r")
done
if [ ${#UPSTREAM_REFS[@]} -eq 0 ]; then
    echo "error: no upstream branches fetched. Try:" >&2
    echo "    git fetch upstream '+refs/heads/*:refs/remotes/upstream/*'" >&2
    exit 1
fi

if [ "$MODE" = "--delete" ]; then
    n=0
    for r in $(git for-each-ref refs/replace --format='%(refname:lstrip=2)'); do
        git replace -d "$r" >/dev/null && n=$((n + 1))
    done
    echo "deleted $n replace ref(s)"
    exit 0
fi

# Key on tree AND author timestamp AND subject. Tree alone is not unique -- a release bump
# and its backport onto a release branch can share a tree -- and mapping to the wrong twin
# would silently graft the fork onto the wrong lineage.
declare -A UP_BY_KEY
while IFS=$'\t' read -r sha tree at subject; do
    key="$tree|$at|$subject"
    [ -n "${UP_BY_KEY[$key]:-}" ] || UP_BY_KEY[$key]=$sha
done < <(git --no-replace-objects log "${UPSTREAM_REFS[@]}" --format='%H%x09%T%x09%at%x09%s')

# `git merge-base --is-ancestor` takes exactly two arguments, so test each upstream ref.
in_upstream() {
    local sha=$1 r
    for r in "${UPSTREAM_REFS[@]}"; do
        git --no-replace-objects merge-base --is-ancestor "$sha" "$r" 2>/dev/null && return 0
    done
    return 1
}

created=0
skipped=0
while IFS=$'\t' read -r sha tree at subject; do
    # Already upstream's own hash -- nothing to map.
    if in_upstream "$sha"; then
        continue
    fi
    up=${UP_BY_KEY["$tree|$at|$subject"]:-}
    if [ -z "$up" ]; then
        echo "  skip  ${sha:0:10}  no upstream commit with the same tree+date+subject  | $subject"
        skipped=$((skipped + 1))
        continue
    fi
    # Never map a commit to itself.
    if [ "$up" = "$sha" ]; then
        continue
    fi
    if [ "$(git for-each-ref "refs/replace/$sha" --format='%(objectname)')" = "$up" ]; then
        continue
    fi
    if [ "$MODE" = "--dry-run" ]; then
        echo "  would map  ${sha:0:10} -> ${up:0:10}  | $subject"
    else
        git replace -f "$sha" "$up"
        echo "  mapped     ${sha:0:10} -> ${up:0:10}  | $subject"
    fi
    created=$((created + 1))
done < <(git --no-replace-objects log HEAD --format='%H%x09%T%x09%at%x09%s' \
         | grep -P '\tBump version to [0-9.]+-stable' || true)

echo
echo "replace refs created/updated: $created   unmatched: $skipped"
if [ "$MODE" != "--dry-run" ]; then
    for ref in HEAD; do
        mb=$(git merge-base "$ref" upstream/master 2>/dev/null) || continue
        echo "merge-base($ref, upstream/master) is now: $(git log -1 --format='%h %ad %s' --date=short "$mb")"
    done
fi
