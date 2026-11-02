#!/usr/bin/env bash
# Read-only snapshots for route comparisons. Run once per named playback case.
set -euo pipefail
case_name=${1:-snapshot}
if [[ ! "$case_name" =~ ^[A-Za-z0-9_-]+$ ]]; then
    echo 'Use a case name containing only letters, numbers, _ and -' >&2
    exit 1
fi
output_dir=${AGBPLAY_AUDIO_REPORT_DIR:-"audio-reports/$case_name"}
mkdir -p "$output_dir"
adb get-state >/dev/null
adb shell getprop ro.product.manufacturer > "$output_dir/manufacturer.txt"
adb shell getprop ro.product.model > "$output_dir/model.txt"
adb shell getprop ro.build.version.sdk > "$output_dir/api.txt"
# Some manufacturers restrict dumpsys; retain each error rather than inventing
# a route/session/MMAP measurement or requiring elevated device permissions.
for service in audio media.audio_flinger media.audio_policy media_session power; do
    adb shell dumpsys "$service" > "$output_dir/$service.txt" 2>&1 || true
done
adb shell dumpsys activity services org.agbplay.android > "$output_dir/playback-service.txt" 2>&1 || true
adb logcat -d -v threadtime 'agbplay:V' 'Qt:V' 'AAudio:V' 'AudioTrack:V' '*:S' > "$output_dir/logcat.txt" 2>&1 || true
printf 'Audio report: %s\n' "$output_dir"
