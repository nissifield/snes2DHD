#!/usr/bin/env bash
# Publish one immutable nightly-<sha12> prerelease. Usage: publish.sh DIST_DIR
# Only a lightweight tag on this run's GITHUB_SHA is released; nothing is
# edited, deleted or forced, and a mismatch fails before any mutation.
set -euo pipefail

dist=${1:?usage: publish.sh DIST_DIR}
: "${GITHUB_SHA:?GITHUB_SHA is required}"
: "${GH_REPO:?GH_REPO is required}"

tag="nightly-${GITHUB_SHA:0:12}"
release_url="repos/$GH_REPO/releases/tags/$tag"
tag_url="repos/$GH_REPO/git/ref/tags/$tag"
# Exactly these three assets; never a glob.
assets=(SHA256SUMS snes2dhd-linux-x86_64.tar.gz snes2dhd-source.tar.gz)
expected_assets=$(printf '%s\n' "${assets[@]}")

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat > "$work/notes.md" <<EOF
Technical preview of commit $GITHUB_SHA.

Build-only preview, not a completed playable M1: input, audio, saves and HD-2D are unfinished.

The Linux binary needs a Fedora 44-era glibc and a system SDL3; not portable.

Exact source: the snes2dhd-source.tar.gz asset below (git archive of the full commit),
https://github.com/$GH_REPO/archive/$GITHUB_SHA.tar.gz and https://github.com/$GH_REPO/tree/$GITHUB_SHA.

All previews: https://github.com/$GH_REPO/releases (not the latest link).
EOF

# Print "<object type> <sha>"; a lightweight tag reports "commit <sha>".
tag_ref() {
  gh api "$tag_url" --jq '.object.type + " " + .object.sha'
}

if gh api "$release_url" > /dev/null 2> "$work/release.err"; then
  if ! target=$(tag_ref 2> "$work/tag.err"); then
    cat "$work/tag.err" >&2
    echo "could not query tag $tag for the existing release" >&2
    exit 1
  fi
  assets_now=$(gh api "$release_url" --jq '.assets[].name' | sort)
  if [ "$target" != "commit $GITHUB_SHA" ] || [ "$assets_now" != "$expected_assets" ]; then
    echo "release $tag does not match this commit and assets; refusing to change it" >&2
    exit 1
  fi
  echo "release $tag already exists for $GITHUB_SHA; skipping"
  exit 0
fi

# Only an explicit 404 means missing; auth and transport errors fail.
if ! grep -q 'HTTP 404' "$work/release.err"; then
  cat "$work/release.err" >&2
  echo "could not query release $tag" >&2
  exit 1
fi

tag_status=0
target=$(tag_ref 2> "$work/tag.err") || tag_status=$?
create=(gh release create "$tag")
for asset in "${assets[@]}"; do
  create+=("$dist/$asset")
done
create+=(--latest=false --prerelease --title "$tag" --notes-file "$work/notes.md")
if [ "$tag_status" -eq 0 ]; then
  if [ "$target" != "commit $GITHUB_SHA" ]; then
    echo "tag $tag is '$target', not a lightweight tag on $GITHUB_SHA" >&2
    exit 1
  fi
  create+=(--verify-tag)
else
  if ! grep -q 'HTTP 404' "$work/tag.err"; then
    cat "$work/tag.err" >&2
    echo "could not query tag $tag" >&2
    exit 1
  fi
  create+=(--target "$GITHUB_SHA")
fi

"${create[@]}"

# Outside pushes can race the check above; fail loudly but delete nothing.
if ! target=$(tag_ref 2> "$work/tag.err"); then
  cat "$work/tag.err" >&2
  echo "could not re-read tag $tag after publish" >&2
  exit 1
fi
if [ "$target" != "commit $GITHUB_SHA" ]; then
  echo "tag $tag is '$target' after publish, expected commit $GITHUB_SHA" >&2
  exit 1
fi
