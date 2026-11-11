{ sub(/\r$/, "") }
/^#define UIF_(MAX_FILES|MAX_PATH|MAX_TEXT|MAX_HEADER|ACTIVITY_LINES) / { print }
/^typedef struct$/ && !types_done { types = 1 }
types {
    print
    if (/^} AssistantState;$/) {
        types = 0; types_done = 1
        print "static AssistantStagedInstallRequest g_staged_install;"
        print "#include \"staged_install_host_io.h\""
    }
    next
}
/^typedef struct \{ const char \*title_id; bool found; \} StagedTitleLookup;$/ { print }
/^static / && /[ *](copy_text|store_transfer_result|staged_title_match|validate_staged_install|handle_staged_install)\(/ { function_body = 1; functions++ }
function_body { print; if (/^}$/) function_body = 0 }
END {
    if (!types_done || functions != 5 || function_body) {
        print "Staged installer extraction failed; review production definitions." > "/dev/stderr"
        exit 1
    }
}
