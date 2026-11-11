#!/bin/sh
set -eu
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
printf '%s\n' '#include <stdbool.h>' '#include <stdint.h>' '#include <stddef.h>' '#include <stdio.h>' '#include <stdlib.h>' '#include <string.h>' '#include <strings.h>' '#include <errno.h>' '#include <sys/stat.h>' '#include <sys/socket.h>' '#include <sys/select.h>' '#include <unistd.h>' '#include <mbedtls/sha256.h>' '#include "assistant_ui.h"' '#include "assistant_pairing.h"' '#include "assistant_upload.h"' > "$build/upload-receiver.c"
awk -f tests/extract-upload-functions.awk source/main.c >> "$build/upload-receiver.c"
printf '%s\n' '#undef fopen' '#include "assistant_upload_receiver_tests.c"' >> "$build/upload-receiver.c"
${CC:-cc} -UNDEBUG -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude -Itests "$build/upload-receiver.c" source/assistant_upload.c -lmbedcrypto -pthread -o "$build/upload-receiver"
"$build/upload-receiver"
