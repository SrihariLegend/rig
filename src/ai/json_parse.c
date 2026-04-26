#include "json_parse.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

cJSON *json_parse_repair(const char *json) {
    if (!json) return NULL;

    cJSON *result = cJSON_Parse(json);
    if (result) return result;

    size_t len = strlen(json);
    char *fixed = malloc(len + 64);
    if (!fixed) return NULL;
    memcpy(fixed, json, len + 1);

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)fixed[i];
        if (c < 0x20 && c != '\n' && c != '\r' && c != '\t') {
            fixed[i] = ' ';
        }
    }

    result = cJSON_Parse(fixed);
    if (result) { free(fixed); return result; }

    // Strip trailing commas before } and ]
    size_t write = 0;
    bool in_string = false;
    for (size_t i = 0; fixed[i]; i++) {
        if (fixed[i] == '"' && (i == 0 || fixed[i-1] != '\\')) in_string = !in_string;
        if (!in_string && fixed[i] == ',') {
            size_t j = i + 1;
            while (fixed[j] && isspace((unsigned char)fixed[j])) j++;
            if (fixed[j] == '}' || fixed[j] == ']' || fixed[j] == '\0') continue;
        }
        fixed[write++] = fixed[i];
    }
    fixed[write] = '\0';

    result = cJSON_Parse(fixed);
    if (result) { free(fixed); return result; }

    size_t end = write;
    while (end > 0 && isspace((unsigned char)fixed[end - 1])) end--;
    fixed[end] = '\0';

    int braces = 0, brackets = 0;
    for (size_t i = 0; i < end; i++) {
        if (fixed[i] == '{') braces++;
        else if (fixed[i] == '}') braces--;
        else if (fixed[i] == '[') brackets++;
        else if (fixed[i] == ']') brackets--;
    }

    size_t pos = end;
    while (brackets > 0) { fixed[pos++] = ']'; brackets--; }
    while (braces > 0) { fixed[pos++] = '}'; braces--; }
    fixed[pos] = '\0';

    result = cJSON_Parse(fixed);
    free(fixed);
    return result;
}

cJSON *json_parse_streaming(const char *partial_json) {
    if (!partial_json) return NULL;

    cJSON *result = cJSON_Parse(partial_json);
    if (result) return result;

    return json_parse_repair(partial_json);
}
