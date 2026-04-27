#ifndef RIG_HARNESS_PRINT_MODE_H
#define RIG_HARNESS_PRINT_MODE_H

#include "agent/agent.h"

typedef struct {
    const Model *model;
    const char *api_key;
    const char *prompt;
    const char *cwd;
    Tool *tools;
    int tool_count;
    bool json_mode;
    ThinkingLevel thinking;
} PrintModeOptions;

int print_mode_run(PrintModeOptions *opts);

#endif
