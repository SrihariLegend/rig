#ifndef PI_HARNESS_SETTINGS_H
#define PI_HARNESS_SETTINGS_H

#include "cjson/cJSON.h"
#include <stdbool.h>

/* Settings layers, ordered by precedence (lowest to highest) */
typedef enum {
    SETTINGS_GLOBAL,    /* ~/.pi/agent/settings.json */
    SETTINGS_PROJECT,   /* .pi/settings.json         */
    SETTINGS_CLI,       /* command-line overrides     */
} SettingsLayer;

typedef struct {
    cJSON *global;       /* parsed from global settings file  */
    cJSON *project;      /* parsed from project settings file */
    cJSON *cli;          /* runtime overrides                 */
    cJSON *merged;       /* deep merge: global < project < cli */
    char  *global_path;
    char  *project_path;
} SettingsManager;

/* Lifecycle */
SettingsManager *settings_create(const char *global_path, const char *project_path);
void             settings_free(SettingsManager *sm);

/* Get a setting value from the merged layer. Dot-notation: "compaction.enabled" */
cJSON *settings_get(SettingsManager *sm, const char *key);

/* Set a value in a specific layer. Creates intermediate objects for dot-paths. */
int settings_set(SettingsManager *sm, SettingsLayer layer, const char *key, cJSON *value);

/* Flush global and project layers to disk */
int settings_flush(SettingsManager *sm);

/* Re-read files from disk and recompute the merged layer */
int settings_reload(SettingsManager *sm);

/* Typed getters with defaults (read from merged) */
const char *settings_get_string(SettingsManager *sm, const char *key, const char *def);
int         settings_get_int(SettingsManager *sm, const char *key, int def);
bool        settings_get_bool(SettingsManager *sm, const char *key, bool def);
double      settings_get_double(SettingsManager *sm, const char *key, double def);

#endif
