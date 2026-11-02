#!/usr/bin/env bash
# Run immediately after the failure, before clearing data or reinstalling.
set -euo pipefail
case_name=${1:-snapshot}
if [[ ! "$case_name" =~ ^[A-Za-z0-9_-]+$ ]]; then
    echo 'Use a case name containing only letters, numbers, _ and -' >&2
    exit 1
fi
output_dir=${AGBPLAY_LIFECYCLE_REPORT_DIR:-"lifecycle-reports/$case_name"}
mkdir -p "$output_dir"
adb get-state >/dev/null
for property in ro.product.manufacturer ro.product.model ro.build.version.release ro.build.version.sdk; do
    adb shell getprop "$property" > "$output_dir/$property.txt" 2>&1 || true
done
adb shell dumpsys package org.agbplay.android > "$output_dir/package.txt" 2>&1 || true
adb shell dumpsys activity exit-info org.agbplay.android > "$output_dir/exit-info.txt" 2>&1 || true
adb shell dumpsys activity services org.agbplay.android > "$output_dir/services.txt" 2>&1 || true
adb shell dumpsys activity activities > "$output_dir/activities.txt" 2>&1 || true
adb shell dumpsys window > "$output_dir/windows.txt" 2>&1 || true
adb shell dumpsys power > "$output_dir/power.txt" 2>&1 || true
adb logcat -d -b crash -v threadtime > "$output_dir/crash.txt" 2>&1 || true
adb logcat -d -v threadtime 'agbplay.lifecycle:V' 'agbplay:V' 'Qt:V' 'AndroidRuntime:V' 'libc:V' 'DEBUG:V' '*:S' > "$output_dir/logcat.txt" 2>&1 || true
adb exec-out run-as org.agbplay.android cat files/last-java-crash.txt > "$output_dir/last-java-crash.txt" 2>&1 || true
printf 'Lifecycle report: %s\n' "$output_dir"
