#!/bin/sh
#
# make-patch.sh
#
# Generate a single-commit patch file for a driver topic branch,
# with sanity checks for the things reviewers most commonly
# reject.  Companion to Chapter 37 of "FreeBSD Device Drivers:
# From First Steps to Kernel Mastery."
#
# The script assumes you have committed your driver as a single
# commit on a topic branch that was created off of origin/main.
#
# Usage:
#   cd /usr/src
#   /path/to/make-patch.sh [output-directory]
#
# The default output directory is the current directory.  The
# generated patch file will be named something like
#   0001-mydev-Add-driver-for-FooCorp-FC100-sensor-boards.patch
#
# Exit code 0 means the patch was generated and passed the
# sanity checks.  Non-zero means a check failed.

set -e

OUTDIR=${1:-.}

# ----- helpers -----

stage() {
	printf '\n=== %s ===\n' "$1"
}

fail() {
	printf '\n*** FAIL: %s ***\n' "$1" >&2
	exit 1
}

# ----- sanity checks -----

stage "sanity checks"

command -v git >/dev/null 2>&1 || fail "git is not installed"

[ -d .git ] || fail "not a git repository (run from the top of the tree)"

BRANCH=$(git rev-parse --abbrev-ref HEAD)
if [ "$BRANCH" = "main" ] || [ "$BRANCH" = "master" ]; then
	fail "on $BRANCH branch; patches must be generated from a topic branch"
fi

echo "branch: $BRANCH"

# Verify the branch diverges from origin/main by exactly one commit.
BASE=$(git merge-base HEAD origin/main 2>/dev/null) \
	|| fail "cannot find merge base with origin/main"

AHEAD=$(git rev-list --count "${BASE}..HEAD")
if [ "$AHEAD" -eq 0 ]; then
	fail "branch has no commits ahead of origin/main"
fi
if [ "$AHEAD" -gt 1 ]; then
	printf 'branch has %s commits ahead of origin/main.\n' "$AHEAD" >&2
	printf 'a driver submission should be a single commit.\n' >&2
	printf 'consider: git rebase -i origin/main\n' >&2
	fail "multiple commits; squash before submitting"
fi

echo "branch is exactly 1 commit ahead of origin/main."

# ----- commit-message checks -----

stage "commit-message checks"

SUBJECT=$(git log -1 --pretty=%s HEAD)
BODY=$(git log -1 --pretty=%b HEAD)

echo "subject: $SUBJECT"

# Subject should be under 72 characters.
if [ "$(printf '%s' "$SUBJECT" | wc -c)" -gt 72 ]; then
	fail "subject line exceeds 72 characters"
fi

# Subject should follow subsystem: description form.
case "$SUBJECT" in
	*:\ *) ;;
	*) fail "subject does not follow 'subsystem: description' form" ;;
esac

# Body should wrap at 72 columns.
if printf '%s' "$BODY" | awk '{ if (length($0) > 72) exit 1 }'; then
	:
else
	fail "commit-message body has a line longer than 72 columns"
fi

# Look for Signed-off-by.
if ! printf '%s' "$BODY" | grep -q '^Signed-off-by: '; then
	printf 'warning: no Signed-off-by line in commit body.\n' >&2
	printf 'consider: git commit --amend -s\n' >&2
fi

echo "commit message looks well-formed."

# ----- working-tree checks -----

stage "working-tree checks"

if [ -n "$(git status --porcelain)" ]; then
	fail "working tree is dirty; commit or stash before generating patch"
fi

# Trailing whitespace in the diff.
if git show HEAD | grep -nE '\^I$|[ 	]+$' >/dev/null; then
	git show HEAD | grep -nE '^\+.*[ 	]+$' || true
	fail "diff contains trailing whitespace"
fi

echo "working tree is clean and diff has no trailing whitespace."

# ----- generate the patch -----

stage "generating patch"

mkdir -p "$OUTDIR"
PATCH_FILE=$(git format-patch -1 HEAD -o "$OUTDIR" | tail -n 1)

if [ -z "$PATCH_FILE" ] || [ ! -f "$PATCH_FILE" ]; then
	fail "git format-patch did not produce a file"
fi

echo "patch file: $PATCH_FILE"

# ----- verify the patch applies cleanly -----

stage "verifying the patch applies cleanly"

# Use a scratch worktree rooted at origin/main to test application.
WORKDIR=$(mktemp -d)
trap 'rm -rf "$WORKDIR"' EXIT

git worktree add --detach "$WORKDIR" origin/main >/dev/null
(
	cd "$WORKDIR" || fail "cannot cd to scratch worktree"
	git am --abort >/dev/null 2>&1 || true
	git am "$PATCH_FILE" >/dev/null
)
git worktree remove --force "$WORKDIR"

echo "patch applies cleanly to a fresh origin/main tree."

# ----- done -----

stage "patch is ready for submission"
echo "file: $PATCH_FILE"
echo "next: open a Phabricator review or a GitHub PR."
