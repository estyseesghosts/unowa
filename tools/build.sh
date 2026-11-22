#!/bin/sh
# Build one unowa profile under a per-tree lock.
#
#   tools/build.sh 0 0 [make args...]   Black, release   -> build/black/unowa-black.3dsx
#   tools/build.sh 0 1 [make args...]   Black, profile   -> build/black/unowa-black-dbg.3dsx
#   tools/build.sh 1 0 [make args...]   White, release
#   tools/build.sh 1 1 [make args...]   White, profile
#
# A tree is locked with mkdir, so two concurrent builds of the same tree fail
# at once instead of mixing objects. Release and profile lock separately.
set -eu
edition=${1:?usage: tools/build.sh <edition 0|1> <profile 0|1> [make args...]}
profile=${2:?usage: tools/build.sh <edition 0|1> <profile 0|1> [make args...]}
shift 2
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
if [ "$edition" = 0 ]; then name=black; else name=white; fi
if [ "$profile" = 1 ]; then suffix=-prof; else suffix=; fi
tree="$root/build/$name/3ds$suffix"
lock="$tree/.build.lock"
mkdir -p "$tree"
if ! mkdir "$lock" 2>/dev/null; then
    echo "build tree $tree is locked by another build" >&2
    exit 1
fi
cleanup() { rmdir "$lock" 2>/dev/null || true; }
trap cleanup EXIT HUP INT TERM
make EDITION="$edition" PROFILE="$profile" program "$@"
status=$?
cleanup
trap - EXIT HUP INT TERM
exit $status
