#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include "ai/types.h"
#include "ai/models.h"
#include "ai/registry.h"
#include "ai/providers/anthropic.h"
#include "ai/providers/openai.h"
#include "ai/providers/google.h"
#include "ai/providers/bedrock.h"
#include "ai/providers/mistral.h"
#include "harness/auth.h"
#include "harness/config.h"
#include "harness/tools/tools.h"
#include "harness/modes/print.h"
#include "harness/modes/interactive.h"
#include "rig.h"
#include "util/http.h"
#include "util/fs.h"
#include "util/log.h"

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options] [prompt]\n"
        "       %s auth              Set up API key / provider\n"
        "       %s auth logout       Remove saved credentials\n"
        "       %s auth status       Show current auth config\n"
        "\n"
        "Options:\n"
        "  -p, --print          Print mode (single-shot, output to stdout)\n"
        "  -m, --model MODEL    Model to use (e.g., claude-opus-4-7, gpt-4o)\n"
        "  -s, --session ID     Resume an existing session by ID\n"
        "  --provider PROVIDER  Provider (e.g., anthropic, openai, bedrock)\n"
        "  --thinking LEVEL     Thinking level (off, minimal, low, medium, high, xhigh)\n"
        "  --json               JSON event output mode\n"
        "  --no-tools           Disable built-in tools\n"
        "  -v, --verbose        Verbose logging\n"
        "  -h, --help           Show this help\n"
        "\n"
        "Examples:\n"
        "  %s                                   Interactive mode\n"
        "  %s auth                               Set up credentials\n"
        "  %s --session abc123                   Resume session\n"
        "  %s -p \"What is 2+2?\"\n"
        "  %s -p -m claude-sonnet-4-6 \"Explain quicksort\"\n"
        "\n", prog, prog, prog, prog, prog, prog, prog, prog, prog);
}

static ThinkingLevel parse_thinking(const char *s) {
    if (!s) return THINKING_OFF;
    if (strcmp(s, "off") == 0) return THINKING_OFF;
    if (strcmp(s, "minimal") == 0) return THINKING_MINIMAL;
    if (strcmp(s, "low") == 0) return THINKING_LOW;
    if (strcmp(s, "medium") == 0) return THINKING_MEDIUM;
    if (strcmp(s, "high") == 0) return THINKING_HIGH;
    if (strcmp(s, "xhigh") == 0) return THINKING_XHIGH;
    fprintf(stderr, "Warning: unknown thinking level '%s', using 'off'\n", s);
    return THINKING_OFF;
}

static int cmd_auth(int argc, char **argv) {
    if (argc >= 3 && strcmp(argv[2], "logout") == 0) {
        int rc = auth_logout();
        if (rc == 0) {
            fprintf(stderr, "Logged out. Credentials removed.\n");
        } else {
            fprintf(stderr, "No credentials to remove.\n");
        }
        return rc == 0 ? 0 : 1;
    }

    if (argc >= 3 && strcmp(argv[2], "status") == 0) {
        AuthCredentials *creds = auth_load();
        if (creds && creds->provider) {
            fprintf(stderr, "Provider: %s\n", creds->provider);
            if (creds->api_key) {
                int len = (int)strlen(creds->api_key);
                if (len > 8) {
                    fprintf(stderr, "API Key:  %.*s...%s\n", 4, creds->api_key, creds->api_key + len - 4);
                } else {
                    fprintf(stderr, "API Key:  ****\n");
                }
            }
            if (creds->aws_access_key) {
                fprintf(stderr, "AWS Key:  %.*s....\n", 4, creds->aws_access_key);
            }
            if (creds->aws_region) {
                fprintf(stderr, "Region:   %s\n", creds->aws_region);
            }
            fprintf(stderr, "Config:   %s\n", config_auth_path());
            auth_credentials_free(creds);
        } else {
            auth_credentials_free(creds);
            if (auth_is_configured()) {
                fprintf(stderr, "Auth: via environment variables\n");
            } else {
                fprintf(stderr, "No credentials configured.\n");
                fprintf(stderr, "Run: rig auth\n");
            }
        }
        return 0;
    }

    return auth_interactive_setup();
}

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "auth") == 0) {
        return cmd_auth(argc, argv);
    }

    bool print_mode = false;
    bool json_mode = false;
    bool no_tools = false;
    const char *model_pattern = NULL;
    const char *provider = NULL;
    const char *thinking_str = NULL;
    const char *session_id = NULL;

    static struct option long_opts[] = {
        {"print",    no_argument,       0, 'p'},
        {"model",    required_argument, 0, 'm'},
        {"provider", required_argument, 0, 'P'},
        {"thinking", required_argument, 0, 't'},
        {"session",  required_argument, 0, 's'},
        {"json",     no_argument,       0, 'j'},
        {"no-tools", no_argument,       0, 'T'},
        {"verbose",  no_argument,       0, 'v'},
        {"help",     no_argument,       0, 'h'},
        {0, 0, 0, 0},
    };

    int c;
    while ((c = getopt_long(argc, argv, "pm:s:vhj", long_opts, NULL)) != -1) {
        switch (c) {
        case 'p': print_mode = true; break;
        case 'm': model_pattern = optarg; break;
        case 'P': provider = optarg; break;
        case 't': thinking_str = optarg; break;
        case 's': session_id = optarg; break;
        case 'j': json_mode = true; print_mode = true; break;
        case 'T': no_tools = true; break;
        case 'v': rig_log_set_level(LOG_DEBUG); break;
        case 'h': usage(argv[0]); return 0;
        default: usage(argv[0]); return 1;
        }
    }

    char *prompt = NULL;
    if (optind < argc) {
        prompt = argv[optind];
    } else if (print_mode && !isatty(STDIN_FILENO)) {
        size_t cap = 4096, len = 0;
        prompt = malloc(cap);
        while (1) {
            size_t n = fread(prompt + len, 1, cap - len - 1, stdin);
            if (n == 0) break;
            len += n;
            if (len >= cap - 1) { cap *= 2; prompt = realloc(prompt, cap); }
        }
        prompt[len] = '\0';
    }

    if (!print_mode) {
        RigInstance *rig = rig_create();
        if (!rig) {
            fprintf(stderr, "Error: Failed to create Rig instance\n");
            return 1;
        }
        int rc = interactive_mode_start(rig, session_id, model_pattern, provider);
        rig_free(rig);
        return rc;
    }

    if (!prompt || !prompt[0]) {
        fprintf(stderr, "Error: No prompt provided. Use: rig -p \"your prompt\"\n");
        return 1;
    }

    /* Open log file for print mode too */
    const char *agent_dir = config_agent_dir();
    if (agent_dir) {
        fs_mkdir_p(agent_dir);
        char log_path[512];
        snprintf(log_path, sizeof(log_path), "%s/rig.log", agent_dir);
        rig_log_open(log_path);
    }

    http_global_init();
    ai_registry_init();
    models_init();
    anthropic_register();
    openai_completions_register();
    openai_responses_register();
    google_provider_register();
    bedrock_provider_register();
    mistral_provider_register();

    const char *effective_provider = provider;
    if (!effective_provider) {
        effective_provider = auth_get_active_provider();
    }

    const Model *model = NULL;
    char *api_key = NULL;

    int all_count = 0;
    const Model **all_models = models_get_all(effective_provider, &all_count);
    for (int i = 0; i < all_count && !api_key; i++) {
        if (model_pattern) {
            if (!strstr(all_models[i]->id, model_pattern) &&
                !strstr(all_models[i]->name, model_pattern)) continue;
        }
        char *key = auth_get_api_key(all_models[i]->provider);
        if (key) { model = all_models[i]; api_key = key; }
    }

    if (model_pattern && !model) {
        fprintf(stderr, "Error: Model '%s' not found or no API key available\n", model_pattern);
        return 1;
    }
    if (!model) {
        fprintf(stderr, "Error: No API key found. Set one of:\n");
        fprintf(stderr, "  ANTHROPIC_ARIG_KEY, OPENAI_ARIG_KEY, GOOGLE_ARIG_KEY,\n");
        fprintf(stderr, "  MISTRAL_ARIG_KEY, AWS_ACCESS_KEY_ID\n");
        return 1;
    }

    char cwd[4096];
    getcwd(cwd, sizeof(cwd));

    Tool tools[6];
    int tool_count = 0;
    if (!no_tools) {
        tools[tool_count++] = tool_bash_create(cwd);
        tools[tool_count++] = tool_read_create();
        tools[tool_count++] = tool_write_create();
        tools[tool_count++] = tool_edit_create();
        tools[tool_count++] = tool_grep_create();
    }

    ThinkingLevel thinking = parse_thinking(thinking_str);

    PrintModeOptions opts = {
        .model = model,
        .api_key = api_key,
        .prompt = prompt,
        .cwd = cwd,
        .tools = no_tools ? NULL : tools,
        .tool_count = no_tools ? 0 : tool_count,
        .json_mode = json_mode,
        .thinking = thinking,
    };

    int rc = print_mode_run(&opts);

    free(api_key);
    ai_registry_cleanup();
    http_global_cleanup();

    return rc;
}
