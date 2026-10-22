#!/usr/bin/env bash
set -euo pipefail
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=${AGBPLAY_DOCUMENT_TEST_DIR:-"$project_root/build-document-tests"}
mkdir -p "$build_dir/classes"
java_bin=${JAVA_HOME:+"$JAVA_HOME/bin/"}
"${java_bin}javac" -d "$build_dir/classes" \
    "$project_root/android/src/org/agbplay/android/DocumentCopy.java" \
    "$project_root/src/tests/java/TestDocumentCopy.java"
"${java_bin}java" -ea -cp "$build_dir/classes" TestDocumentCopy
