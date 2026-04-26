#include "keys.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

static void set_id(ParsedKey *key, const char *id) {
    strncpy(key->id, id, sizeof(key->id) - 1);
    key->id[sizeof(key->id) - 1] = '\0';
}

static void set_printable(ParsedKey *key, const char *p) {
    strncpy(key->printable, p, sizeof(key->printable) - 1);
    key->printable[sizeof(key->printable) - 1] = '\0';
}

ParsedKey key_parse(const char *raw, int len) {
    ParsedKey key = {0};

    if (raw && len > 0) {
        int copy = len < (int)sizeof(key.raw) - 1 ? len : (int)sizeof(key.raw) - 1;
        memcpy(key.raw, raw, copy);
        key.raw[copy] = '\0';
        key.raw_len = copy;
    }

    if (!raw || len <= 0) {
        set_id(&key, "unknown");
        return key;
    }

    if (len >= 6 && memcmp(raw, "\x1b[200~", 6) == 0) {
        key.is_paste = true;
        set_id(&key, "paste");
        const char *end = strstr(raw + 6, "\x1b[201~");
        if (end) {
            int paste_len = (int)(end - raw - 6);
            key.paste_data = strndup(raw + 6, paste_len);
        }
        return key;
    }

    if (len >= 4 && raw[0] == '\x1b' && raw[1] == '[' && raw[len-1] == 'u') {
        int code = 0, mods = 1;
        char type = 0;
        const char *p = raw + 2;
        while (*p && isdigit((unsigned char)*p)) {
            code = code * 10 + (*p - '0');
            p++;
        }
        if (*p == ';') {
            p++;
            mods = 0;
            while (*p && isdigit((unsigned char)*p)) {
                mods = mods * 10 + (*p - '0');
                p++;
            }
            if (*p == ':') {
                p++;
                type = *p;
                p++;
            }
        }

        key.is_release = (type == '3');

        char prefix[32] = "";
        if (mods > 1) {
            int m = mods - 1;
            if (m & 1) strcat(prefix, "shift+");
            if (m & 2) strcat(prefix, "alt+");
            if (m & 4) strcat(prefix, "ctrl+");
        }

        if (code == 13) { snprintf(key.id, sizeof(key.id), "%senter", prefix); }
        else if (code == 9) { snprintf(key.id, sizeof(key.id), "%stab", prefix); }
        else if (code == 27) { snprintf(key.id, sizeof(key.id), "%sescape", prefix); }
        else if (code == 127) { snprintf(key.id, sizeof(key.id), "%sbackspace", prefix); }
        else if (code >= 32 && code < 127) {
            char c = (char)code;
            if (strlen(prefix) > 0) {
                snprintf(key.id, sizeof(key.id), "%s%c", prefix, (char)tolower(c));
            } else {
                key.printable[0] = c;
                key.printable[1] = '\0';
                snprintf(key.id, sizeof(key.id), "%c", c);
            }
        }
        else { snprintf(key.id, sizeof(key.id), "csi-u-%d", code); }

        return key;
    }

    if (raw[0] == '\x1b' && len >= 2) {
        if (raw[1] == '[') {
            if (len == 3) {
                switch (raw[2]) {
                    case 'A': set_id(&key, "up"); return key;
                    case 'B': set_id(&key, "down"); return key;
                    case 'C': set_id(&key, "right"); return key;
                    case 'D': set_id(&key, "left"); return key;
                    case 'H': set_id(&key, "home"); return key;
                    case 'F': set_id(&key, "end"); return key;
                    case 'Z': set_id(&key, "shift+tab"); return key;
                }
            }

            if (len == 4 && raw[3] == '~') {
                switch (raw[2]) {
                    case '1': set_id(&key, "home"); return key;
                    case '2': set_id(&key, "insert"); return key;
                    case '3': set_id(&key, "delete"); return key;
                    case '4': set_id(&key, "end"); return key;
                    case '5': set_id(&key, "pageup"); return key;
                    case '6': set_id(&key, "pagedown"); return key;
                }
            }

            if (len >= 5 && raw[2] == '1' && raw[3] == ';') {
                int mod = raw[4] - '0';
                const char *arrow = "";
                if (len >= 6) {
                    switch (raw[5]) {
                        case 'A': arrow = "up"; break;
                        case 'B': arrow = "down"; break;
                        case 'C': arrow = "right"; break;
                        case 'D': arrow = "left"; break;
                    }
                }
                char prefix[32] = "";
                int m = mod - 1;
                if (m & 1) strcat(prefix, "shift+");
                if (m & 2) strcat(prefix, "alt+");
                if (m & 4) strcat(prefix, "ctrl+");
                snprintf(key.id, sizeof(key.id), "%s%s", prefix, arrow);
                return key;
            }
        }

        if (len == 2) {
            snprintf(key.id, sizeof(key.id), "alt+%c", raw[1]);
            return key;
        }

        set_id(&key, "escape");
        return key;
    }

    if (raw[0] == '\r' || raw[0] == '\n') {
        set_id(&key, "enter");
        set_printable(&key, "\n");
    } else if (raw[0] == '\t') {
        set_id(&key, "tab");
    } else if (raw[0] == 127) {
        set_id(&key, "backspace");
    } else if (raw[0] >= 1 && raw[0] <= 26) {
        snprintf(key.id, sizeof(key.id), "ctrl+%c", 'a' + raw[0] - 1);
    } else if (raw[0] >= 32) {
        int bytes = 1;
        unsigned char c = (unsigned char)raw[0];
        if ((c & 0xE0) == 0xC0) bytes = 2;
        else if ((c & 0xF0) == 0xE0) bytes = 3;
        else if ((c & 0xF8) == 0xF0) bytes = 4;

        if (bytes <= len) {
            memcpy(key.printable, raw, bytes);
            key.printable[bytes] = '\0';
            memcpy(key.id, raw, bytes);
            key.id[bytes] = '\0';
        }
    } else {
        set_id(&key, "unknown");
    }

    return key;
}

bool key_matches(const ParsedKey *key, const char *key_id) {
    if (!key || !key_id) return false;
    return strcmp(key->id, key_id) == 0;
}
