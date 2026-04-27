#ifndef RIG_WORKFLOW_EXPR_H
#define RIG_WORKFLOW_EXPR_H

#include "workflow.h"
#include <stdbool.h>

bool expr_eval(WorkflowContext *ctx, const char *expr);

#endif
