#include "assistant_json.h"
#include "assistant_paths.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    char output[1024];
    assert(assistant_json_decode_string("\"Link\\u0027s Adventure\"", output, sizeof(output)));
    assert(strcmp(output, "Link's Adventure") == 0);
    assert(assistant_json_decode_string("\"Pok\\u00E9mon\"", output, sizeof(output)));
    assert(strcmp(output, "Pok\xC3\xA9mon") == 0);
    assert(assistant_json_decode_string("\"\\uD83D\\uDE00\"", output, sizeof(output)));
    assert(strcmp(output, "\xF0\x9F\x98\x80") == 0);
    assert(assistant_json_decode_string("\"\\b\\f\\n\\r\\t\\\\\\/\"", output, sizeof(output)));
    const char *invalid[] = { "\"", "\"\\", "\"\\u\"", "\"\\u123\"", "\"\\uXYZW\"",
        "\"\\uD800\"", "\"\\uDC00\"", "\"\\uD800\\u0041\"", "\"\\q\"", "\"\\u0000\"",
        "\"\xC0\xAF\"", "\"\xED\xA0\x80\"", "\"\xF4\x90\x80\x80\"", "\"\n\"" };
    for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i)
        assert(!assistant_json_decode_string(invalid[i], output, sizeof(output)));
    assert(!assistant_json_decode_string("\"abcd\"", output, 4));
    assert(assistant_json_decode_string("\"abcd\"", output, 5));
    assert(assistant_sd_config_path_allowed("nincfg.bin"));
    assert(assistant_sd_config_path_allowed("nintendont/configs/GMSE.bin"));
    assert(!assistant_sd_config_path_allowed("nintendont/configs/../../boot.bin"));
    assert(!assistant_sd_config_path_allowed("nintendont/configs/ABCDE.bin"));
    assert(!assistant_sd_config_path_allowed("nintendont/configs/GMSE.bin/extra"));
    assert(!assistant_sd_config_path_allowed("nintendont/other.bin"));
    // Optional line pairs emitted by System.Text.Json: JSON string, expected UTF-8.
    if (argc == 2)
    {
        FILE *fixture = fopen(argv[1], "rb");
        assert(fixture);
        char json[4096], expected[1024];
        int pairs = 0;
        while (fgets(json, sizeof(json), fixture))
        {
            assert(fgets(expected, sizeof(expected), fixture));
            expected[strcspn(expected, "\r\n")] = 0;
            assert(assistant_json_decode_string(json, output, sizeof(output)));
            assert(strcmp(output, expected) == 0);
            ++pairs;
        }
        fclose(fixture);
        assert(pairs >= 5);
    }
    puts("Assistant JSON and config path tests passed.");
    return 0;
}
