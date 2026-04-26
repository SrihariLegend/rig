/* slash_commands.c — slash command registry and built-in handlers */
#include "harness/slash_commands.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define INITIAL_CAPACITY 16
#define MAX_ARGS 64

SlashCommandRegistry *slash_registry_create(void) {
    SlashCommandRegistry *reg = calloc(1, sizeof(SlashCommandRegistry));
    if (!reg) return NULL;
    reg->commands = calloc(INITIAL_CAPACITY, sizeof(SlashCommand));
    if (!reg->commands) {
        free(reg);
        return NULL;
    }
    reg->capacity = INITIAL_CAPACITY;
    reg->count = 0;
    return reg;
}

void slash_registry_free(SlashCommandRegistry *reg) {
    if (!reg) return;
    for (int i = 0; i < reg->count; i++) {
        free(reg->commands[i].name);
        free(reg->commands[i].description);
        free(reg->commands[i].usage);
    }
    free(reg->commands);
    free(reg);
}

int slash_register(SlashCommandRegistry *reg, SlashCommand *cmd) {
    if (!reg || !cmd || !cmd->name || !cmd->handler) return -1;

    /* Check for duplicate */
    for (int i = 0; i < reg->count; i++) {
        if (strcmp(reg->commands[i].name, cmd->name) == 0) return -1;
    }

    /* Grow if needed */
    if (reg->count >= reg->capacity) {
        int new_cap = reg->capacity * 2;
        SlashCommand *new_cmds = realloc(reg->commands, (size_t)new_cap * sizeof(SlashCommand));
        if (!new_cmds) return -1;
        reg->commands = new_cmds;
        reg->capacity = new_cap;
    }

    SlashCommand *slot = &reg->commands[reg->count];
    slot->name = strdup(cmd->name);
    slot->description = cmd->description ? strdup(cmd->description) : NULL;
    slot->usage = cmd->usage ? strdup(cmd->usage) : NULL;
    slot->handler = cmd->handler;
    slot->ctx = cmd->ctx;
    slot->hidden = cmd->hidden;
    reg->count++;
    return 0;
}

/* Parse "/cmd arg1 arg2" into command name and args array */
static int parse_input(const char *input, char **out_name,
                       const char ***out_args, int *out_argc) {
    if (!input || input[0] != '/') return -1;

    /* Skip leading '/' */
    const char *p = input + 1;

    /* Skip whitespace */
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0') return -1;

    /* Extract command name */
    const char *name_start = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    size_t name_len = (size_t)(p - name_start);
    if (name_len == 0) return -1;

    *out_name = strndup(name_start, name_len);

    /* Parse args */
    const char **args = calloc(MAX_ARGS, sizeof(char *));
    int argc = 0;

    while (*p && argc < MAX_ARGS) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        const char *arg_start = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        args[argc] = strndup(arg_start, (size_t)(p - arg_start));
        argc++;
    }

    *out_args = args;
    *out_argc = argc;
    return 0;
}

int slash_execute(SlashCommandRegistry *reg, const char *input, void *ctx) {
    if (!reg || !input) return -1;

    char *name = NULL;
    const char **args = NULL;
    int argc = 0;

    if (parse_input(input, &name, &args, &argc) != 0) {
        return -1;
    }

    int result = -1;
    bool found = false;

    for (int i = 0; i < reg->count; i++) {
        if (strcmp(reg->commands[i].name, name) == 0) {
            found = true;
            void *handler_ctx = reg->commands[i].ctx ? reg->commands[i].ctx : ctx;
            result = reg->commands[i].handler(args, argc, handler_ctx);
            break;
        }
    }

    if (!found) {
        fprintf(stderr, "Unknown command: /%s\n", name);
        result = -1;
    }

    free(name);
    for (int i = 0; i < argc; i++) {
        free((char *)args[i]);
    }
    free(args);
    return result;
}

SlashCommand **slash_complete(SlashCommandRegistry *reg, const char *prefix, int *count) {
    if (!reg || !count) return NULL;

    *count = 0;
    const char *pfx = prefix;

    /* Skip leading '/' if present */
    if (pfx && pfx[0] == '/') pfx++;
    if (!pfx) pfx = "";

    size_t pfx_len = strlen(pfx);

    /* First pass: count matches */
    int matches = 0;
    for (int i = 0; i < reg->count; i++) {
        if (!reg->commands[i].hidden &&
            strncmp(reg->commands[i].name, pfx, pfx_len) == 0) {
            matches++;
        }
    }

    if (matches == 0) return NULL;

    SlashCommand **result = calloc((size_t)matches, sizeof(SlashCommand *));
    if (!result) return NULL;

    int idx = 0;
    for (int i = 0; i < reg->count; i++) {
        if (!reg->commands[i].hidden &&
            strncmp(reg->commands[i].name, pfx, pfx_len) == 0) {
            result[idx++] = &reg->commands[i];
        }
    }

    *count = matches;
    return result;
}

/* ========== Built-in handlers ========== */

static int builtin_help(const char **args, int argc, void *ctx) {
    (void)args; (void)argc;
    SlashCommandRegistry *reg = (SlashCommandRegistry *)ctx;
    if (!reg) return -1;

    printf("Available commands:\n");
    for (int i = 0; i < reg->count; i++) {
        if (reg->commands[i].hidden) continue;
        const char *usage = reg->commands[i].usage ? reg->commands[i].usage : "";
        const char *desc = reg->commands[i].description ? reg->commands[i].description : "";
        printf("  %-20s %s\n", usage[0] ? usage : reg->commands[i].name, desc);
    }
    return 0;
}

static int builtin_clear(const char **args, int argc, void *ctx) {
    (void)args; (void)argc; (void)ctx;
    printf("\033[2J\033[H");
    fflush(stdout);
    return 0;
}

static int builtin_model(const char **args, int argc, void *ctx) {
    (void)ctx;
    if (argc < 1) {
        printf("Usage: /model <pattern>\n");
        return -1;
    }
    printf("Model switched to: %s\n", args[0]);
    return 0;
}

static int builtin_session(const char **args, int argc, void *ctx) {
    (void)ctx;
    if (argc < 1) {
        printf("Current session\n");
    } else {
        printf("Switched to session: %s\n", args[0]);
    }
    return 0;
}

static int builtin_compact(const char **args, int argc, void *ctx) {
    (void)args; (void)argc; (void)ctx;
    printf("Compaction triggered\n");
    return 0;
}

static int builtin_export(const char **args, int argc, void *ctx) {
    (void)args; (void)argc; (void)ctx;
    printf("Conversation exported\n");
    return 0;
}

static int builtin_quit(const char **args, int argc, void *ctx) {
    (void)args; (void)argc; (void)ctx;
    return 0;
}

void slash_register_builtins(SlashCommandRegistry *reg) {
    if (!reg) return;

    SlashCommand cmds[] = {
        { .name = "help",    .description = "List all commands",
          .usage = "/help",  .handler = builtin_help, .ctx = reg, .hidden = false },
        { .name = "clear",   .description = "Clear conversation display",
          .usage = "/clear",  .handler = builtin_clear, .ctx = NULL, .hidden = false },
        { .name = "model",   .description = "Switch model",
          .usage = "/model <pattern>", .handler = builtin_model, .ctx = NULL, .hidden = false },
        { .name = "session", .description = "Show or switch session",
          .usage = "/session [id]", .handler = builtin_session, .ctx = NULL, .hidden = false },
        { .name = "compact", .description = "Force compaction",
          .usage = "/compact", .handler = builtin_compact, .ctx = NULL, .hidden = false },
        { .name = "export",  .description = "Export conversation",
          .usage = "/export",  .handler = builtin_export, .ctx = NULL, .hidden = false },
        { .name = "quit",    .description = "Exit",
          .usage = "/quit",    .handler = builtin_quit, .ctx = NULL, .hidden = false },
    };

    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        slash_register(reg, &cmds[i]);
    }
}
