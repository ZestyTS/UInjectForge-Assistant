#!/bin/sh
set -eu
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
awk '
    { sub(/\r$/, "") }
    /^static (int|void) (read_runtime_apd|write_runtime_apd|update_power_guard)\(/ { active = 1; count++ }
    active { print }
    active && /^}/ { active = 0 }
    END { if (count != 3 || active) exit 1 }
' source/main.c > "$build/power-wiring.inc"
awk '
    { sub(/\r$/, "") }
    /state->transfer = transfer;/ {
        getline; sub(/\r$/, ""); if ($0 !~ /update_power_guard\(state, false\)/) exit 1; transfer++
    }
    /state->install_active = true;/ {
        getline; sub(/\r$/, ""); if ($0 !~ /update_power_guard\(state, false\)/) exit 1; install++
    }
    /update_power_guard\(&state, false\)/ { loop++ }
    /update_power_guard\(&state, true\)/ {
        getline; sub(/\r$/, ""); if ($0 !~ /WHBProcShutdown\(\)/) exit 1; cleanup++
    }
    END { if (transfer != 1 || install != 1 || loop != 2 || cleanup != 1) exit 1 }
' source/main.c
${CC:-cc} -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude -Itests -I"$build" tests/assistant_power_wiring_tests.c source/assistant_power.c -o "$build/power-wiring"
"$build/power-wiring"
