#!/bin/bash
set -euo pipefail

tag="${1:?usage: publish-release.sh <tag> <asset>...}"
shift
[ "$#" -gt 0 ] || { echo "::error::no assets for $tag"; exit 1; }

is_draft="$(gh release view "$tag" --json isDraft --jq .isDraft --repo "$GITHUB_REPOSITORY")"
if [ "$is_draft" != "true" ]; then
  echo "::error::$tag is already published; its assets are never replaced"
  exit 1
fi

gh release upload "$tag" "$@" --clobber --repo "$GITHUB_REPOSITORY"
gh release edit "$tag" --draft=false --repo "$GITHUB_REPOSITORY"
