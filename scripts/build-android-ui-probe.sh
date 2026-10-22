#!/usr/bin/env bash
set -euo pipefail
: "${QT_ANDROID_PREFIX:?Set Qt 6.11.2 Android x86_64 prefix}"
: "${QT_HOST_PATH:?Set matching Qt host prefix}"
: "${ANDROID_SDK_ROOT:?Set Android SDK directory}"
: "${ANDROID_NDK_ROOT:?Set Android NDK directory}"
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=${AGBPLAY_UI_PROBE_BUILD_DIR:-"$project_root/build-android-ui"}
"$QT_ANDROID_PREFIX/bin/qt-cmake" -S "$project_root" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DQT_ANDROID_DEPLOYMENT_TYPE=Debug \
    -DQT_USE_TARGET_ANDROID_BUILD_DIR=TRUE \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DANDROID_ABI=x86_64 -DANDROID_PLATFORM=android-28 \
    -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" \
    -DQT_HOST_PATH="$QT_HOST_PATH" -DAGBPLAY_FETCH_DEPENDENCIES=ON \
    -DAGBPLAY_QT_AUDIO=ON -DAGBPLAY_ANDROID_UI_PROBE=ON \
    -DAGBPLAY_BUILD_CLI=OFF -DAGBPLAY_BUILD_NC=OFF -DBUILD_TESTING=OFF
cmake --build "$build_dir" --target android-ui-probe_make_apk --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-3}"
mkdir -p "$build_dir/artifacts"
apk=$(find "$build_dir" -path '*android-build-android-ui-probe*/outputs/apk/debug/*-debug.apk' -print -quit)
test -n "$apk"
cp "$apk" "$build_dir/artifacts/ui-probe-x86_64.apk"
