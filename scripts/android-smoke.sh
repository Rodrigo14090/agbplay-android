#!/usr/bin/env bash
# Requires a connected ARM64 device. Does not erase app data or user documents.
set -euo pipefail
apk=${1:?Usage: android-smoke.sh path/to/agbplay.apk}
package=org.agbplay.android
adb get-state
[[ $(adb shell getprop ro.product.cpu.abilist) == *arm64-v8a* ]] || { echo 'ARM64 Android required' >&2; exit 1; }
adb install -r "$apk"
adb shell am start -W -n "$package/org.qtproject.qt.android.bindings.QtActivity"
adb shell pidof "$package"
for cycle in 1 2 3 4 5; do
    adb shell input keyevent KEYCODE_HOME
    adb shell am start -W -n "$package/org.qtproject.qt.android.bindings.QtActivity"
    adb shell pidof "$package"
done
adb shell am send-trim-memory "$package" RUNNING_LOW || true
adb shell dumpsys meminfo "$package"
echo 'Startup/background/resume smoke completed. Inspect ROM playback, rotation and sharing manually; see docs/ANDROID.md.'
