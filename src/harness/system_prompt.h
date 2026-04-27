#ifndef PI_HARNESS_SYSTEM_PROMPT_H
#define PI_HARNESS_SYSTEM_PROMPT_H

#include "ai/types.h"

char *system_prompt_build(const Tool *tools, int tool_count, const char *cwd);

void system_prompt_add_fragment(const char *key, const char *text);
void system_prompt_remove_fragment(const char *key);

#endif
