#!/usr/bin/env bash
# Alternative to the official Qt installer. Builds only the modules needed here.
set -euo pipefail
: "${ANDROID_SDK_ROOT:?Set Android SDK directory}"
: "${ANDROID_NDK_ROOT:?Set NDK 27.2.12479018 directory}"
: "${JAVA_HOME:?Set JDK 21 directory}"
work=${QT_SOURCE_WORK:-"${TMPDIR:-/tmp}/agbplay-qt-toolchain"}
version=6.11.2
mkdir -p "$work/src"
work=$(cd "$work" && pwd)
for module in qtbase qtmultimedia; do
    if [[ ! -f "$work/src/$module-$version/CMakeLists.txt" ]]; then
        curl -fL "https://codeload.github.com/qt/$module/tar.gz/refs/tags/v$version" |
            tar -xz -C "$work/src"
    fi
done
cmake -S "$work/src/qtbase-$version" -B "$work/build-host" -G Ninja \
    -DCMAKE_INSTALL_PREFIX="$work/host" -DCMAKE_BUILD_TYPE=Release \
    -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF \
    -DFEATURE_gui=OFF -DFEATURE_widgets=OFF -DFEATURE_icu=OFF -DFEATURE_dbus=OFF \
    -DFEATURE_sql=OFF -DFEATURE_testlib=OFF -DFEATURE_network=OFF
cmake --build "$work/build-host" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-3}"
cmake --install "$work/build-host"
cmake -S "$work/src/qtbase-$version" -B "$work/build-android" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON \
    -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" \
    -DQT_HOST_PATH="$work/host" -DCMAKE_INSTALL_PREFIX="$work/android" \
    -DCMAKE_BUILD_TYPE=Release -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF \
    -DFEATURE_sql=OFF -DFEATURE_testlib=OFF -DFEATURE_dbus=OFF -DFEATURE_icu=OFF -DFEATURE_openssl=OFF
cmake --build "$work/build-android" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-3}"
cmake --install "$work/build-android"
mkdir -p "$work/build-multimedia"
(cd "$work/build-multimedia" && "$work/android/bin/qt-configure-module" "$work/src/qtmultimedia-$version" -- -DFEATURE_ffmpeg=OFF -DFEATURE_gstreamer=OFF \
    -DFEATURE_spatialaudio=OFF -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF)
cmake --build "$work/build-multimedia" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-3}"
cmake --install "$work/build-multimedia"
echo "QT_ANDROID_PREFIX=$work/android"
echo "QT_HOST_PATH=$work/host"
