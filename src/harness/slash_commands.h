#ifndef PI_HARNESS_SLASH_COMMANDS_H
#define PI_HARNESS_SLASH_COMMANDS_H

#include <stdbool.h>

typedef struct {
    char *name;          /* e.g., "help", "model", "clear" */
    char *description;
    char *usage;         /* e.g., "/model <pattern>" */
    int (*handler)(const char **args, int argc, void *ctx);
    void *ctx;
    bool hidden;         /* don't show in /help */
} SlashCommand;

typedef struct {
    SlashCommand *commands;
    int count;
    int capacity;
} SlashCommandRegistry;

SlashCommandRegistry *slash_registry_create(void);
void slash_registry_free(SlashCommandRegistry *reg);

int slash_register(SlashCommandRegistry *reg, SlashCommand *cmd);
int slash_execute(SlashCommandRegistry *reg, const char *input, void *ctx);

/* Returns list of matching commands for autocomplete */
SlashCommand **slash_complete(SlashCommandRegistry *reg, const char *prefix, int *count);

/* Register built-in commands: /help, /clear, /model, /session, /compact, /export, /quit */
void slash_register_builtins(SlashCommandRegistry *reg);

#endif
