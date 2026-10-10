#!/usr/bin/env bash
# Print the RPM version and release for the current checkout, as
#   version=0.1.57
#   release=1.g20f61e9
#
# VERSION holds "major.minor" and is bumped by hand. The third number is the
# number of commits on HEAD, so every commit gets a higher version than the
# last without anyone editing a file. A tag vX.Y.Z overrides this: the build
# is then exactly X.Y.Z-1.
set -euo pipefail
cd "$(dirname "$0")/.."

ref="${GITHUB_REF:-}"
if [[ "$ref" =~ ^refs/tags/v([0-9]+\.[0-9]+\.[0-9]+)$ ]]; then
    version="${BASH_REMATCH[1]}"
    release="1"
else
    base="$(tr -d '[:space:]' < VERSION)"
    [[ "$base" =~ ^[0-9]+\.[0-9]+$ ]] || { echo "VERSION must be major.minor, got '$base'" >&2; exit 1; }
    version="$base.$(git rev-list --count HEAD)"
    release="1.g$(git rev-parse --short=7 HEAD)"
fi
printf 'version=%s\nrelease=%s\n' "$version" "$release"
