#ifndef RIG_JSON_H
#define RIG_JSON_H

#include "cjson/cJSON.h"
#include <stdbool.h>

// Path-based access: "foo.bar[0].baz"
cJSON *json_get(const cJSON *root, const char *path);
char  *json_get_string(const cJSON *root, const char *path);     // returns NULL if not found/wrong type
int    json_get_int(const cJSON *root, const char *path, int default_val);
double json_get_double(const cJSON *root, const char *path, double default_val);
bool   json_get_bool(const cJSON *root, const char *path, bool default_val);

// Set value at path (creates intermediate objects/arrays as needed)
int json_set_string(cJSON *root, const char *path, const char *value);
int json_set_int(cJSON *root, const char *path, int value);
int json_set_bool(cJSON *root, const char *path, bool value);

// Deep merge: overlay values from `patch` onto `base`. Returns new object.
// Caller owns the result. base and patch are not modified.
cJSON *json_deep_merge(const cJSON *base, const cJSON *patch);

// Read JSON from file. Returns NULL on error.
cJSON *json_read_file(const char *path);

// Write JSON to file (pretty-printed). Returns 0 on success.
int json_write_file(const char *path, const cJSON *json);

// Convenience: create string array from NULL-terminated char**
cJSON *json_string_array(const char **strings);

#endif
