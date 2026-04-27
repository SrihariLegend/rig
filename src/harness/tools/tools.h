#ifndef RIG_HARNESS_TOOLS_H
#define RIG_HARNESS_TOOLS_H

#include "ai/types.h"
#include "harness/permissions.h"
#include "harness/extensions/extension.h"

Tool tool_bash_create(const char *cwd);
Tool tool_read_create(void);
Tool tool_write_create(void);
Tool tool_edit_create(void);
Tool tool_grep_create(void);
Tool tool_ls_create(void);
Tool tool_introspect_create(void);

void introspect_tool_set_context(RigExtensionAPI *api, PermissionSet *perms,
                                  const Tool *tools, int tool_count,
                                  const char *cwd);

#endif
