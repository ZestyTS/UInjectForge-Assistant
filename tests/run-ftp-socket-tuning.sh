#!/bin/sh
set -eu
build=$(mktemp -d "${TMPDIR:-/tmp}/uif-ftp-socket-tuning.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM
# Rename only the engine's socket-option symbols. Test wrappers forward to real
# TCP sockets except an explicitly injected option failure. No production seam.
${CC:-cc} -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -fsanitize=address,undefined \
    -Dsetsockopt=assistant_test_setsockopt -Dgetsockopt=assistant_test_getsockopt \
    -Iinclude -c source/assistant_ftp.c -o "$build/assistant-ftp.o"
${CC:-cc} -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -fsanitize=address,undefined \
    -Iinclude -Itests tests/assistant_ftp_socket_tuning_tests.c source/assistant_upload.c \
    "$build/assistant-ftp.o" -o "$build/assistant-ftp-socket-tuning"
"$build/assistant-ftp-socket-tuning"
