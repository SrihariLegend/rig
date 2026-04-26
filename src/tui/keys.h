#ifndef PI_KEYS_H
#define PI_KEYS_H

#include <stdbool.h>

typedef struct {
    char id[64];
    char printable[8];
    bool is_release;
    bool is_paste;
    char *paste_data;
} ParsedKey;

ParsedKey key_parse(const char *raw_input, int len);
bool key_matches(const ParsedKey *key, const char *key_id);

#endif
