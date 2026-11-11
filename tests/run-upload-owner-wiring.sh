#!/bin/sh
set -eu
# Pair the behavioral clock/socket tests with the production owner loop: pacing
# platform work must never move the bounded data pump or HTTP cancellation poll
# underneath that cadence. No platform or console execution is needed here.
awk '
    { sub(/\r$/, "") }
    /^    while \(!g_procui_exit_requested\)/ { active = 1; loops++ }
    active {
        if (/assistant_upload_owner_control_due\(&ftp_owner_pump,/) { if (depth != 1) exit 1; due++ }
        if (/if \(owner_control_due && !WHBProcIsRunning\(\)\)/) { if (depth != 1) exit 1; procui++ }
        if (/if \(owner_control_due\) assistant_ui_poll_input\(\)/) input++
        if (/if \(owner_control_due && initialized\) tick_install\(&state\)/) install++
        if (/assistant_upload_owner_control_complete\(&ftp_owner_pump,/) { completed = NR; complete++ }
        if (/if \(initialized && state.ftp_server\)/) { if (depth != 1) exit 1; ftp_guard = NR; ftp++ }
        if (/assistant_ftp_pump\(state.ftp_server, OSTicksToMilliseconds\(OSGetTime\(\)\), UIF_UPLOAD_BUFFER\)/) {
            if (depth != 2) exit 1; data = NR; pump++
        }
        if (/bool client_ready = initialized && wait_for_client\(state.server_fd, state.ftp_server != NULL\);/) {
            if (depth != 1) exit 1; http_poll = NR; http++
        }
        if (/if \(client_ready\)/) { if (depth != 1) exit 1; http_dispatch = NR; dispatch++ }
        if (/if \(owner_control_due \|\| !state.ftp_server\) update_power_guard\(&state, false\)/) {
            if (depth != 1) exit 1; lease++
        }
        if (/OSSleepTicks\(OSMillisecondsToTicks\(state.ftp_server \? 1 : 25\)\)/) sleep++
        code = $0
        gsub(/"([^"\\]|\\.)*"/, "", code)
        sub(/\/\/.*$/, "", code)
        opened = gsub(/\{/, "{", code)
        closed = gsub(/}/, "}", code)
        depth += opened - closed
        if (opened) entered = 1
        if (entered && depth == 0) active = 0
    }
    END {
        if (active || loops != 1 || due != 1 || procui != 1 || input != 1 || install != 1 || complete != 1 ||
            ftp != 1 || pump != 1 || http != 1 || dispatch != 1 || lease != 1 || sleep != 1 ||
            !(completed < ftp_guard && ftp_guard < data && data < http_poll && http_poll < http_dispatch)) exit 1
        print "Production FTP owner wiring passed: paced ProcUI/input/install, bounded data and HTTP cancellation remain on every outer loop."
    }
' "${1:-source/main.c}"
