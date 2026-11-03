#!/bin/sh
set -eu
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
awk '{sub(/\r$/, "")} /^static bool mkdir_recursive\(/{copy=1} copy{print} copy && /^}/{exit}' source/assistant_telemetry.c > "$build/directory.h"
test -s "$build/directory.h"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude \
    -DUIF_TELEMETRY_DIRECTORY_FUNCTION="\"$build/directory.h\"" \
    tests/assistant_telemetry_paths_tests.c source/assistant_paths.c -o "$build/telemetry-paths"
"$build/telemetry-paths"
