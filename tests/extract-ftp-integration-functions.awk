# Exercise actual production admission, callbacks, sealing and publication.
# Only platform services/HTTP response sinks are substituted by the host header.
{ sub(/\r$/, "") }
/^#define UIF_(ASSISTANT_PROTOCOL|MAX_FILES|MAX_PATH|MAX_TEXT|MAX_HEADER|MAX_MANIFEST|IO_BUFFER|UPLOAD_BUFFER|UPLOAD_RENDER_INTERVAL_MS|ACTIVITY_LINES) / { print }
/^typedef struct$/ && !types_done { types = 1 }
types {
    print
    if (/^} AssistantState;$/) {
        types = 0; types_done = 1
        print "#include \"ftp_integration_host_io.h\""
    }
    next
}
/^static / && !/;$/ && /[ *](store_transfer_result|find_transfer_result|transfer_destination_available|copy_text|is_hex_sha256|equals_ignore_case|starts_with_ignore_case|json_escape|record_transfer_result|safe_relative_path|path_is_below|path_has_allowed_sd_prefix|path_is_assistant_transaction_storage|validate_transfer_destination_header|validate_transfer_file_destination|join_path|join_path_checked|parent_path|path_exists|path_is_directory|mkdir_recursive|ensure_parent_directory|remove_tree|digest_to_hex|hash_file_sha256|url_decode|read_body_to_memory|find_json_key|json_get_string|json_get_long|json_get_bool|skip_spaces|hex_value|find_matching_brace|find_file|parse_manifest|authorization_token|authorization_ok|clear_receiving_file_state|build_file_backup_path|rollback_sd_root_commit|commit_transfer|ftp_begin_file|ftp_write_file|ftp_progress_file|ftp_finish_file|stop_ftp_transfer|handle_start_transfer|handle_upload|send_finalized_commit_response|handle_commit|handle_cancel_transfer)\(/ {
    function_body = 1; functions++
}
function_body { print; if (/^}$/) function_body = 0 }
END {
    if (!types_done || functions != 52 || function_body) {
        print "FTP integration extraction failed; review changed production definitions (expected 52)." > "/dev/stderr"
        exit 1
    }
}
