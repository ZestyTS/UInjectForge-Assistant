#include "assistant_json.h"
#include <stdint.h>
#include <string.h>

static void whitespace(const char **p) { while (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n') ++*p; }
static bool json_string_end(const char **p)
{
    char decoded[8192];
    if (!assistant_json_decode_string(*p, decoded, sizeof(decoded))) return false;
    ++*p;
    while (**p && **p != '"') { if (**p == '\\') ++*p; ++*p; }
    if (**p != '"') return false;
    ++*p;
    return true;
}
static bool json_value_end(const char **p, unsigned depth)
{
    if (depth > 16) return false;
    whitespace(p);
    if (**p == '"') return json_string_end(p);
    if (**p == '{' || **p == '[')
    {
        bool object = **p == '{';
        char end = object ? '}' : ']';
        ++*p; whitespace(p);
        if (**p == end) { ++*p; return true; }
        for (;;)
        {
            if (object) {
                if (!json_string_end(p)) return false;
                whitespace(p);
                if (**p != ':') return false;
                ++*p;
            }
            if (!json_value_end(p, depth + 1)) return false;
            whitespace(p);
            if (**p == end) { ++*p; return true; }
            if (**p != ',') return false;
            ++*p; whitespace(p);
        }
    }
    const char *words[] = {"true", "false", "null"};
    for (unsigned i = 0; i < 3; i++) if (strncmp(*p, words[i], strlen(words[i])) == 0) {
        *p += strlen(words[i]); return true;
    }
    if (**p == '-') ++*p;
    if (**p == '0') ++*p;
    else {
        if (**p < '1' || **p > '9') return false;
        do { ++*p; } while (**p >= '0' && **p <= '9');
    }
    if (**p == '.') {
        ++*p;
        if (**p < '0' || **p > '9') return false;
        do { ++*p; } while (**p >= '0' && **p <= '9');
    }
    if (**p == 'e' || **p == 'E') {
        ++*p;
        if (**p == '+' || **p == '-') ++*p;
        if (**p < '0' || **p > '9') return false;
        do { ++*p; } while (**p >= '0' && **p <= '9');
    }
    return true;
}

bool assistant_json_object(const char *json, AssistantJsonField *fields, size_t capacity, size_t *count)
{
    if (!json || !fields || !count || strlen(json) >= 8192) return false;
    *count = 0;
    const char *p = json;
    whitespace(&p);
    if (*p++ != '{') return false;
    whitespace(&p);
    if (*p != '}') for (;;) {
        if (*count >= capacity) return false;
        AssistantJsonField *field = &fields[*count];
        field->start = p;
        if (!assistant_json_decode_string(p, field->name, sizeof(field->name)) || !json_string_end(&p)) return false;
        for (size_t i = 0; i < *count; i++) if (!strcmp(fields[i].name, field->name)) return false;
        whitespace(&p);
        if (*p++ != ':') return false;
        whitespace(&p);
        field->value = p;
        if (!json_value_end(&p, 0)) return false;
        field->value_length = (size_t)(p - field->value);
        field->length = (size_t)(p - field->start);
        ++*count;
        whitespace(&p);
        if (*p == '}') break;
        if (*p++ != ',') return false;
        whitespace(&p);
    }
    ++p; whitespace(&p);
    return *p == 0;
}

static bool hex_word(const unsigned char **cursor, uint32_t *value)
{
    *value = 0;
    for (int i = 0; i < 4; ++i)
    {
        unsigned char c = **cursor;
        unsigned digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return false;
        ++*cursor;
        *value = (*value << 4) | digit;
    }
    return true;
}

bool assistant_json_decode_string(const char *input, char *output, size_t capacity)
{
    if (!output || !capacity) return false;
    output[0] = 0;
    if (!input || *input != '"') return false;
    const unsigned char *cursor = (const unsigned char *)input + 1;
    size_t used = 0;
    while (*cursor && *cursor != '"')
    {
        uint32_t value = *cursor++;
        if (value < 0x20) return false;
        if (value == '\\')
        {
            unsigned char escape = *cursor;
            if (!escape) return false;
            ++cursor;
            switch (escape)
            {
                case '"': case '\\': case '/': value = escape; break;
                case 'b': value = '\b'; break;
                case 'f': value = '\f'; break;
                case 'n': value = '\n'; break;
                case 'r': value = '\r'; break;
                case 't': value = '\t'; break;
                case 'u':
                    if (!hex_word(&cursor, &value)) return false;
                    if (value >= 0xD800 && value <= 0xDBFF)
                    {
                        uint32_t low;
                        if (cursor[0] != '\\' || cursor[1] != 'u') return false;
                        cursor += 2;
                        if (!hex_word(&cursor, &low) || low < 0xDC00 || low > 0xDFFF) return false;
                        value = 0x10000 + ((value - 0xD800) << 10) + low - 0xDC00;
                    }
                    else if (value >= 0xDC00 && value <= 0xDFFF) return false;
                    break;
                default: return false;
            }
        }
        else if (value >= 0x80)
        {
            unsigned remaining;
            uint32_t minimum;
            if (value >= 0xC2 && value <= 0xDF) { remaining = 1; minimum = 0x80; value &= 0x1F; }
            else if (value >= 0xE0 && value <= 0xEF) { remaining = 2; minimum = 0x800; value &= 0x0F; }
            else if (value >= 0xF0 && value <= 0xF4) { remaining = 3; minimum = 0x10000; value &= 7; }
            else return false;
            while (remaining--)
            {
                unsigned char next = *cursor;
                if (next < 0x80 || next > 0xBF) return false;
                ++cursor;
                value = (value << 6) | (next & 0x3F);
            }
            if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return false;
        }

        // C string consumers cannot safely represent embedded NUL.
        if (!value) return false;
        unsigned count = value < 0x80 ? 1 : value < 0x800 ? 2 : value < 0x10000 ? 3 : 4;
        if (count >= capacity - used) return false;
        if (count == 1) output[used++] = (char)value;
        else
        {
            output[used++] = (char)((count == 2 ? 0xC0 : count == 3 ? 0xE0 : 0xF0) | (value >> (6 * (count - 1))));
            for (unsigned n = count - 1; n > 0; --n)
                output[used++] = (char)(0x80 | ((value >> (6 * (n - 1))) & 0x3F));
        }
        output[used] = 0;
    }
    if (*cursor != '"') { output[0] = 0; return false; }
    output[used] = 0;
    return true;
}
