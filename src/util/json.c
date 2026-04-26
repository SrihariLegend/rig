#include "json.h"
#include "cjson/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <unistd.h>

// Helper: parse array index from "[N]" at start of str. Returns index and advances *str.
// Returns -1 if not a valid array index.
static int parse_array_index(const char **str) {
    if (**str != '[') return -1;
    (*str)++;

    if (!isdigit(**str)) return -1;

    int idx = 0;
    while (isdigit(**str)) {
        idx = idx * 10 + (**str - '0');
        (*str)++;
    }

    if (**str != ']') return -1;
    (*str)++;

    return idx;
}

// Helper: extract next path segment. Handles "foo", "foo[0]", etc.
// Returns malloc'd segment name (without array notation) and advances *path.
// Sets *array_idx to index if array notation present, else -1.
// Returns NULL at end of path.
static char *next_segment(const char **path, int *array_idx) {
    *array_idx = -1;

    if (!*path || !**path) return NULL;

    // Skip leading dot
    if (**path == '.') (*path)++;

    if (!**path) return NULL;

    // Find end of segment name (before . or [ or end)
    const char *start = *path;
    const char *end = start;

    while (*end && *end != '.' && *end != '[') {
        end++;
    }

    if (end == start) return NULL;

    size_t len = end - start;
    char *segment = malloc(len + 1);
    if (!segment) return NULL;

    memcpy(segment, start, len);
    segment[len] = '\0';

    *path = end;

    // Check for array index
    if (**path == '[') {
        *array_idx = parse_array_index(path);
        if (*array_idx < 0) {
            free(segment);
            return NULL;
        }
    }

    return segment;
}

cJSON *json_get(const cJSON *root, const char *path) {
    if (!root || !path) return NULL;

    cJSON *current = (cJSON *)root;
    const char *p = path;
    int array_idx;

    while (current) {
        char *segment = next_segment(&p, &array_idx);
        if (!segment) break;

        // Navigate to named item
        if (!cJSON_IsArray(current) && !cJSON_IsObject(current)) {
            free(segment);
            return NULL;
        }

        current = cJSON_GetObjectItemCaseSensitive(current, segment);
        free(segment);

        if (!current) return NULL;

        // Navigate to array index if present
        if (array_idx >= 0) {
            if (!cJSON_IsArray(current)) return NULL;
            current = cJSON_GetArrayItem(current, array_idx);
            if (!current) return NULL;
        }
    }

    return current;
}

char *json_get_string(const cJSON *root, const char *path) {
    cJSON *item = json_get(root, path);
    if (!item || !cJSON_IsString(item)) return NULL;
    return item->valuestring;
}

int json_get_int(const cJSON *root, const char *path, int default_val) {
    cJSON *item = json_get(root, path);
    if (!item || !cJSON_IsNumber(item)) return default_val;
    return item->valueint;
}

double json_get_double(const cJSON *root, const char *path, double default_val) {
    cJSON *item = json_get(root, path);
    if (!item || !cJSON_IsNumber(item)) return default_val;
    return item->valuedouble;
}

bool json_get_bool(const cJSON *root, const char *path, bool default_val) {
    cJSON *item = json_get(root, path);
    if (!item || !cJSON_IsBool(item)) return default_val;
    return cJSON_IsTrue(item);
}

// Helper: ensure parent path exists, creating objects as needed.
// Returns pointer to parent object where final key should be set.
static cJSON *ensure_path(cJSON *root, const char *path, char **final_key) {
    if (!root || !path) return NULL;

    cJSON *current = root;
    const char *p = path;
    int array_idx;

    char *segment = next_segment(&p, &array_idx);
    if (!segment) return NULL;

    while (true) {
        int next_idx;
        const char *peek = p;
        char *next_seg = next_segment(&peek, &next_idx);

        if (!next_seg) {
            *final_key = segment;
            return current;
        }

        cJSON *child = cJSON_GetObjectItemCaseSensitive(current, segment);
        if (!child) {
            child = cJSON_CreateObject();
            cJSON_AddItemToObject(current, segment, child);
        }

        if (!cJSON_IsObject(child)) {
            free(segment);
            free(next_seg);
            return NULL;
        }

        current = child;
        free(segment);
        segment = next_seg;
        p = peek;
    }
}

int json_set_string(cJSON *root, const char *path, const char *value) {
    if (!root || !path || !value) return -1;

    char *final_key = NULL;
    cJSON *parent = ensure_path(root, path, &final_key);

    if (!parent || !final_key) {
        if (final_key) free(final_key);
        return -1;
    }

    // Remove existing item if present
    cJSON_DeleteItemFromObject(parent, final_key);

    // Add new string value
    cJSON *item = cJSON_CreateString(value);
    if (!item) {
        free(final_key);
        return -1;
    }

    cJSON_AddItemToObject(parent, final_key, item);
    free(final_key);
    return 0;
}

int json_set_int(cJSON *root, const char *path, int value) {
    if (!root || !path) return -1;

    char *final_key = NULL;
    cJSON *parent = ensure_path(root, path, &final_key);

    if (!parent || !final_key) {
        if (final_key) free(final_key);
        return -1;
    }

    cJSON_DeleteItemFromObject(parent, final_key);

    cJSON *item = cJSON_CreateNumber(value);
    if (!item) {
        free(final_key);
        return -1;
    }

    cJSON_AddItemToObject(parent, final_key, item);
    free(final_key);
    return 0;
}

int json_set_bool(cJSON *root, const char *path, bool value) {
    if (!root || !path) return -1;

    char *final_key = NULL;
    cJSON *parent = ensure_path(root, path, &final_key);

    if (!parent || !final_key) {
        if (final_key) free(final_key);
        return -1;
    }

    cJSON_DeleteItemFromObject(parent, final_key);

    cJSON *item = cJSON_CreateBool(value);
    if (!item) {
        free(final_key);
        return -1;
    }

    cJSON_AddItemToObject(parent, final_key, item);
    free(final_key);
    return 0;
}

cJSON *json_deep_merge(const cJSON *base, const cJSON *patch) {
    if (!base && !patch) return NULL;
    if (!base) return cJSON_Duplicate(patch, 1);
    if (!patch) return cJSON_Duplicate(base, 1);

    // If patch is not an object, it replaces base entirely
    if (!cJSON_IsObject(patch)) {
        return cJSON_Duplicate(patch, 1);
    }

    // Start with a copy of base
    cJSON *result = cJSON_Duplicate(base, 1);
    if (!result) return NULL;

    // If base is not an object, patch replaces it
    if (!cJSON_IsObject(result)) {
        cJSON_Delete(result);
        return cJSON_Duplicate(patch, 1);
    }

    // Iterate over patch keys
    cJSON *patch_item = NULL;
    cJSON_ArrayForEach(patch_item, patch) {
        const char *key = patch_item->string;
        if (!key) continue;

        cJSON *base_item = cJSON_GetObjectItemCaseSensitive(result, key);

        if (base_item && cJSON_IsObject(base_item) && cJSON_IsObject(patch_item)) {
            // Recursive merge for nested objects
            cJSON *merged = json_deep_merge(base_item, patch_item);
            if (merged) {
                cJSON_DeleteItemFromObject(result, key);
                cJSON_AddItemToObject(result, key, merged);
            }
        } else {
            // Replace value
            cJSON_DeleteItemFromObject(result, key);
            cJSON *copy = cJSON_Duplicate(patch_item, 1);
            if (copy) {
                cJSON_AddItemToObject(result, key, copy);
            }
        }
    }

    return result;
}

cJSON *json_read_file(const char *path) {
    if (!path) return NULL;

    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    // Get file size
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0 || size > 100 * 1024 * 1024) {  // Sanity check: max 100MB
        fclose(f);
        return NULL;
    }

    char *buffer = malloc(size + 1);
    if (!buffer) {
        fclose(f);
        return NULL;
    }

    size_t read_size = fread(buffer, 1, size, f);
    fclose(f);

    if ((long)read_size != size) {
        free(buffer);
        return NULL;
    }

    buffer[size] = '\0';

    cJSON *json = cJSON_Parse(buffer);
    free(buffer);

    return json;
}

int json_write_file(const char *path, const cJSON *json) {
    if (!path || !json) return -1;

    char *json_str = cJSON_Print(json);
    if (!json_str) return -1;

    // Write to temp file
    char temp_path[4096];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp.%d", path, getpid());

    FILE *f = fopen(temp_path, "wb");
    if (!f) {
        free(json_str);
        return -1;
    }

    size_t len = strlen(json_str);
    size_t written = fwrite(json_str, 1, len, f);
    fclose(f);
    free(json_str);

    if (written != len) {
        unlink(temp_path);
        return -1;
    }

    // Atomic rename
    if (rename(temp_path, path) != 0) {
        unlink(temp_path);
        return -1;
    }

    return 0;
}

cJSON *json_string_array(const char **strings) {
    if (!strings) return NULL;

    cJSON *array = cJSON_CreateArray();
    if (!array) return NULL;

    for (int i = 0; strings[i] != NULL; i++) {
        cJSON *str = cJSON_CreateString(strings[i]);
        if (!str) {
            cJSON_Delete(array);
            return NULL;
        }
        cJSON_AddItemToArray(array, str);
    }

    return array;
}
