#include "models.h"
#include <string.h>

static const char *text_image[] = {"text", "image", NULL};

static Model builtin_models[] = {
    {
        .id = "claude-opus-4-7-20250929",
        .name = "Claude Opus 4.7",
        .api = "anthropic-messages",
        .provider = "anthropic",
        .base_url = "https://api.anthropic.com",
        .reasoning = true,
        .input_modalities = text_image,
        .input_modality_count = 2,
        .cost_per_million = { .input = 15.0, .output = 75.0, .cache_read = 1.5, .cache_write = 18.75 },
        .context_window = 200000,
        .max_tokens = 32000,
        .compat_type = COMPAT_ANTHROPIC,
        .compat.anthropic = { .supports_eager_tool_input_streaming = true, .supports_long_cache_retention = true },
    },
    {
        .id = "claude-opus-4-6-20250501",
        .name = "Claude Opus 4.6",
        .api = "anthropic-messages",
        .provider = "anthropic",
        .base_url = "https://api.anthropic.com",
        .reasoning = true,
        .input_modalities = text_image,
        .input_modality_count = 2,
        .cost_per_million = { .input = 15.0, .output = 75.0, .cache_read = 1.5, .cache_write = 18.75 },
        .context_window = 200000,
        .max_tokens = 32000,
        .compat_type = COMPAT_ANTHROPIC,
        .compat.anthropic = { .supports_eager_tool_input_streaming = true, .supports_long_cache_retention = true },
    },
    {
        .id = "claude-sonnet-4-6-20250514",
        .name = "Claude Sonnet 4.6",
        .api = "anthropic-messages",
        .provider = "anthropic",
        .base_url = "https://api.anthropic.com",
        .reasoning = true,
        .input_modalities = text_image,
        .input_modality_count = 2,
        .cost_per_million = { .input = 3.0, .output = 15.0, .cache_read = 0.3, .cache_write = 3.75 },
        .context_window = 200000,
        .max_tokens = 16000,
        .compat_type = COMPAT_ANTHROPIC,
        .compat.anthropic = { .supports_eager_tool_input_streaming = true, .supports_long_cache_retention = true },
    },
    {
        .id = "claude-haiku-4-5-20251001",
        .name = "Claude Haiku 4.5",
        .api = "anthropic-messages",
        .provider = "anthropic",
        .base_url = "https://api.anthropic.com",
        .reasoning = false,
        .input_modalities = text_image,
        .input_modality_count = 2,
        .cost_per_million = { .input = 0.8, .output = 4.0, .cache_read = 0.08, .cache_write = 1.0 },
        .context_window = 200000,
        .max_tokens = 8192,
        .compat_type = COMPAT_ANTHROPIC,
        .compat.anthropic = { .supports_eager_tool_input_streaming = false, .supports_long_cache_retention = false },
    },
    {
        .id = "gpt-4o-2024-11-20",
        .name = "GPT-4o",
        .api = "openai-completions",
        .provider = "openai",
        .base_url = "https://api.openai.com/v1",
        .reasoning = false,
        .input_modalities = text_image,
        .input_modality_count = 2,
        .cost_per_million = { .input = 2.5, .output = 10.0, .cache_read = 1.25, .cache_write = 0 },
        .context_window = 128000,
        .max_tokens = 16384,
        .compat_type = COMPAT_OPENAI_COMPLETIONS,
    },
};

static const int builtin_model_count = sizeof(builtin_models) / sizeof(builtin_models[0]);

void models_init(void) {
}

const Model *models_get(const char *provider, const char *model_id) {
    if (!model_id) return NULL;
    for (int i = 0; i < builtin_model_count; i++) {
        if (provider && strcmp(builtin_models[i].provider, provider) != 0) continue;
        if (strcmp(builtin_models[i].id, model_id) == 0) return &builtin_models[i];
    }
    for (int i = 0; i < builtin_model_count; i++) {
        if (provider && strcmp(builtin_models[i].provider, provider) != 0) continue;
        if (strstr(builtin_models[i].id, model_id)) return &builtin_models[i];
    }
    return NULL;
}

const Model **models_get_all(const char *provider, int *count) {
    static const Model *result[32];
    int n = 0;
    for (int i = 0; i < builtin_model_count && n < 32; i++) {
        if (!provider || strcmp(builtin_models[i].provider, provider) == 0) {
            result[n++] = &builtin_models[i];
        }
    }
    *count = n;
    return result;
}

const char **models_get_providers(int *count) {
    static const char *providers[16];
    int n = 0;
    for (int i = 0; i < builtin_model_count; i++) {
        bool found = false;
        for (int j = 0; j < n; j++) {
            if (strcmp(providers[j], builtin_models[i].provider) == 0) { found = true; break; }
        }
        if (!found && n < 16) providers[n++] = builtin_models[i].provider;
    }
    *count = n;
    return providers;
}
