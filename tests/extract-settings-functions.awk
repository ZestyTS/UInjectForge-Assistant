# Compile production state and settings handlers; replace only status logging.
{ sub(/\r$/, "") }
/^#define UIF_(MAX_FILES|MAX_PATH|MAX_TEXT|MAX_HEADER|ACTIVITY_LINES) / { print }
/^typedef struct$/ && !types_done { types = 1 }
types {
    print
    if (/^} AssistantState;$/) {
        types = 0
        types_done = 1
        print "static void set_status(AssistantState *s, const char *message) { snprintf(s->status, sizeof(s->status), \"%s\", message); }"
        print "static unsigned int create_pairing_code(const AssistantState *s) { (void)s; static unsigned int code = 123456; return code++; }"
    }
    next
}
/^static / && !/;$/ && /[ *](copy_text|join_path_checked|find_json_key|skip_spaces|starts_with_ignore_case|json_get_bool|apply_live_settings|load_assistant_config|load_assistant_preferences|load_persistent_pairing_code|save_ui_settings)\(/ {
    function_body = 1
    functions++
}
function_body {
    print
    if (/^}$/) function_body = 0
}
END {
    if (!types_done || functions != 11 || function_body) {
        print "Settings extraction failed; review changed production definitions." > "/dev/stderr"
        exit 1
    }
}
