#ifndef ASSISTANT_JSON_H
#define ASSISTANT_JSON_H
#include <stdbool.h>
#include <stddef.h>
bool assistant_json_decode_string(const char *input, char *output, size_t capacity);
/* Bounded, validated top-level fields; slices remain owned by the input. */
typedef struct {
    char name[128];
    const char *start;
    size_t length;
    const char *value;
    size_t value_length;
} AssistantJsonField;
bool assistant_json_object(const char *json, AssistantJsonField *fields, size_t capacity, size_t *count);
#endif
