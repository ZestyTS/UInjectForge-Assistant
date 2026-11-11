static bool field_equals(const char *name, const char *expected)
{
    AssistantJsonField fields[24]; size_t count = 0;
    assert(response_body && assistant_json_object(response_body, fields, 24, &count));
    for (size_t i = 0; i < count; i++) {
        if (!strcmp(fields[i].name, name))
            return strlen(expected) == fields[i].value_length && !memcmp(fields[i].value, expected, fields[i].value_length);
    }
    return false;
}
static bool string_field_equals(const char *name, const char *expected)
{
    AssistantJsonField fields[24]; size_t count = 0;
    assert(response_body && assistant_json_object(response_body, fields, 24, &count));
    for (size_t i = 0; i < count; i++) {
        if (!strcmp(fields[i].name, name)) {
            char encoded[64], decoded[64];
            if (fields[i].value_length >= sizeof(encoded)) return false;
            memcpy(encoded, fields[i].value, fields[i].value_length);
            encoded[fields[i].value_length] = '\0';
            return assistant_json_decode_string(encoded, decoded, sizeof(decoded)) && !strcmp(decoded, expected);
        }
    }
    return false;
}
int main(void)
{
    AssistantState *state = calloc(1, sizeof(*state));
    HttpRequest *request = calloc(1, sizeof(*request));
    assert(state && request);
    handle_catalog_probe(1, state, request, "expanded");
    assert(response_status == 401 && probe_calls == 0);
    handle_catalog_probe(1, state, request, "device/slc");
    assert(response_status == 401 && probe_calls == 0);
    handle_catalog_probe(1, state, request, "folders/0005000200000001");
    assert(response_status == 401 && folder_probe_calls == 0);
    authorized = true;
    bool *busy[] = {&state->transfer.active, &state->receiving_file, &state->verifying_files,
        &state->install_active, &state->auto_install_pending, &state->title_launch_pending};
    for (unsigned i = 0; i < sizeof(busy) / sizeof(*busy); i++) {
        *busy[i] = true; handle_catalog_probe(1, state, request, "expanded");
        assert(response_status == 409 && probe_calls == 0);
        handle_catalog_probe(1, state, request, "device/slc");
        assert(response_status == 409 && probe_calls == 0);
        handle_catalog_probe(1, state, request, "folders/0005000200000001");
        assert(response_status == 409 && folder_probe_calls == 0);
        assert(response_status == 409 && probe_calls == 0); *busy[i] = false;
    }
    const char *invalid[] = {"", "../expanded", "app-type/", "app-type/8000000", "app-type/800000000", "app-type/8000000Z", "app-type/80000000/x", "unknown",
        "device/", "device/auto", "device/USB", "device/usb/x", "device/5",
        "device/SLC", "device/slc/x", "device/slccmpt/../usb"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        handle_catalog_probe(1, state, request, invalid[i]); assert(response_status == 400 && probe_calls == 0);
    }
    handle_catalog_probe(1, state, request, "count-sized");
    assert(response_status == 200 && received_kind == ASSISTANT_PROBE_COUNT_SIZED);
    assert(field_equals("diagnosticOnly", "true") && field_equals("safeForInstallation", "false"));
    assert(field_equals("success", "true") && field_equals("truncated", "false"));
    assert(strstr(response_body, "quoted\\\"") && strstr(response_body, "test\\\\path"));
    handle_catalog_probe(1, state, request, "expanded");
    assert(received_kind == ASSISTANT_PROBE_EXPANDED);
    handle_catalog_probe(1, state, request, "app-type/8000002e");
    assert(received_kind == ASSISTANT_PROBE_APP_TYPE && received_app == 0x8000002e);
    const char *devices[] = {"usb", "mlc", "odd"};
    const uint32_t filters[] = {4, 3, 2};
    for (unsigned i = 0; i < 3; i++) {
        char mode[16]; snprintf(mode, sizeof(mode), "device/%s", devices[i]);
        handle_catalog_probe(1, state, request, mode);
        assert(response_status == 200 && received_kind == ASSISTANT_PROBE_DEVICE_TYPE && received_app == filters[i]);
        assert(string_field_equals("mode", mode) && field_equals("diagnosticOnly", "true") && field_equals("safeForInstallation", "false"));
    }
    const char *system_devices[] = {"slc", "slccmpt"};
    for (unsigned i = 0; i < 2; i++) {
        char mode[24]; snprintf(mode, sizeof(mode), "device/%s", system_devices[i]);
        handle_catalog_probe(1, state, request, mode);
        assert(response_status == 200 && received_kind == ASSISTANT_PROBE_SYSTEM_DEVICE && received_app == i);
        assert(string_field_equals("mode", mode) && field_equals("diagnosticOnly", "true") && field_equals("safeForInstallation", "false"));
    }
    fail_probe = true; handle_catalog_probe(1, state, request, "expanded");
    assert(field_equals("success", "false") && field_equals("truncated", "true") && field_equals("error", "-15"));
    handle_catalog_probe(1, state, request, "folders/../../key");
    assert(response_status == 400 && folder_probe_calls == 0);
    const AssistantTitleFolderState folder_states[] = {ASSISTANT_TITLE_FOLDERS_UNKNOWN, ASSISTANT_TITLE_FOLDERS_ABSENT, ASSISTANT_TITLE_FOLDERS_PRESENT};
    const char *names[] = {"unknown", "absent", "present"};
    for (unsigned i = 0; i < 3; ++i) {
        folder_result.state = folder_states[i];
        handle_catalog_probe(1, state, request, "folders/0005000200000001");
        assert(response_status == 200 && string_field_equals("state", names[i]));
        assert(field_equals("diagnosticOnly", "true") && field_equals("safeForInstallation", "false"));
    }
    assert(folder_probe_calls == 3);
    char small[128] = {0}; CatalogProbeJsonContext output = {small, sizeof(small), 0, true};
    AssistantCatalogProbeRecord record = {0};
    assert(!append_catalog_probe_json(&record, &output) && output.offset == 0 && output.first);
    free(state); free(request); free(response_body);
    puts("Production catalog diagnostic handler passed; pairing, busy gates, system-device whitelist and bounded JSON verified.");
    return 0;
}
