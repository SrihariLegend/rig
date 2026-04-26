#ifndef PI_HARNESS_SYSTEM_PROMPT_H
#define PI_HARNESS_SYSTEM_PROMPT_H

#include "ai/types.h"

char *system_prompt_build(const Tool *tools, int tool_count, const char *cwd);

#endif
