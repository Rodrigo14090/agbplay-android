#!/usr/bin/env bash
set -euo pipefail
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=${AGBPLAY_BUILD_DIR:-"$project_root/build-test"}
cmake -S "$project_root" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DAGBPLAY_BUILD_GUI=OFF -DAGBPLAY_BUILD_CLI=OFF -DAGBPLAY_BUILD_NC=OFF \
    -DBUILD_TESTING=ON -DAGBPLAY_ENABLE_AVX2=OFF "$@"
cmake --build "$build_dir" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-3}"
ctest --test-dir "$build_dir" --output-on-failure
