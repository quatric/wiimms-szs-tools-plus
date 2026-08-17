#!/usr/bin/env bash
# Generates a unified diff of this fork's project/ tree against a clone of
# upstream Wiimm/wiimms-szs-tools, for reference / potential upstreaming.
#
# Upstream has no tags and no shared git history with this fork (this repo
# started as a single-commit import that already carried large platform
# additions -- BFRES/BNTX, NSBMD, ASTC, the RSA/WC24 crypto, etc. -- that
# don't exist upstream). So this is a plain content diff, not `git
# format-patch`: it won't apply cleanly with `git am`/`patch -p1` against
# upstream's tree since large parts of it are new files/subsystems, but it
# gives a complete, reviewable record of how far the fork has drifted and
# where the two trees still line up.
#
# Usage: scripts/gen-upstream-patch.sh [upstream-ref]
#   upstream-ref: branch/tag/commit to diff against (default: master)
#
# Output: patches/vs-upstream.patch (one full-tree unified diff)

set -euo pipefail

UPSTREAM_URL="https://github.com/Wiimm/wiimms-szs-tools.git"
UPSTREAM_REF="${1:-master}"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="$REPO_ROOT/patches"
OUT_FILE="$OUT_DIR/vs-upstream.patch"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "Cloning upstream ($UPSTREAM_REF)..." >&2
git clone --quiet --depth 50 --branch "$UPSTREAM_REF" "$UPSTREAM_URL" "$WORK/upstream" \
	|| git clone --quiet "$UPSTREAM_URL" "$WORK/upstream"
UPSTREAM_COMMIT="$(git -C "$WORK/upstream" rev-parse HEAD)"

mkdir -p "$OUT_DIR"

{
	echo "# Diff of $REPO_ROOT/project vs upstream Wiimm/wiimms-szs-tools"
	echo "# Upstream ref: $UPSTREAM_REF ($UPSTREAM_COMMIT)"
	echo "# Generated: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "#"
	echo "# NOTE: upstream shares no git history with this fork and this diff"
	echo "# covers large subsystems (BFRES/BNTX, NSBMD, ASTC, RSA/WC24 crypto,"
	echo "# ...) that don't exist upstream. It will not apply cleanly with"
	echo "# git am or patch -p1 -- treat it as a reference diff, not a"
	echo "# ready-to-apply patch series."
	echo
	diff -ruN \
		--exclude=.git \
		"$WORK/upstream/project" \
		"$REPO_ROOT/project" \
		|| true
} > "$OUT_FILE"

echo "Wrote $OUT_FILE ($(wc -l < "$OUT_FILE") lines)" >&2
