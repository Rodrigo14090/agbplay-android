#!/usr/bin/env bash
set -euo pipefail
: "${QT_ANDROID_PREFIX:?Set to Qt 6.11+ android_arm64_v8a installation}"
: "${ANDROID_SDK_ROOT:?Set to Android SDK directory}"
: "${ANDROID_NDK_ROOT:?Set to Android NDK 27.2.12479018 directory}"
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=${AGBPLAY_BUILD_DIR:-"$project_root/build-android"}
args=()
if [[ -n ${QT_HOST_PATH:-} ]]; then args+=(-DQT_HOST_PATH="$QT_HOST_PATH"); fi
"$QT_ANDROID_PREFIX/bin/qt-cmake" -S "$project_root" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DQT_ANDROID_DEPLOYMENT_TYPE=Debug -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON \
    -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" \
    -DAGBPLAY_FETCH_DEPENDENCIES=ON -DAGBPLAY_QT_AUDIO=ON \
    -DAGBPLAY_BUILD_CLI=OFF -DAGBPLAY_BUILD_NC=OFF -DBUILD_TESTING=OFF \
    "${args[@]}" "$@"
cmake --build "$build_dir" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-3}"
cmake --build "$build_dir" --target apk
apk=$(find "$build_dir" -path '*/outputs/apk/debug/*-debug.apk' -print -quit)
if [[ -n "$apk" ]]; then
    mkdir -p "$build_dir/artifacts"
    cp "$apk" "$build_dir/artifacts/agbplay-android-arm64-debug.apk"
    echo "$build_dir/artifacts/agbplay-android-arm64-debug.apk"
else
    echo 'Expected signed development APK was not generated' >&2
    exit 1
fi
