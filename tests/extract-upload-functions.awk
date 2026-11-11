# Compile the actual upload receiver with host sockets/files and platform stubs.
{ sub(/\r$/, "") }
/^#define UIF_(MAX_FILES|MAX_PATH|MAX_TEXT|MAX_HEADER|ACTIVITY_LINES|IO_BUFFER|UPLOAD_BUFFER|UPLOAD_IDLE_TIMEOUT_MS|UPLOAD_RENDER_INTERVAL_MS) / { print }
/^typedef struct$/ && !types_done { types = 1 }
types {
    print
    if (/^} AssistantState;$/) {
        types = 0
        types_done = 1
        print "#include \"upload_host_io.h\""
    }
    next
}
/^static / && /[ *](copy_text|copy_display_name|equals_ignore_case|digest_to_hex|hash_file_sha256|clear_receiving_file_state|persist_upload_chunk|record_upload_timing|receive_file_body)\(/ {
    function_body = 1
    functions++
}
function_body {
    print
    if (/^}$/) function_body = 0
}
END {
    if (!types_done || functions != 9 || function_body) {
        print "Upload receiver extraction failed; review production definitions." > "/dev/stderr"
        exit 1
    }
}
