#include "auth.h"
#include "config.h"
#include "util/fs.h"
#include "util/json.h"
#include "cjson/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <sys/stat.h>

static const struct { const char *provider; const char *env_vars[4]; } key_map[] = {
    { "anthropic",  { "ANTHROPIC_OAUTH_TOKEN", "ANTHROPIC_ARIG_KEY", NULL } },
    { "openai",     { "OPENAI_ARIG_KEY", NULL } },
    { "google",     { "GOOGLE_ARIG_KEY", "GEMINI_ARIG_KEY", NULL } },
    { "mistral",    { "MISTRAL_ARIG_KEY", NULL } },
    { "deepseek",   { "DEEPSEEK_ARIG_KEY", NULL } },
    { "xai",        { "XAI_ARIG_KEY", NULL } },
    { "groq",       { "GROQ_ARIG_KEY", NULL } },
    { "openrouter", { "OPENROUTER_ARIG_KEY", NULL } },
    { "bedrock",    { "AWS_BEARER_TOKEN_BEDROCK", "BEDROCK_ARIG_KEY", "AWS_ACCESS_KEY_ID", NULL } },
};

static const int key_map_count = sizeof(key_map) / sizeof(key_map[0]);

static char *get_env_key(const char *provider) {
    for (int i = 0; i < key_map_count; i++) {
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

AuthCredentials *auth_load(void) {
    const char *path = config_auth_path();
    if (!path) return NULL;

    size_t len;
    char *content = fs_read_file(path, &len);
    if (!content) return NULL;

    cJSON *root = cJSON_Parse(content);
    free(content);
    if (!root) return NULL;

    AuthCredentials *creds = calloc(1, sizeof(AuthCredentials));
    if (!creds) { cJSON_Delete(root); return NULL; }

    cJSON *p = cJSON_GetObjectItem(root, "provider");
    if (p && cJSON_IsString(p)) creds->provider = strdup(p->valuestring);

    cJSON *k = cJSON_GetObjectItem(root, "api_key");
    if (k && cJSON_IsString(k)) creds->api_key = strdup(k->valuestring);

    cJSON *ak = cJSON_GetObjectItem(root, "aws_access_key");
    if (ak && cJSON_IsString(ak)) creds->aws_access_key = strdup(ak->valuestring);

    cJSON *sk = cJSON_GetObjectItem(root, "aws_secret_key");
    if (sk && cJSON_IsString(sk)) creds->aws_secret_key = strdup(sk->valuestring);

    cJSON *ar = cJSON_GetObjectItem(root, "aws_region");
    if (ar && cJSON_IsString(ar)) creds->aws_region = strdup(ar->valuestring);

    cJSON *st = cJSON_GetObjectItem(root, "aws_session_token");
    if (st && cJSON_IsString(st)) creds->aws_session_token = strdup(st->valuestring);

    cJSON_Delete(root);
    return creds;
}

void auth_credentials_free(AuthCredentials *creds) {
    if (!creds) return;
    free(creds->provider);
    free(creds->api_key);
    free(creds->aws_access_key);
    free(creds->aws_secret_key);
    free(creds->aws_region);
    free(creds->aws_session_token);
    free(creds);
}

int auth_save(const char *provider, const char *api_key,
              const char *aws_access_key, const char *aws_secret_key,
              const char *aws_region, const char *aws_session_token) {
    if (!provider) return -1;

    const char *path = config_auth_path();
    if (!path) return -1;

    const char *dir = config_agent_dir();
    fs_mkdir_p(dir);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "provider", provider);

    if (api_key) cJSON_AddStringToObject(root, "api_key", api_key);
    if (aws_access_key) cJSON_AddStringToObject(root, "aws_access_key", aws_access_key);
    if (aws_secret_key) cJSON_AddStringToObject(root, "aws_secret_key", aws_secret_key);
    if (aws_region) cJSON_AddStringToObject(root, "aws_region", aws_region);
    if (aws_session_token) cJSON_AddStringToObject(root, "aws_session_token", aws_session_token);

    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);
    if (!json_str) return -1;

    int result = fs_write_file(path, json_str, strlen(json_str));
    free(json_str);

    if (result == 0) {
        chmod(path, 0600);
    }

    return result;
}

int auth_logout(void) {
    const char *path = config_auth_path();
    if (!path) return -1;
    if (fs_exists(path)) {
        return unlink(path);
    }
    return 0;
}

bool auth_is_configured(void) {
    AuthCredentials *creds = auth_load();
    if (creds && creds->provider) {
        auth_credentials_free(creds);
        return true;
    }
    auth_credentials_free(creds);

    for (int i = 0; i < key_map_count; i++) {
        char *key = get_env_key(key_map[i].provider);
        if (key) { free(key); return true; }
    }
    return false;
}

const char *auth_get_active_provider(void) {
    static char provider_buf[64];

    AuthCredentials *creds = auth_load();
    if (creds && creds->provider) {
        strncpy(provider_buf, creds->provider, sizeof(provider_buf) - 1);
        auth_credentials_free(creds);
        return provider_buf;
    }
    auth_credentials_free(creds);
    return NULL;
}

char *auth_get_api_key(const char *provider) {
    if (!provider) return NULL;

    AuthCredentials *creds = auth_load();
    if (creds && creds->provider && strcmp(creds->provider, provider) == 0) {
        if (strcmp(provider, "bedrock") == 0) {
            if (creds->api_key) {
                setenv("AWS_BEARER_TOKEN_BEDROCK", creds->api_key, 0);
                setenv("BEDROCK_ARIG_KEY", creds->api_key, 0);
                if (creds->aws_region)
                    setenv("AWS_REGION", creds->aws_region, 0);
                char *key = strdup(creds->api_key);
                auth_credentials_free(creds);
                return key;
            }
            if (creds->aws_access_key) {
                setenv("AWS_ACCESS_KEY_ID", creds->aws_access_key, 0);
                if (creds->aws_secret_key)
                    setenv("AWS_SECRET_ACCESS_KEY", creds->aws_secret_key, 0);
                if (creds->aws_region)
                    setenv("AWS_REGION", creds->aws_region, 0);
                if (creds->aws_session_token)
                    setenv("AWS_SESSION_TOKEN", creds->aws_session_token, 0);
                char *key = strdup(creds->aws_access_key);
                auth_credentials_free(creds);
                return key;
            }
        } else if (creds->api_key) {
            char *key = strdup(creds->api_key);
            auth_credentials_free(creds);
            return key;
        }
    }
    auth_credentials_free(creds);

    return get_env_key(provider);
}

static char *read_line_visible(const char *prompt_text) {
    fprintf(stderr, "%s", prompt_text);
    fflush(stderr);

    char buf[1024] = {0};
    if (!fgets(buf, sizeof(buf), stdin)) return NULL;

    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
    if (buf[0] == '\0') return NULL;

    return strdup(buf);
}

static char *read_line_hidden(const char *prompt_text) {
    fprintf(stderr, "%s", prompt_text);
    fflush(stderr);

    struct termios old, new;
    bool tty = isatty(STDIN_FILENO);
    if (tty) {
        tcgetattr(STDIN_FILENO, &old);
        new = old;
        new.c_lflag &= ~(unsigned long)ECHO;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &new);
    }

    char buf[1024] = {0};
    char *result = fgets(buf, sizeof(buf), stdin);

    if (tty) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &old);
        fprintf(stderr, "\n");
    }

    if (!result) return NULL;

    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
    if (buf[0] == '\0') return NULL;

    return strdup(buf);
}

int auth_interactive_setup(void) {
    fprintf(stderr, "\n");
    fprintf(stderr, "  Pi Authentication Setup\n");
    fprintf(stderr, "  =======================\n\n");
    fprintf(stderr, "  Select a provider:\n\n");
    fprintf(stderr, "    1) Anthropic     (Claude models)\n");
    fprintf(stderr, "    2) OpenAI        (GPT models)\n");
    fprintf(stderr, "    3) Google        (Gemini models)\n");
    fprintf(stderr, "    4) AWS Bedrock   (Claude via AWS)\n");
    fprintf(stderr, "    5) Mistral       (Mistral models)\n");
    fprintf(stderr, "    6) DeepSeek\n");
    fprintf(stderr, "    7) xAI           (Grok models)\n");
    fprintf(stderr, "    8) Groq\n");
    fprintf(stderr, "    9) OpenRouter\n");
    fprintf(stderr, "\n");

    char *choice = read_line_visible("  Enter number (1-9): ");
    if (!choice) {
        fprintf(stderr, "  Cancelled.\n");
        return -1;
    }

    int num = atoi(choice);
    free(choice);

    static const char *providers[] = {
        NULL, "anthropic", "openai", "google", "bedrock", "mistral",
        "deepseek", "xai", "groq", "openrouter"
    };

    if (num < 1 || num > 9) {
        fprintf(stderr, "  Invalid choice.\n");
        return -1;
    }

    const char *provider = providers[num];
    fprintf(stderr, "\n  Provider: %s\n\n", provider);

    if (strcmp(provider, "bedrock") == 0) {
        fprintf(stderr, "  Bedrock auth method:\n");
        fprintf(stderr, "    a) API Key (starts with ABSK or similar)\n");
        fprintf(stderr, "    b) IAM Credentials (Access Key + Secret Key)\n\n");

        char *method = read_line_visible("  Enter a or b: ");
        if (!method) { fprintf(stderr, "  Cancelled.\n"); return -1; }

        bool use_apikey = (method[0] == 'a' || method[0] == 'A');
        free(method);

        if (use_apikey) {
            char *api_key = read_line_hidden("  Bedrock API Key: ");
            if (!api_key) { fprintf(stderr, "  Cancelled.\n"); return -1; }

            char *region = read_line_visible("  AWS Region [us-east-1]: ");
            if (!region || !region[0]) {
                free(region);
                region = strdup("us-east-1");
            }

            int rc = auth_save(provider, api_key, NULL, NULL, region, NULL);
            free(api_key);
            free(region);

            if (rc == 0) {
                fprintf(stderr, "\n  Saved to %s\n", config_auth_path());
                fprintf(stderr, "  Provider: bedrock (API key)\n\n");
            }
            return rc;
        }

        char *access = read_line_hidden("  AWS Access Key ID: ");
        if (!access) { fprintf(stderr, "  Cancelled.\n"); return -1; }

        char *secret = read_line_hidden("  AWS Secret Access Key: ");
        if (!secret) { free(access); fprintf(stderr, "  Cancelled.\n"); return -1; }

        char *region = read_line_visible("  AWS Region [us-east-1]: ");
        if (!region || !region[0]) {
            free(region);
            region = strdup("us-east-1");
        }

        char *token = read_line_hidden("  AWS Session Token (optional, press Enter to skip): ");

        int rc = auth_save(provider, NULL, access, secret, region, token);

        free(access);
        free(secret);
        free(region);
        free(token);

        if (rc == 0) {
            fprintf(stderr, "\n  Saved to %s\n", config_auth_path());
            fprintf(stderr, "  Provider: bedrock (IAM)\n\n");
        }
        return rc;
    }

    char *api_key = read_line_hidden("  API Key: ");
    if (!api_key) {
        fprintf(stderr, "  Cancelled.\n");
        return -1;
    }

    int rc = auth_save(provider, api_key, NULL, NULL, NULL, NULL);
    free(api_key);

    if (rc == 0) {
        fprintf(stderr, "\n  Saved to %s\n", config_auth_path());
        fprintf(stderr, "  Provider: %s\n\n", provider);
    }
    return rc;
}
