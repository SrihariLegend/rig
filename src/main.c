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
#include "pi.h"
#include "util/http.h"
#include "util/log.h"

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options] [prompt]\n"
        "\n"
        "Options:\n"
        "  -p, --print          Print mode (single-shot, output to stdout)\n"
        "  -m, --model MODEL    Model to use (e.g., claude-opus-4-7, claude-sonnet-4-6)\n"
        "  -s, --session ID     Resume an existing session by ID\n"
        "  --provider PROVIDER  Provider (e.g., anthropic, openai)\n"
        "  --thinking LEVEL     Thinking level (off, minimal, low, medium, high, xhigh)\n"
        "  --json               JSON event output mode\n"
        "  --no-tools           Disable built-in tools\n"
        "  -v, --verbose        Verbose logging\n"
        "  -h, --help           Show this help\n"
        "\n"
        "Examples:\n"
        "  %s                                   Interactive mode\n"
        "  %s --session abc123                   Resume session\n"
        "  %s -p \"What is 2+2?\"\n"
        "  %s -p -m claude-sonnet-4-6 \"Explain quicksort\"\n"
        "  echo \"Fix this bug\" | %s -p\n"
        "\n", prog, prog, prog, prog, prog);
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

int main(int argc, char **argv) {
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
        case 'v': pi_log_set_level(LOG_DEBUG); break;
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
        PiInstance *pi = pi_create();
        if (!pi) {
            fprintf(stderr, "Error: Failed to create Pi instance\n");
            return 1;
        }
        int rc = interactive_mode_start(pi, session_id);
        pi_free(pi);
        return rc;
    }

    if (!prompt || !prompt[0]) {
        fprintf(stderr, "Error: No prompt provided. Use: pi -p \"your prompt\"\n");
        return 1;
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

    const Model *model = NULL;
    char *api_key = NULL;

    if (model_pattern) {
        model = models_get(provider, model_pattern);
        if (!model) {
            fprintf(stderr, "Error: Model '%s' not found\n", model_pattern);
            return 1;
        }
        api_key = auth_get_api_key(model->provider);
        if (!api_key) {
            fprintf(stderr, "Error: No API key for provider '%s'. Set %s_API_KEY\n",
                    model->provider, model->provider);
            return 1;
        }
    } else {
        static const char *try_models[] = {
            "claude-sonnet-4-6", "gpt-4o", "gemini-2.0-flash", NULL
        };
        for (int i = 0; try_models[i]; i++) {
            const Model *m = models_get(NULL, try_models[i]);
            if (m) {
                char *key = auth_get_api_key(m->provider);
                if (key) { model = m; api_key = key; break; }
            }
        }
        if (!model) {
            fprintf(stderr, "Error: No API key found. Set ANTHROPIC_API_KEY, OPENAI_API_KEY, etc.\n");
            return 1;
        }
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
        tools[tool_count++] = tool_ls_create();
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
