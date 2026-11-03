#!/bin/sh
set -eu
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
printf '%s\n' '#include <stdbool.h>' '#include <stdint.h>' '#include <stddef.h>' '#include <stdio.h>' '#include <stdlib.h>' '#include <string.h>' '#include <strings.h>' '#include <ctype.h>' '#include <stdarg.h>' '#include "assistant_ui.h"' '#include "assistant_pairing.h"' '#include "assistant_json.h"' '#include "assistant_catalog_probe.h"' '#define UIF_COMPONENT_BUILD_ID "src-test"' > "$build/handler.c"
printf '%s\n' '#include "assistant_title_folders.h"' >> "$build/handler.c"
awk -f tests/extract-catalog-probe-functions.awk source/main.c >> "$build/handler.c"
printf '%s\n' '#include "assistant_catalog_probe_handler_tests.c"' >> "$build/handler.c"
${CC:-cc} -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude -Itests "$build/handler.c" source/assistant_json.c -o "$build/handler"
"$build/handler"
grep -q 'catalog_probe_prefix = "/api/v1/diagnostics/title-catalog/"' source/main.c
grep -q 'handle_catalog_probe(fd, state, &request, request.path + strlen(catalog_probe_prefix))' source/main.c
