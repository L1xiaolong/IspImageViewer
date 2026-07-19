#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: ./scripts/release.sh VERSION

Creates a release commit and a matching vVERSION tag on main, then pushes both
to GitHub. The tag triggers the GitHub Release workflow.

Example:
  ./scripts/release.sh 0.2.3
EOF
}

if [[ $# -eq 1 && ( "$1" == "-h" || "$1" == "--help" ) ]]; then
    usage
    exit 0
fi

if [[ $# -ne 1 ]]; then
    usage >&2
    exit 2
fi

version="$1"
if [[ ! "$version" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]; then
    echo "VERSION must use MAJOR.MINOR.PATCH form, for example 0.2.3." >&2
    exit 2
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd -- "$script_dir/.." && pwd)"
cd "$repo_dir"

if [[ -n "$(git status --porcelain)" ]]; then
    echo "Working tree is not clean. Commit or stash changes before releasing." >&2
    exit 2
fi

branch="$(git branch --show-current)"
if [[ "$branch" != "main" ]]; then
    echo "Releases must be created from main; current branch is $branch." >&2
    exit 2
fi

git pull --ff-only origin main

tag="v$version"
if git rev-parse -q --verify "refs/tags/$tag" >/dev/null; then
    echo "Tag $tag already exists." >&2
    exit 2
fi

ISPVIEW_RELEASE_VERSION="$version" perl -0pi -e \
    's/(project\(ISPImageViewer VERSION )[^ ]+( LANGUAGES CXX\))/$1$ENV{ISPVIEW_RELEASE_VERSION}$2/' \
    CMakeLists.txt

if ! grep -Fq "project(ISPImageViewer VERSION $version LANGUAGES CXX)" CMakeLists.txt; then
    echo "Could not update the project version in CMakeLists.txt." >&2
    exit 1
fi

git diff --check
git add CMakeLists.txt
git commit -m "chore: release $tag"
git push origin main

git tag -a "$tag" -m "Release $tag"
git push origin "$tag"

echo "Published $tag. Track the package build at GitHub Actions → Release."
