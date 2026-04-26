#ifndef PI_HARNESS_TOOLS_H
#define PI_HARNESS_TOOLS_H

#include "ai/types.h"

Tool tool_bash_create(const char *cwd);
Tool tool_read_create(void);
Tool tool_write_create(void);
Tool tool_edit_create(void);
Tool tool_grep_create(void);
Tool tool_ls_create(void);

#endif
