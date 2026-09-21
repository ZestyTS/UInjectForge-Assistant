# Compile the actual receiver functions on a host, substituting only platform I/O.
# Definitions are column-zero blocks in main.c; fail closed if this layout changes.
{ sub(/\r$/, "") }
/^#define UIF_(MAX_FILES|MAX_PATH|MAX_TEXT|MAX_HEADER|ACTIVITY_LINES) / { print }
/^typedef struct$/ && !types_done { types = 1 }
types {
    print
    if (/^} AssistantState;$/) {
        types = 0
        types_done = 1
        print "static struct { bool automatic; AssistantTransferResult transfer_result; } g_install;"
        print "#include \"transfer_host_io.h\""
    }
    next
}
/^static / && /[ *](store_transfer_result|find_transfer_result|copy_text|json_escape|append_format|authorization_ok|handle_transfer_status|build_transfer_result_json|starts_with_ignore_case|skip_spaces)\(/ {
    function_body = 1
    functions++
}
function_body {
    print
    if (/^}$/) function_body = 0
}
END {
    if (!types_done || functions != 10 || function_body) {
        print "Receiver extraction failed; review changed production definitions." > "/dev/stderr"
        exit 1
    }
}
