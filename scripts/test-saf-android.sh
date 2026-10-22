#!/usr/bin/env bash
set -euo pipefail
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=${AGBPLAY_SAF_TEST_DIR:-"$project_root/build-saf-tests"}
adb install -r "$build_dir/saf-tests.apk"
adb shell am instrument -w org.agbplay.saftest/android.test.InstrumentationTestRunner | tee "$build_dir/result.log"
grep -q 'OK (5 tests)' "$build_dir/result.log"
