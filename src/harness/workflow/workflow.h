#ifndef RIG_WORKFLOW_H
#define RIG_WORKFLOW_H

#include "cjson/cJSON.h"
#include <stdbool.h>

typedef enum {
    STEP_PROMPT,
    STEP_TOOL,
    STEP_BASH,
    STEP_GATE,
    STEP_CONDITION,
    STEP_PARALLEL,
    STEP_JOIN,
    STEP_SUB_WORKFLOW,
    STEP_TRANSFORM,
    STEP_LOOP,
    STEP_RETRY,
    STEP_EMIT,
    STEP_WAIT_EVENT,
    STEP_SPAWN_SESSION,
    STEP_HTTP,
    STEP_CHECKPOINT,
} StepType;

typedef enum {
    GATE_USER_CONFIRM,
    GATE_USER_INPUT,
    GATE_WEBHOOK,
    GATE_FILE_WATCH,
    GATE_CUSTOM,
} GateType;

typedef enum {
    STEP_STATUS_PENDING,
    STEP_STATUS_RUNNING,
    STEP_STATUS_SUCCESS,
    STEP_STATUS_ERROR,
    STEP_STATUS_SKIPPED,
} StepStatus;

typedef struct WorkflowStep WorkflowStep;
struct WorkflowStep {
    char *name;
    StepType type;
    cJSON *config;

    char **inputs;
    int input_count;
    char *save_as;

    char *condition;
    char *then_step;
    char *else_step;
    char *goto_step;
    int max_iterations;

    char **depends_on;
    int depends_count;
    char *parallel_with;

    char *on_success;
    char *on_failure;
    int max_retries;
    int retry_delay_ms;
    int timeout_ms;

    WorkflowStep **parallel_steps;
    int parallel_count;

    char *loop_over;
    WorkflowStep *loop_body;

    struct {
        char *method;
        char *url;
        cJSON *headers;
        char *body;
        int timeout_ms;
        int max_retries;
        char *response_path;
    } http;
};

typedef struct {
    char *name;
    char *description;
    char *trigger;
    WorkflowStep *steps;
    int step_count;
    cJSON *defaults;
} Workflow;

typedef struct WorkflowContext WorkflowContext;
struct WorkflowContext {
    Workflow *workflow;
    cJSON *variables;
    cJSON *metadata;
    void *agent;
    void *rig;
    void (*on_step_complete)(WorkflowContext *ctx, WorkflowStep *step, cJSON *result);
    void (*on_gate_request)(WorkflowContext *ctx, WorkflowStep *step, const char *message);
    bool aborted;
    char *checkpoint_path;
    StepStatus *step_statuses;
    int *iteration_counts;
};

Workflow *workflow_parse_yaml(const char *path);
Workflow *workflow_parse_json(const char *json_str);
int workflow_validate(Workflow *wf, char **error);
void workflow_free(Workflow *wf);
void workflow_step_free(WorkflowStep *step);

WorkflowContext *workflow_context_create(Workflow *wf);
void workflow_context_free(WorkflowContext *ctx);

int workflow_execute(Workflow *wf, WorkflowContext *ctx);
void workflow_abort(WorkflowContext *ctx);

char *workflow_resolve_var(WorkflowContext *ctx, const char *expr);

int workflow_checkpoint(WorkflowContext *ctx, const char *path);
int workflow_resume(const char *checkpoint_path, WorkflowContext *ctx);

#endif
