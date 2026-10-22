#!/usr/bin/env bash
# Optional x86 desktop comparison against unchanged official engine sources.
set -euo pipefail
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=${AGBPLAY_REFERENCE_BUILD_DIR:-"$project_root/build-gsf-reference"}
revision=0b87da48d2502da359e45718eec8566ac40fa9d7
mkdir -p "$build_dir"
if [[ ! -d "$build_dir/upstream" ]]; then
    git -C "$project_root" worktree add --detach "$build_dir/upstream" "$revision"
fi
test "$(git -C "$build_dir/upstream" rev-parse HEAD)" = "$revision"
test -z "$(git -C "$build_dir/upstream" status --porcelain)"
cmake -S "$project_root/src/tests/upstream-reference" -B "$build_dir/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DAGBPLAY_UPSTREAM_SOURCE="$build_dir/upstream" -DAGBPLAY_PORT_SOURCE="$project_root" "$@"
cmake --build "$build_dir/build" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-3}"
"$build_dir/build/gsf-reference" "$build_dir/fixture.gsflib"
