#ifndef ASSISTANT_JSON_H
#define ASSISTANT_JSON_H
#include <stdbool.h>
#include <stddef.h>
bool assistant_json_decode_string(const char *input, char *output, size_t capacity);
#endif
