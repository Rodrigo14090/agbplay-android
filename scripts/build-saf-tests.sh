#!/usr/bin/env bash
# Builds a small platform instrumentation APK; it contains only synthetic data.
set -euo pipefail
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
: "${ANDROID_SDK_ROOT:?Set ANDROID_SDK_ROOT}"
: "${JAVA_HOME:?Set JAVA_HOME to JDK 17+}"
build_dir=${AGBPLAY_SAF_TEST_DIR:-"$project_root/build-saf-tests"}
tools="$ANDROID_SDK_ROOT/build-tools/35.0.0"
platform="$ANDROID_SDK_ROOT/platforms/android-35"
mkdir -p "$build_dir/classes" "$build_dir/dex"
test_sources=("$project_root"/src/tests/android/org/agbplay/saftest/*.java)
"$JAVA_HOME/bin/javac" --release 8 \
    -cp "$platform/android.jar:$platform/optional/android.test.runner.jar:$platform/optional/android.test.base.jar" \
    -d "$build_dir/classes" \
    "$project_root/android/src/org/agbplay/android/DocumentCopy.java" \
    "$project_root/android/src/org/agbplay/android/DocumentImporter.java" "${test_sources[@]}"
"$JAVA_HOME/bin/jar" cf "$build_dir/tests.jar" -C "$build_dir/classes" .
JAVA_HOME="$JAVA_HOME" "$tools/d8" --min-api 28 --lib "$platform/android.jar" \
    --classpath "$platform/optional/android.test.runner.jar" \
    --classpath "$platform/optional/android.test.base.jar" \
    --output "$build_dir/dex" "$build_dir/tests.jar"
"$tools/aapt2" link --manifest "$project_root/src/tests/android/AndroidManifest.xml" \
    -I "$platform/android.jar" -o "$build_dir/unaligned.apk"
zip -q -j "$build_dir/unaligned.apk" "$build_dir/dex/classes.dex"
"$tools/zipalign" -f 4 "$build_dir/unaligned.apk" "$build_dir/saf-tests.apk"
if [[ ! -e "$build_dir/debug.keystore" ]]; then
    "$JAVA_HOME/bin/keytool" -genkeypair -keystore "$build_dir/debug.keystore" \
        -storepass android -keypass android -alias androiddebugkey -keyalg RSA \
        -dname 'CN=agbplay SAF tests' -validity 30
fi
JAVA_HOME="$JAVA_HOME" "$tools/apksigner" sign --ks "$build_dir/debug.keystore" \
    --ks-pass pass:android --key-pass pass:android "$build_dir/saf-tests.apk"
echo "$build_dir/saf-tests.apk"
