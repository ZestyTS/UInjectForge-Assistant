#!/bin/sh
set -eu
awk '
    { sub(/\r$/, "") }
    /^static bool perform_install_action\(/ { active = 1; functions++ }
    active {
        if (/assistant_open_title_folder_session\(/) { session = NR; sessions++ }
        if (/validate_staged_install\(/) { guard = NR; guards++ }
        if (/MCP_InstallGetInfo\(handle,/) { if (!first_info) first_info = NR; last_info = NR; infos++ }
        if (/assistant_check_title_folders_in_session\(/) { final_check = NR; checks++ }
        if (/MCP_InstallSetTargetDevice\(/) target = NR
        if (/IOS_IoctlvAsync\(handle,/) submitted = NR
        if (/observed != strtoull\(g_staged_install.title_id/) identity++
        if (/strcmp\(current_digest, g_staged_install.tmd_sha256\)/) digest++
    }
    active && /^}/ { active = 0 }
    END {
        if (functions != 1 || active || sessions != 1 || guards != 1 || infos != 2 || checks != 1 || identity != 1 || digest != 1 ||
            !(session < first_info && first_info < guard && guard < last_info && last_info < final_check && final_check < target && target < submitted)) exit 1
        print "Production install context ordering passed: full late guard retained; setup restored; identity and folders rechecked before target setup."
    }
' "${1:-source/main.c}"
