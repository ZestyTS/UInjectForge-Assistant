#!/bin/sh
set -eu
build=$(mktemp -d "${TMPDIR:-/tmp}/uif-ftp-tests.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -fsanitize=address,undefined \
    -Iinclude tests/assistant_ftp_tests.c source/assistant_ftp.c source/assistant_upload.c -o "$build/assistant-ftp-tests"
"$build/assistant-ftp-tests"
