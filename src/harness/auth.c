#include "auth.h"
#include <stdlib.h>
#include <string.h>

static const struct { const char *provider; const char *env_vars[4]; } key_map[] = {
    { "anthropic", { "ANTHROPIC_OAUTH_TOKEN", "ANTHROPIC_API_KEY", NULL } },
    { "openai",    { "OPENAI_API_KEY", NULL } },
    { "google",    { "GOOGLE_API_KEY", "GEMINI_API_KEY", NULL } },
    { "mistral",   { "MISTRAL_API_KEY", NULL } },
    { "deepseek",  { "DEEPSEEK_API_KEY", NULL } },
    { "xai",       { "XAI_API_KEY", NULL } },
    { "groq",      { "GROQ_API_KEY", NULL } },
    { "openrouter", { "OPENROUTER_API_KEY", NULL } },
};

char *auth_get_api_key(const char *provider) {
    if (!provider) return NULL;

    for (int i = 0; i < (int)(sizeof(key_map) / sizeof(key_map[0])); i++) {
        if (strcmp(key_map[i].provider, provider) == 0) {
            for (int j = 0; key_map[i].env_vars[j]; j++) {
                const char *val = getenv(key_map[i].env_vars[j]);
                if (val && val[0]) return strdup(val);
            }
            return NULL;
        }
    }
    return NULL;
}
