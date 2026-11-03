{ sub(/\r$/, "") }
/^#define UIF_(MAX_FILES|MAX_PATH|MAX_TEXT|MAX_HEADER|ACTIVITY_LINES) / { print }
/^typedef struct$/ && !types_done { types = 1 }
types {
    print
    if (/^} AssistantState;$/) {
        types = 0; types_done = 1
        print "#include \"catalog_probe_host_io.h\""
    }
    next
}
/^typedef struct \{$/ { context = ""; collecting = 1 }
collecting {
    context = context $0 "\n"
    if (/^} [A-Za-z]/) {
        if (/^} CatalogProbeJsonContext;$/) { printf "%s", context; context_done = 1 }
        collecting = 0
    }
    next
}
/^static / && /[ *](json_escape|append_format|append_catalog_probe_json|handle_catalog_probe)\(/ { function_body = 1; functions++ }
function_body { print; if (/^}$/) function_body = 0 }
END {
    if (!types_done || !context_done || functions != 4 || function_body) {
        print "Catalog diagnostic extraction failed; review production definitions." > "/dev/stderr"
        exit 1
    }
}
