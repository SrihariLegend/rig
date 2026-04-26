#include "model_registry.h"
#include "ai/models.h"
#include "util/log.h"
#include "cjson/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* ---- Helpers ---- */

static char *str_tolower(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *lower = malloc(len + 1);
    for (size_t i = 0; i < len; i++) {
        lower[i] = (char)tolower((unsigned char)s[i]);
    }
    lower[len] = '\0';
    return lower;
}

static bool case_insensitive_contains(const char *haystack, const char *needle) {
    if (!haystack || !needle) return false;
    char *h = str_tolower(haystack);
    char *n = str_tolower(needle);
    bool found = strstr(h, n) != NULL;
    free(h);
    free(n);
    return found;
}

static Model model_clone(const Model *src) {
    Model m = *src;
    m.id = src->id ? strdup(src->id) : NULL;
    m.name = src->name ? strdup(src->name) : NULL;
    m.api = src->api ? strdup(src->api) : NULL;
    m.provider = src->provider ? strdup(src->provider) : NULL;
    m.base_url = src->base_url ? strdup(src->base_url) : NULL;
    m.headers = src->headers ? cJSON_Duplicate(src->headers, 1) : NULL;

    /* Clone modalities array */
    if (src->input_modalities && src->input_modality_count > 0) {
        const char **mods = calloc((size_t)(src->input_modality_count + 1), sizeof(char *));
        for (int i = 0; i < src->input_modality_count; i++) {
            mods[i] = src->input_modalities[i] ? strdup(src->input_modalities[i]) : NULL;
        }
        m.input_modalities = mods;
    } else {
        m.input_modalities = NULL;
    }
    return m;
}

static void model_destroy(Model *m) {
    if (!m) return;
    free(m->id);
    free(m->name);
    free(m->api);
    free(m->provider);
    free(m->base_url);
    if (m->headers) cJSON_Delete(m->headers);
    if (m->input_modalities) {
        for (int i = 0; i < m->input_modality_count; i++) {
            free((char *)m->input_modalities[i]);
        }
        free((void *)m->input_modalities);
    }
}

/* ---- Registry lifecycle ---- */

ModelRegistry *model_registry_create(void) {
    ModelRegistry *mr = calloc(1, sizeof(ModelRegistry));
    if (!mr) return NULL;

    /* Load builtin models from models.c */
    models_init();

    int all_count = 0;
    const Model **all = models_get_all(NULL, &all_count);

    if (all_count > 0 && all) {
        mr->builtin_models = calloc((size_t)all_count, sizeof(Model));
        mr->builtin_count = all_count;
        for (int i = 0; i < all_count; i++) {
            mr->builtin_models[i] = model_clone(all[i]);
        }
    }

    return mr;
}

void model_registry_free(ModelRegistry *mr) {
    if (!mr) return;

    for (int i = 0; i < mr->builtin_count; i++) {
        model_destroy(&mr->builtin_models[i]);
    }
    free(mr->builtin_models);

    for (int i = 0; i < mr->custom_count; i++) {
        model_destroy(&mr->custom_models[i]);
    }
    free(mr->custom_models);

    free(mr);
}

/* ---- Resolution ---- */

Model *model_registry_resolve(ModelRegistry *mr, const char *pattern) {
    if (!mr || !pattern || !*pattern) return NULL;

    /* Pass 1: exact ID match */
    for (int i = 0; i < mr->custom_count; i++) {
        if (mr->custom_models[i].id && strcmp(mr->custom_models[i].id, pattern) == 0) {
            return &mr->custom_models[i];
        }
    }
    for (int i = 0; i < mr->builtin_count; i++) {
        if (mr->builtin_models[i].id && strcmp(mr->builtin_models[i].id, pattern) == 0) {
            return &mr->builtin_models[i];
        }
    }

    /* Pass 2: ID substring match (case insensitive) */
    for (int i = 0; i < mr->custom_count; i++) {
        if (case_insensitive_contains(mr->custom_models[i].id, pattern)) {
            return &mr->custom_models[i];
        }
    }
    for (int i = 0; i < mr->builtin_count; i++) {
        if (case_insensitive_contains(mr->builtin_models[i].id, pattern)) {
            return &mr->builtin_models[i];
        }
    }

    /* Pass 3: name substring match (case insensitive) */
    for (int i = 0; i < mr->custom_count; i++) {
        if (case_insensitive_contains(mr->custom_models[i].name, pattern)) {
            return &mr->custom_models[i];
        }
    }
    for (int i = 0; i < mr->builtin_count; i++) {
        if (case_insensitive_contains(mr->builtin_models[i].name, pattern)) {
            return &mr->builtin_models[i];
        }
    }

    /* Pass 4: provider+name composite substring */
    for (int i = 0; i < mr->builtin_count; i++) {
        char composite[256];
        snprintf(composite, sizeof(composite), "%s %s",
            mr->builtin_models[i].provider ? mr->builtin_models[i].provider : "",
            mr->builtin_models[i].name ? mr->builtin_models[i].name : "");
        if (case_insensitive_contains(composite, pattern)) {
            return &mr->builtin_models[i];
        }
    }
    for (int i = 0; i < mr->custom_count; i++) {
        char composite[256];
        snprintf(composite, sizeof(composite), "%s %s",
            mr->custom_models[i].provider ? mr->custom_models[i].provider : "",
            mr->custom_models[i].name ? mr->custom_models[i].name : "");
        if (case_insensitive_contains(composite, pattern)) {
            return &mr->custom_models[i];
        }
    }

    return NULL;
}

/* ---- Custom model loading ---- */

static char *json_get_string(cJSON *obj, const char *key) {
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item && cJSON_IsString(item) && item->valuestring) {
        return strdup(item->valuestring);
    }
    return NULL;
}

static int json_get_int(cJSON *obj, const char *key, int def) {
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item && cJSON_IsNumber(item)) return item->valueint;
    return def;
}

static double json_get_double(cJSON *obj, const char *key, double def) {
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item && cJSON_IsNumber(item)) return item->valuedouble;
    return def;
}

static Model parse_model_json(cJSON *obj) {
    Model m = {0};
    m.id = json_get_string(obj, "id");
    m.name = json_get_string(obj, "name");
    m.api = json_get_string(obj, "api");
    m.provider = json_get_string(obj, "provider");
    m.base_url = json_get_string(obj, "base_url");
    m.reasoning = cJSON_IsTrue(cJSON_GetObjectItem(obj, "reasoning"));
    m.context_window = json_get_int(obj, "context_window", 128000);
    m.max_tokens = json_get_int(obj, "max_tokens", 4096);

    cJSON *cost = cJSON_GetObjectItem(obj, "cost_per_million");
    if (cost) {
        m.cost_per_million.input = json_get_double(cost, "input", 0);
        m.cost_per_million.output = json_get_double(cost, "output", 0);
        m.cost_per_million.cache_read = json_get_double(cost, "cache_read", 0);
        m.cost_per_million.cache_write = json_get_double(cost, "cache_write", 0);
    }

    cJSON *modalities = cJSON_GetObjectItem(obj, "input_modalities");
    if (modalities && cJSON_IsArray(modalities)) {
        int mc = cJSON_GetArraySize(modalities);
        const char **mods = calloc((size_t)(mc + 1), sizeof(char *));
        for (int i = 0; i < mc; i++) {
            cJSON *item = cJSON_GetArrayItem(modalities, i);
            if (cJSON_IsString(item)) {
                mods[i] = strdup(item->valuestring);
            }
        }
        m.input_modalities = mods;
        m.input_modality_count = mc;
    }

    return m;
}

int model_registry_load_custom(ModelRegistry *mr, const char *path) {
    if (!mr || !path) return -1;

    FILE *f = fopen(path, "r");
    if (!f) {
        LOG_DEBUG("No custom models file: %s", path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 10 * 1024 * 1024) {
        fclose(f);
        return -1;
    }

    char *buf = malloc((size_t)size + 1);
    size_t read_bytes = fread(buf, 1, (size_t)size, f);
    buf[read_bytes] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        LOG_ERROR("Failed to parse custom models JSON: %s", path);
        return -1;
    }

    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        LOG_ERROR("Custom models file must be a JSON array: %s", path);
        return -1;
    }

    int count = cJSON_GetArraySize(root);
    if (count <= 0) {
        cJSON_Delete(root);
        return 0;
    }

    /* Free existing custom models */
    for (int i = 0; i < mr->custom_count; i++) {
        model_destroy(&mr->custom_models[i]);
    }
    free(mr->custom_models);

    mr->custom_models = calloc((size_t)count, sizeof(Model));
    mr->custom_count = 0;

    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_GetArrayItem(root, i);
        if (!cJSON_IsObject(item)) continue;

        Model m = parse_model_json(item);
        if (m.id) {
            mr->custom_models[mr->custom_count++] = m;
        } else {
            model_destroy(&m);
        }
    }

    cJSON_Delete(root);
    LOG_INFO("Loaded %d custom models from %s", mr->custom_count, path);
    return 0;
}

/* ---- Listing ---- */

Model **model_registry_list(ModelRegistry *mr, int *count) {
    if (!mr || !count) return NULL;

    int total = mr->builtin_count + mr->custom_count;
    if (total <= 0) {
        *count = 0;
        return NULL;
    }

    Model **list = calloc((size_t)total, sizeof(Model *));
    int n = 0;
    for (int i = 0; i < mr->builtin_count; i++) {
        list[n++] = &mr->builtin_models[i];
    }
    for (int i = 0; i < mr->custom_count; i++) {
        list[n++] = &mr->custom_models[i];
    }

    *count = n;
    return list;
}

Model **model_registry_list_provider(ModelRegistry *mr, const char *provider, int *count) {
    if (!mr || !provider || !count) return NULL;

    int total = mr->builtin_count + mr->custom_count;
    Model **list = calloc((size_t)(total > 0 ? total : 1), sizeof(Model *));
    int n = 0;

    for (int i = 0; i < mr->builtin_count; i++) {
        if (mr->builtin_models[i].provider &&
            strcmp(mr->builtin_models[i].provider, provider) == 0) {
            list[n++] = &mr->builtin_models[i];
        }
    }
    for (int i = 0; i < mr->custom_count; i++) {
        if (mr->custom_models[i].provider &&
            strcmp(mr->custom_models[i].provider, provider) == 0) {
            list[n++] = &mr->custom_models[i];
        }
    }

    *count = n;
    if (n == 0) {
        free(list);
        return NULL;
    }
    return list;
}
