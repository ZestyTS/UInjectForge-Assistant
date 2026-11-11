#!/bin/sh
set -eu
build=$(mktemp -d "${TMPDIR:-/tmp}/uif-ftp-integration.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM
cat > "$build/integration.c" <<'EOF'
#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <mbedtls/sha256.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "assistant_ui.h"
#include "assistant_pairing.h"
#include "assistant_json.h"
#include "assistant_paths.h"
#include "assistant_policy.h"
#include "assistant_security.h"
#include "assistant_install.h"
#include "assistant_telemetry.h"
#include "assistant_upload.h"
#include "assistant_ftp.h"
EOF
awk -f tests/extract-ftp-integration-functions.awk source/main.c >> "$build/integration.c"
printf '%s\n' '#include "assistant_ftp_integration_tests.c"' >> "$build/integration.c"
${CC:-cc} -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -fsanitize=address,undefined \
    -Iinclude -Itests "$build/integration.c" source/assistant_ftp.c source/assistant_upload.c \
    source/assistant_json.c source/assistant_paths.c source/assistant_policy.c source/assistant_install.c \
    source/assistant_security.c source/assistant_pairing.c -lmbedcrypto -o "$build/assistant-ftp-integration"
"$build/assistant-ftp-integration" "$build"
