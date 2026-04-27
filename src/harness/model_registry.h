#ifndef RIG_HARNESS_MODEL_REGISTRY_H
#define RIG_HARNESS_MODEL_REGISTRY_H

#include "ai/types.h"

typedef struct {
    Model *builtin_models;
    int builtin_count;
    Model *custom_models;    /* from ~/.rig/agent/models.json */
    int custom_count;
} ModelRegistry;

/* Create a registry populated with builtin models */
ModelRegistry *model_registry_create(void);

/* Free the registry and all owned models */
void model_registry_free(ModelRegistry *mr);

/*
 * Resolve a pattern to a model.
 * Tries exact ID match first, then name substring, then provider+name substring.
 * Examples: "sonnet" -> "Claude Sonnet 4.6", "opus" -> "Claude Opus 4.7",
 *           "gpt-4o" -> exact match, "haiku" -> "Claude Haiku 4.5"
 * Returns NULL if no match found.
 */
Model *model_registry_resolve(ModelRegistry *mr, const char *pattern);

/*
 * Load custom models from a JSON file.
 * Expected format: array of model objects with fields matching Model struct.
 * Returns 0 on success, -1 on error.
 */
int model_registry_load_custom(ModelRegistry *mr, const char *path);

/*
 * List all available models (builtin + custom).
 * Returns heap-allocated array of pointers. Caller frees the array (not the models).
 * Sets *count to total number of models.
 */
Model **model_registry_list(ModelRegistry *mr, int *count);

/*
 * List models for a specific provider.
 * Returns heap-allocated array of pointers. Caller frees the array (not the models).
 * Sets *count to number of matching models.
 */
Model **model_registry_list_provider(ModelRegistry *mr, const char *provider, int *count);

#endif
