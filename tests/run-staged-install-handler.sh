#!/bin/sh
set -eu
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
printf '%s\n' '#include <stdbool.h>' '#include <stdint.h>' '#include <stddef.h>' '#include <stdio.h>' '#include <stdlib.h>' '#include <string.h>' '#include <strings.h>' '#include <errno.h>' '#include <sys/stat.h>' '#include "assistant_ui.h"' '#include "assistant_pairing.h"' '#include "assistant_staged_install.h"' '#include "assistant_storage.h"' '#include "assistant_policy.h"' > "$build/handler.c"
printf '%s\n' '#include "assistant_title_folders.h"' >> "$build/handler.c"
awk -f tests/extract-staged-install-functions.awk source/main.c >> "$build/handler.c"
printf '%s\n' '#include "assistant_staged_install_handler_tests.c"' >> "$build/handler.c"
${CC:-cc} -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude -Itests "$build/handler.c" source/assistant_staged_install.c source/assistant_json.c source/assistant_policy.c -lmbedcrypto -o "$build/handler"
"$build/handler"
# Check the endpoint and late identity validation remain wired into production.
grep -q 'strcmp(request.path, "/api/v1/install-packages/install")' source/main.c
grep -q 'validate_staged_install(fs_package_path, &g_staged_install' source/main.c
