#!/bin/sh
set -eu
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
# Only the transport sink and inactive platform context are replaced.
printf '%s\n' '#include <stdbool.h>' '#include <stdint.h>' '#include <stddef.h>' '#include <stdio.h>' '#include <stdlib.h>' '#include <string.h>' '#include <ctype.h>' '#include <stdarg.h>' '#include "assistant_ui.h"' '#include "assistant_security.h"' > "$build/receiver.c"
awk -f tests/extract-transfer-functions.awk source/main.c >> "$build/receiver.c"
printf '%s\n' '#include "assistant_transfer_results_tests.c"' >> "$build/receiver.c"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude -Itests "$build/receiver.c" source/assistant_security.c -o "$build/results"
"$build/results" "$@"
