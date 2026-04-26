#ifndef PI_WORKFLOW_EXPR_H
#define PI_WORKFLOW_EXPR_H

#include "workflow.h"
#include <stdbool.h>

bool expr_eval(WorkflowContext *ctx, const char *expr);

#endif
