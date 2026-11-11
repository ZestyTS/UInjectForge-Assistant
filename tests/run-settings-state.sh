#!/bin/sh
set -eu
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
printf '%s\n' '#include <stdbool.h>' '#include <stdint.h>' '#include <stddef.h>' '#include <stdio.h>' '#include <stdlib.h>' '#include <string.h>' '#include <ctype.h>' '#include <sys/stat.h>' '#include <unistd.h>' '#include "assistant_ui.h"' '#include "assistant_pairing.h"' '#define UIF_ASSISTANT_DEFAULT_UI_SCALE 1.0f' > "$build/settings-state.c"
awk -f tests/extract-settings-functions.awk source/main.c >> "$build/settings-state.c"
printf '%s\n' '#include "assistant_settings_state_tests.c"' >> "$build/settings-state.c"
${CC:-cc} -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude -Itests "$build/settings-state.c" source/assistant_settings.c source/assistant_json.c -o "$build/settings-state"
"$build/settings-state"
