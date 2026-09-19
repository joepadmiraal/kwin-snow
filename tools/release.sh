#!/usr/bin/env bash
# Prepare a Snow release by updating the checked-in version, testing it,
# committing the release metadata, and creating an annotated Git tag.
#
# Usage: tools/release.sh vX.Y.Z
# Push the commit and tag separately when the release is ready:
#   git push origin master
#   git push origin vX.Y.Z

set -euo pipefail

REPO="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO/build}"

usage() {
    sed -n '2,8p' "${BASH_SOURCE[0]}" | sed 's/^# \?//'
}

if [[ $# -ne 1 || ! "$1" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    usage >&2
    exit 2
fi

TAG="$1"
VERSION="${TAG#v}"

cd "$REPO"

if ! git diff --cached --quiet; then
    echo "release.sh: staged changes exist; commit or unstage them first" >&2
    exit 1
fi

if ! git diff --quiet -- CMakeLists.txt packaging/arch/PKGBUILD; then
    echo "release.sh: version files have uncommitted changes" >&2
    exit 1
fi

if git rev-parse --verify --quiet "refs/tags/$TAG" >/dev/null; then
    echo "release.sh: tag '$TAG' already exists" >&2
    exit 1
fi

if ! grep -Eq '^project\(kwin-effect-snow VERSION [0-9]+\.[0-9]+\.[0-9]+ LANGUAGES CXX\)$' CMakeLists.txt; then
    echo "release.sh: could not find the version in CMakeLists.txt" >&2
    exit 1
fi
if ! grep -Eq '^pkgver=[0-9]+\.[0-9]+\.[0-9]+$' packaging/arch/PKGBUILD; then
    echo "release.sh: could not find the version in packaging/arch/PKGBUILD" >&2
    exit 1
fi

sed -i -E "s/^(project\(kwin-effect-snow VERSION )[0-9]+\.[0-9]+\.[0-9]+( LANGUAGES CXX\))$/\1${VERSION}\2/" CMakeLists.txt
sed -i -E "s/^pkgver=[0-9]+\.[0-9]+\.[0-9]+$/pkgver=${VERSION}/" packaging/arch/PKGBUILD

if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    cmake -B "$BUILD_DIR" -S "$REPO" -G Ninja -DCMAKE_BUILD_TYPE=Debug
else
    cmake -B "$BUILD_DIR" -S "$REPO" -DCMAKE_BUILD_TYPE=Debug
fi
cmake --build "$BUILD_DIR"
ctest --test-dir "$BUILD_DIR" --output-on-failure

git add CMakeLists.txt packaging/arch/PKGBUILD
git commit -m "Release $TAG"
git tag -a "$TAG" -m "Release $TAG"

echo "Prepared $TAG. Push it with:"
echo "  git push origin HEAD"
echo "  git push origin $TAG"
