#include "workflow.h"
#include "expr.h"
#include "util/fs.h"
#include "util/str.h"
#include "util/log.h"
#include "util/process.h"
#include "util/http.h"
#include "ai/types.h"
#include "harness/extensions/extension.h"
#include "cjson/cJSON.h"
#include <yaml.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

static StepType step_type_from_string(const char *s) {
    if (!s) return STEP_PROMPT;
    if (strcmp(s, "prompt") == 0) return STEP_PROMPT;
    if (strcmp(s, "tool") == 0) return STEP_TOOL;
    if (strcmp(s, "bash") == 0) return STEP_BASH;
    if (strcmp(s, "gate") == 0) return STEP_GATE;
    if (strcmp(s, "condition") == 0) return STEP_CONDITION;
    if (strcmp(s, "parallel") == 0) return STEP_PARALLEL;
    if (strcmp(s, "join") == 0) return STEP_JOIN;
    if (strcmp(s, "sub_workflow") == 0) return STEP_SUB_WORKFLOW;
    if (strcmp(s, "transform") == 0) return STEP_TRANSFORM;
    if (strcmp(s, "loop") == 0) return STEP_LOOP;
    if (strcmp(s, "retry") == 0) return STEP_RETRY;
    if (strcmp(s, "emit") == 0) return STEP_EMIT;
    if (strcmp(s, "wait_event") == 0) return STEP_WAIT_EVENT;
    if (strcmp(s, "spawn_session") == 0) return STEP_SPAWN_SESSION;
    if (strcmp(s, "http") == 0) return STEP_HTTP;
    if (strcmp(s, "checkpoint") == 0) return STEP_CHECKPOINT;
    return STEP_PROMPT;
}

static const char *step_type_to_string(StepType t) {
    switch (t) {
        case STEP_PROMPT: return "prompt";
        case STEP_TOOL: return "tool";
        case STEP_BASH: return "bash";
        case STEP_GATE: return "gate";
        case STEP_CONDITION: return "condition";
        case STEP_PARALLEL: return "parallel";
        case STEP_JOIN: return "join";
        case STEP_SUB_WORKFLOW: return "sub_workflow";
        case STEP_TRANSFORM: return "transform";
        case STEP_LOOP: return "loop";
        case STEP_RETRY: return "retry";
        case STEP_EMIT: return "emit";
        case STEP_WAIT_EVENT: return "wait_event";
        case STEP_SPAWN_SESSION: return "spawn_session";
        case STEP_HTTP: return "http";
        case STEP_CHECKPOINT: return "checkpoint";
    }
    return "prompt";
}

void workflow_step_free(WorkflowStep *step) {
    if (!step) return;
    free(step->name);
    if (step->config) cJSON_Delete(step->config);
    for (int i = 0; i < step->input_count; i++) free(step->inputs[i]);
    free(step->inputs);
    free(step->save_as);
    free(step->condition);
    free(step->then_step);
    free(step->else_step);
    free(step->goto_step);
    for (int i = 0; i < step->depends_count; i++) free(step->depends_on[i]);
    free(step->depends_on);
    free(step->parallel_with);
    free(step->on_success);
    free(step->on_failure);
    for (int i = 0; i < step->parallel_count; i++) {
        workflow_step_free(step->parallel_steps[i]);
        free(step->parallel_steps[i]);
    }
    free(step->parallel_steps);
    free(step->loop_over);
    if (step->loop_body) {
        workflow_step_free(step->loop_body);
        free(step->loop_body);
    }
    free(step->http.method);
    free(step->http.url);
    if (step->http.headers) cJSON_Delete(step->http.headers);
    free(step->http.body);
    free(step->http.response_path);
}

void workflow_free(Workflow *wf) {
    if (!wf) return;
    free(wf->name);
    free(wf->description);
    free(wf->trigger);
    for (int i = 0; i < wf->step_count; i++) {
        workflow_step_free(&wf->steps[i]);
    }
    free(wf->steps);
    if (wf->defaults) cJSON_Delete(wf->defaults);
    free(wf);
}

static char *json_get_string(cJSON *obj, const char *key) {
    cJSON *item = cJSON_GetObjectItem(obj, key);
    return (item && cJSON_IsString(item)) ? strdup(item->valuestring) : NULL;
}

static int json_get_int(cJSON *obj, const char *key, int def) {
    cJSON *item = cJSON_GetObjectItem(obj, key);
    return (item && cJSON_IsNumber(item)) ? (int)item->valuedouble : def;
}

static WorkflowStep parse_step_json(cJSON *step_json) {
    WorkflowStep step = {0};

    step.name = json_get_string(step_json, "name");
    step.type = step_type_from_string(
        cJSON_GetObjectItem(step_json, "type") ?
        cJSON_GetObjectItem(step_json, "type")->valuestring : NULL);

    cJSON *config = cJSON_GetObjectItem(step_json, "config");
    if (!config) config = cJSON_GetObjectItem(step_json, "args");
    step.config = config ? cJSON_Duplicate(config, true) : NULL;

    step.save_as = json_get_string(step_json, "save_as");
    step.condition = json_get_string(step_json, "condition");
    step.then_step = json_get_string(step_json, "then");
    step.else_step = json_get_string(step_json, "else");
    step.goto_step = json_get_string(step_json, "goto");
    step.max_iterations = json_get_int(step_json, "max_iterations", 10);
    step.parallel_with = json_get_string(step_json, "parallel_with");
    step.on_success = json_get_string(step_json, "on_success");
    step.on_failure = json_get_string(step_json, "on_failure");
    step.max_retries = json_get_int(step_json, "max_retries", 0);
    step.retry_delay_ms = json_get_int(step_json, "retry_delay_ms", 1000);
    step.timeout_ms = json_get_int(step_json, "timeout_ms", 0);

    cJSON *prompt = cJSON_GetObjectItem(step_json, "prompt");
    if (prompt && cJSON_IsString(prompt) && !step.config) {
        step.config = cJSON_CreateObject();
        cJSON_AddStringToObject(step.config, "prompt", prompt->valuestring);
    }

    cJSON *tool = cJSON_GetObjectItem(step_json, "tool");
    if (tool && cJSON_IsString(tool) && step.type == STEP_PROMPT) {
        step.type = STEP_TOOL;
        if (!step.config) step.config = cJSON_CreateObject();
        cJSON_AddStringToObject(step.config, "tool", tool->valuestring);
    }

    cJSON *gate = cJSON_GetObjectItem(step_json, "gate");
    if (gate && cJSON_IsString(gate)) {
        step.type = STEP_GATE;
        if (!step.config) step.config = cJSON_CreateObject();
        cJSON_AddStringToObject(step.config, "gate", gate->valuestring);
    }

    cJSON *message = cJSON_GetObjectItem(step_json, "message");
    if (message && cJSON_IsString(message)) {
        if (!step.config) step.config = cJSON_CreateObject();
        cJSON_AddStringToObject(step.config, "message", message->valuestring);
    }

    cJSON *loop_over = cJSON_GetObjectItem(step_json, "loop_over");
    if (loop_over && cJSON_IsString(loop_over)) {
        step.loop_over = strdup(loop_over->valuestring);
        step.type = STEP_LOOP;
    }

    cJSON *loop_body_json = cJSON_GetObjectItem(step_json, "loop_body");
    if (loop_body_json && cJSON_IsObject(loop_body_json)) {
        step.loop_body = calloc(1, sizeof(WorkflowStep));
        *step.loop_body = parse_step_json(loop_body_json);
    }

    cJSON *transform = cJSON_GetObjectItem(step_json, "transform");
    if (transform && cJSON_IsString(transform)) {
        step.type = STEP_TRANSFORM;
        if (!step.config) step.config = cJSON_CreateObject();
        cJSON_AddStringToObject(step.config, "transform", transform->valuestring);
    }

    cJSON *depends = cJSON_GetObjectItem(step_json, "depends_on");
    if (depends && cJSON_IsArray(depends)) {
        step.depends_count = cJSON_GetArraySize(depends);
        step.depends_on = calloc(step.depends_count, sizeof(char *));
        for (int i = 0; i < step.depends_count; i++) {
            cJSON *dep = cJSON_GetArrayItem(depends, i);
            step.depends_on[i] = cJSON_IsString(dep) ? strdup(dep->valuestring) : strdup("");
        }
    }

    cJSON *parallel = cJSON_GetObjectItem(step_json, "steps");
    if (parallel && cJSON_IsArray(parallel) && step.type == STEP_PARALLEL) {
        step.parallel_count = cJSON_GetArraySize(parallel);
        step.parallel_steps = calloc(step.parallel_count, sizeof(WorkflowStep *));
        for (int i = 0; i < step.parallel_count; i++) {
            step.parallel_steps[i] = calloc(1, sizeof(WorkflowStep));
            *step.parallel_steps[i] = parse_step_json(cJSON_GetArrayItem(parallel, i));
        }
    }

    cJSON *http_obj = cJSON_GetObjectItem(step_json, "http");
    if (http_obj && cJSON_IsObject(http_obj)) {
        step.type = STEP_HTTP;
        step.http.method = json_get_string(http_obj, "method");
        step.http.url = json_get_string(http_obj, "url");
        step.http.body = json_get_string(http_obj, "body");
        step.http.response_path = json_get_string(http_obj, "response_path");
        step.http.timeout_ms = json_get_int(http_obj, "timeout_ms", 30000);
        step.http.max_retries = json_get_int(http_obj, "max_retries", 0);
        cJSON *headers = cJSON_GetObjectItem(http_obj, "headers");
        step.http.headers = headers ? cJSON_Duplicate(headers, true) : NULL;
    }

    return step;
}

Workflow *workflow_parse_json(const char *json_str) {
    if (!json_str) return NULL;

    cJSON *root = cJSON_Parse(json_str);
    if (!root) return NULL;

    Workflow *wf = calloc(1, sizeof(Workflow));
    if (!wf) {
        cJSON_Delete(root);
        return NULL;
    }

    wf->name = json_get_string(root, "name");
    wf->description = json_get_string(root, "description");
    wf->trigger = json_get_string(root, "trigger");

    cJSON *defaults = cJSON_GetObjectItem(root, "defaults");
    wf->defaults = defaults ? cJSON_Duplicate(defaults, true) : NULL;

    cJSON *steps = cJSON_GetObjectItem(root, "steps");
    if (steps && cJSON_IsArray(steps)) {
        wf->step_count = cJSON_GetArraySize(steps);
        wf->steps = calloc(wf->step_count, sizeof(WorkflowStep));
        for (int i = 0; i < wf->step_count; i++) {
            wf->steps[i] = parse_step_json(cJSON_GetArrayItem(steps, i));
        }
    }

    cJSON_Delete(root);
    return wf;
}

/* ---- libyaml document-tree helpers for workflow_parse_yaml ---- */

static yaml_node_t *yaml_map_lookup(yaml_document_t *doc, yaml_node_t *map,
                                     const char *key) {
    if (!map || map->type != YAML_MAPPING_NODE) return NULL;
    for (yaml_node_pair_t *p = map->data.mapping.pairs.start;
         p < map->data.mapping.pairs.top; p++) {
        yaml_node_t *k = yaml_document_get_node(doc, p->key);
        if (k && k->type == YAML_SCALAR_NODE &&
            strcmp((const char *)k->data.scalar.value, key) == 0) {
            return yaml_document_get_node(doc, p->value);
        }
    }
    return NULL;
}

static char *yaml_scalar_strdup(yaml_node_t *node) {
    if (!node || node->type != YAML_SCALAR_NODE) return NULL;
    return strdup((const char *)node->data.scalar.value);
}

static int yaml_scalar_int(yaml_node_t *node, int def) {
    if (!node || node->type != YAML_SCALAR_NODE) return def;
    return atoi((const char *)node->data.scalar.value);
}

static cJSON *yaml_node_to_cjson(yaml_document_t *doc, yaml_node_t *node);

static cJSON *yaml_mapping_to_cjson(yaml_document_t *doc, yaml_node_t *node) {
    if (!node || node->type != YAML_MAPPING_NODE) return NULL;
    cJSON *obj = cJSON_CreateObject();
    for (yaml_node_pair_t *p = node->data.mapping.pairs.start;
         p < node->data.mapping.pairs.top; p++) {
        yaml_node_t *k = yaml_document_get_node(doc, p->key);
        yaml_node_t *v = yaml_document_get_node(doc, p->value);
        if (k && k->type == YAML_SCALAR_NODE) {
            cJSON *child = yaml_node_to_cjson(doc, v);
            if (child)
                cJSON_AddItemToObject(obj, (const char *)k->data.scalar.value,
                                      child);
        }
    }
    return obj;
}

static cJSON *yaml_node_to_cjson(yaml_document_t *doc, yaml_node_t *node) {
    if (!node) return NULL;
    switch (node->type) {
        case YAML_SCALAR_NODE:
            return cJSON_CreateString((const char *)node->data.scalar.value);
        case YAML_MAPPING_NODE:
            return yaml_mapping_to_cjson(doc, node);
        case YAML_SEQUENCE_NODE: {
            cJSON *arr = cJSON_CreateArray();
            for (yaml_node_item_t *it = node->data.sequence.items.start;
                 it < node->data.sequence.items.top; it++) {
                cJSON *child = yaml_node_to_cjson(doc,
                    yaml_document_get_node(doc, *it));
                if (child) cJSON_AddItemToArray(arr, child);
            }
            return arr;
        }
        default:
            return NULL;
    }
}

static WorkflowStep parse_step_yaml(yaml_document_t *doc, yaml_node_t *node) {
    WorkflowStep step = {0};
    step.max_iterations = 10;
    if (!node || node->type != YAML_MAPPING_NODE) return step;

    step.name = yaml_scalar_strdup(yaml_map_lookup(doc, node, "name"));

    yaml_node_t *type_node = yaml_map_lookup(doc, node, "type");
    step.type = step_type_from_string(
        type_node && type_node->type == YAML_SCALAR_NODE
            ? (const char *)type_node->data.scalar.value : NULL);

    /* config / args (mapping -> cJSON object) */
    yaml_node_t *config_node = yaml_map_lookup(doc, node, "config");
    if (!config_node) config_node = yaml_map_lookup(doc, node, "args");
    if (config_node && config_node->type == YAML_MAPPING_NODE)
        step.config = yaml_mapping_to_cjson(doc, config_node);

    /* prompt shorthand */
    yaml_node_t *prompt_node = yaml_map_lookup(doc, node, "prompt");
    if (prompt_node && prompt_node->type == YAML_SCALAR_NODE && !step.config) {
        step.config = cJSON_CreateObject();
        cJSON_AddStringToObject(step.config, "prompt",
            (const char *)prompt_node->data.scalar.value);
    }

    /* tool shorthand */
    yaml_node_t *tool_node = yaml_map_lookup(doc, node, "tool");
    if (tool_node && tool_node->type == YAML_SCALAR_NODE &&
        step.type != STEP_GATE) {
        step.type = STEP_TOOL;
        if (!step.config) step.config = cJSON_CreateObject();
        cJSON_AddStringToObject(step.config, "tool",
            (const char *)tool_node->data.scalar.value);
    }

    /* gate shorthand */
    yaml_node_t *gate_node = yaml_map_lookup(doc, node, "gate");
    if (gate_node && gate_node->type == YAML_SCALAR_NODE) {
        step.type = STEP_GATE;
        if (!step.config) step.config = cJSON_CreateObject();
        cJSON_AddStringToObject(step.config, "gate",
            (const char *)gate_node->data.scalar.value);
    }

    /* message shorthand */
    yaml_node_t *msg_node = yaml_map_lookup(doc, node, "message");
    if (msg_node && msg_node->type == YAML_SCALAR_NODE) {
        if (!step.config) step.config = cJSON_CreateObject();
        cJSON_AddStringToObject(step.config, "message",
            (const char *)msg_node->data.scalar.value);
    }

    /* model shorthand */
    yaml_node_t *model_node = yaml_map_lookup(doc, node, "model");
    if (model_node && model_node->type == YAML_SCALAR_NODE) {
        if (!step.config) step.config = cJSON_CreateObject();
        cJSON_AddStringToObject(step.config, "model",
            (const char *)model_node->data.scalar.value);
    }

    /* transform shorthand */
    yaml_node_t *transform_node = yaml_map_lookup(doc, node, "transform");
    if (transform_node && transform_node->type == YAML_SCALAR_NODE) {
        step.type = STEP_TRANSFORM;
        if (!step.config) step.config = cJSON_CreateObject();
        cJSON_AddStringToObject(step.config, "transform",
            (const char *)transform_node->data.scalar.value);
    }

    /* loop_over */
    yaml_node_t *loop_node = yaml_map_lookup(doc, node, "loop_over");
    if (loop_node && loop_node->type == YAML_SCALAR_NODE) {
        step.loop_over = strdup((const char *)loop_node->data.scalar.value);
        step.type = STEP_LOOP;
    }

    /* scalar fields */
    step.save_as = yaml_scalar_strdup(yaml_map_lookup(doc, node, "save_as"));
    step.condition = yaml_scalar_strdup(yaml_map_lookup(doc, node, "condition"));
    step.then_step = yaml_scalar_strdup(yaml_map_lookup(doc, node, "then"));
    step.else_step = yaml_scalar_strdup(yaml_map_lookup(doc, node, "else"));
    step.goto_step = yaml_scalar_strdup(yaml_map_lookup(doc, node, "goto"));
    step.parallel_with = yaml_scalar_strdup(
        yaml_map_lookup(doc, node, "parallel_with"));
    step.on_success = yaml_scalar_strdup(
        yaml_map_lookup(doc, node, "on_success"));
    step.on_failure = yaml_scalar_strdup(
        yaml_map_lookup(doc, node, "on_failure"));

    /* integer fields */
    step.max_iterations = yaml_scalar_int(
        yaml_map_lookup(doc, node, "max_iterations"), 10);
    step.max_retries = yaml_scalar_int(
        yaml_map_lookup(doc, node, "max_retries"), 0);
    step.retry_delay_ms = yaml_scalar_int(
        yaml_map_lookup(doc, node, "retry_delay_ms"), 1000);
    step.timeout_ms = yaml_scalar_int(
        yaml_map_lookup(doc, node, "timeout_ms"), 0);

    /* depends_on (sequence of strings) */
    yaml_node_t *deps_node = yaml_map_lookup(doc, node, "depends_on");
    if (deps_node && deps_node->type == YAML_SEQUENCE_NODE) {
        int count = (int)(deps_node->data.sequence.items.top -
                          deps_node->data.sequence.items.start);
        step.depends_count = count;
        step.depends_on = calloc(count, sizeof(char *));
        for (int i = 0; i < count; i++) {
            yaml_node_t *item = yaml_document_get_node(doc,
                deps_node->data.sequence.items.start[i]);
            step.depends_on[i] = (item && item->type == YAML_SCALAR_NODE)
                ? strdup((const char *)item->data.scalar.value)
                : strdup("");
        }
    }

    /* nested parallel steps */
    yaml_node_t *par_node = yaml_map_lookup(doc, node, "steps");
    if (par_node && par_node->type == YAML_SEQUENCE_NODE &&
        step.type == STEP_PARALLEL) {
        int count = (int)(par_node->data.sequence.items.top -
                          par_node->data.sequence.items.start);
        step.parallel_count = count;
        step.parallel_steps = calloc(count, sizeof(WorkflowStep *));
        for (int i = 0; i < count; i++) {
            yaml_node_t *child = yaml_document_get_node(doc,
                par_node->data.sequence.items.start[i]);
            step.parallel_steps[i] = calloc(1, sizeof(WorkflowStep));
            *step.parallel_steps[i] = parse_step_yaml(doc, child);
        }
    }

    /* http block */
    yaml_node_t *http_node = yaml_map_lookup(doc, node, "http");
    if (http_node && http_node->type == YAML_MAPPING_NODE) {
        step.type = STEP_HTTP;
        step.http.method = yaml_scalar_strdup(
            yaml_map_lookup(doc, http_node, "method"));
        step.http.url = yaml_scalar_strdup(
            yaml_map_lookup(doc, http_node, "url"));
        step.http.body = yaml_scalar_strdup(
            yaml_map_lookup(doc, http_node, "body"));
        step.http.response_path = yaml_scalar_strdup(
            yaml_map_lookup(doc, http_node, "response_path"));
        step.http.timeout_ms = yaml_scalar_int(
            yaml_map_lookup(doc, http_node, "timeout_ms"), 30000);
        step.http.max_retries = yaml_scalar_int(
            yaml_map_lookup(doc, http_node, "max_retries"), 0);
        yaml_node_t *hdrs = yaml_map_lookup(doc, http_node, "headers");
        step.http.headers = hdrs ? yaml_mapping_to_cjson(doc, hdrs) : NULL;
    }

    return step;
}

Workflow *workflow_parse_yaml(const char *path) {
    if (!path) return NULL;

    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser)) {
        fclose(fp);
        return NULL;
    }
    yaml_parser_set_input_file(&parser, fp);

    yaml_document_t doc;
    if (!yaml_parser_load(&parser, &doc)) {
        yaml_parser_delete(&parser);
        fclose(fp);
        return NULL;
    }

    yaml_node_t *root = yaml_document_get_root_node(&doc);
    if (!root || root->type != YAML_MAPPING_NODE) {
        yaml_document_delete(&doc);
        yaml_parser_delete(&parser);
        fclose(fp);
        return NULL;
    }

    Workflow *wf = calloc(1, sizeof(Workflow));
    if (!wf) {
        yaml_document_delete(&doc);
        yaml_parser_delete(&parser);
        fclose(fp);
        return NULL;
    }

    wf->name = yaml_scalar_strdup(yaml_map_lookup(&doc, root, "name"));
    wf->description = yaml_scalar_strdup(
        yaml_map_lookup(&doc, root, "description"));
    wf->trigger = yaml_scalar_strdup(
        yaml_map_lookup(&doc, root, "trigger"));

    yaml_node_t *defaults_node = yaml_map_lookup(&doc, root, "defaults");
    if (defaults_node && defaults_node->type == YAML_MAPPING_NODE)
        wf->defaults = yaml_mapping_to_cjson(&doc, defaults_node);

    yaml_node_t *steps_node = yaml_map_lookup(&doc, root, "steps");
    if (steps_node && steps_node->type == YAML_SEQUENCE_NODE) {
        int count = (int)(steps_node->data.sequence.items.top -
                          steps_node->data.sequence.items.start);
        wf->step_count = count;
        wf->steps = calloc(count, sizeof(WorkflowStep));
        for (int i = 0; i < count; i++) {
            yaml_node_t *sn = yaml_document_get_node(&doc,
                steps_node->data.sequence.items.start[i]);
            wf->steps[i] = parse_step_yaml(&doc, sn);
        }
    }

    yaml_document_delete(&doc);
    yaml_parser_delete(&parser);
    fclose(fp);
    return wf;
}

int workflow_validate(Workflow *wf, char **error) {
    if (!wf) {
        if (error) *error = strdup("null workflow");
        return -1;
    }

    if (!wf->name || strlen(wf->name) == 0) {
        if (error) *error = strdup("workflow missing name");
        return -1;
    }

    if (wf->step_count == 0) {
        if (error) *error = strdup("workflow has no steps");
        return -1;
    }

    for (int i = 0; i < wf->step_count; i++) {
        if (!wf->steps[i].name || strlen(wf->steps[i].name) == 0) {
            if (error) {
                char buf[128];
                snprintf(buf, sizeof(buf), "step %d missing name", i);
                *error = strdup(buf);
            }
            return -1;
        }

        for (int j = i + 1; j < wf->step_count; j++) {
            if (strcmp(wf->steps[i].name, wf->steps[j].name) == 0) {
                if (error) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "duplicate step name: %s", wf->steps[i].name);
                    *error = strdup(buf);
                }
                return -1;
            }
        }
    }

    return 0;
}

WorkflowContext *workflow_context_create(Workflow *wf) {
    if (!wf) return NULL;

    WorkflowContext *ctx = calloc(1, sizeof(WorkflowContext));
    if (!ctx) return NULL;

    ctx->workflow = wf;
    ctx->variables = cJSON_CreateObject();
    ctx->metadata = cJSON_CreateObject();

    if (wf->defaults) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, wf->defaults) {
            cJSON_AddItemToObject(ctx->variables, item->string,
                                  cJSON_Duplicate(item, true));
        }
    }

    ctx->step_statuses = calloc(wf->step_count, sizeof(StepStatus));
    ctx->iteration_counts = calloc(wf->step_count, sizeof(int));

    return ctx;
}

void workflow_context_free(WorkflowContext *ctx) {
    if (!ctx) return;
    if (ctx->variables) cJSON_Delete(ctx->variables);
    if (ctx->metadata) cJSON_Delete(ctx->metadata);
    free(ctx->checkpoint_path);
    free(ctx->step_statuses);
    free(ctx->iteration_counts);
    free(ctx);
}

char *workflow_resolve_var(WorkflowContext *ctx, const char *expr) {
    if (!ctx || !expr) return NULL;

    if (strncmp(expr, "env.", 4) == 0) {
        const char *val = getenv(expr + 4);
        return val ? strdup(val) : strdup("");
    }

    char *dot = strchr(expr, '.');
    if (dot) {
        char *var_name = strndup(expr, dot - expr);
        const char *field = dot + 1;

        if (strcmp(field, "status") == 0) {
            for (int i = 0; i < ctx->workflow->step_count; i++) {
                if (strcmp(ctx->workflow->steps[i].name, var_name) == 0) {
                    free(var_name);
                    switch (ctx->step_statuses[i]) {
                        case STEP_STATUS_SUCCESS: return strdup("success");
                        case STEP_STATUS_ERROR: return strdup("error");
                        case STEP_STATUS_SKIPPED: return strdup("skipped");
                        case STEP_STATUS_RUNNING: return strdup("running");
                        default: return strdup("pending");
                    }
                }
            }
        }

        cJSON *var = cJSON_GetObjectItem(ctx->variables, var_name);
        free(var_name);

        if (var && cJSON_IsObject(var)) {
            cJSON *sub = cJSON_GetObjectItem(var, field);
            if (sub) {
                if (cJSON_IsString(sub)) return strdup(sub->valuestring);
                char *s = cJSON_PrintUnformatted(sub);
                return s;
            }
        }
        return strdup("");
    }

    cJSON *var = cJSON_GetObjectItem(ctx->variables, expr);
    if (!var) return strdup("");

    if (cJSON_IsString(var)) return strdup(var->valuestring);
    char *s = cJSON_PrintUnformatted(var);
    return s ? s : strdup("");
}

static char *interpolate_vars(WorkflowContext *ctx, const char *template) {
    if (!template) return NULL;

    Str result = str_new(strlen(template) + 64);
    const char *p = template;

    while (*p) {
        if (*p == '$' && *(p + 1) == '{') {
            const char *start = p + 2;
            const char *end = strchr(start, '}');
            if (end) {
                char *expr = strndup(start, end - start);
                char *val = workflow_resolve_var(ctx, expr);
                str_append(&result, val ? val : "");
                free(expr);
                free(val);
                p = end + 1;
                continue;
            }
        }
        str_append_char(&result, *p);
        p++;
    }

    char *out = strdup(result.data);
    str_free(&result);
    return out;
}

/* ---- stdout capture callback for process_run ---- */
typedef struct {
    Str buf;
} StdoutCapture;

static void capture_stdout(const char *data, size_t len, void *user) {
    StdoutCapture *cap = user;
    str_append_len(&cap->buf, data, len);
}

/* ---- execute a single step on an ad-hoc 1-step workflow (for loop/parallel/retry) ---- */
static int execute_step(WorkflowContext *ctx, int step_idx);

/* Callback to capture substep result. */
typedef struct {
    cJSON *captured;
} SubstepCapture;

static void substep_on_complete(WorkflowContext *c, WorkflowStep *s, cJSON *r) {
    (void)c; (void)s;
    SubstepCapture *cap = c->agent; /* borrowing agent pointer for capture ctx */
    if (cap && r) {
        if (cap->captured) cJSON_Delete(cap->captured);
        cap->captured = cJSON_Duplicate(r, true);
    }
}

static int execute_substep(WorkflowContext *ctx, WorkflowStep *step, cJSON *result) {
    /* Build a temporary 1-step workflow so we can reuse execute_step. */
    Workflow tmp_wf = {
        .name = "substep",
        .steps = step,
        .step_count = 1,
    };
    StepStatus tmp_status = STEP_STATUS_PENDING;
    int tmp_iter = 0;
    SubstepCapture cap = { .captured = NULL };

    WorkflowContext tmp_ctx = {
        .workflow = &tmp_wf,
        .variables = ctx->variables,
        .metadata = ctx->metadata,
        .agent = &cap,
        .pi = ctx->pi,
        .on_step_complete = substep_on_complete,
        .on_gate_request = ctx->on_gate_request,
        .aborted = false,
        .checkpoint_path = NULL,
        .step_statuses = &tmp_status,
        .iteration_counts = &tmp_iter,
    };

    int rc = execute_step(&tmp_ctx, 0);

    /* Copy captured output into result. */
    if (cap.captured) {
        cJSON *out = cJSON_GetObjectItem(cap.captured, "output");
        if (out) {
            cJSON_DeleteItemFromObject(result, "output");
            cJSON_AddItemToObject(result, "output", cJSON_Duplicate(out, true));
        }
        cJSON_Delete(cap.captured);
    }

    return (tmp_status == STEP_STATUS_ERROR) ? -1 : rc;
}

/* ---- parallel step thread arg ---- */
typedef struct {
    WorkflowStep *step;
    cJSON *variables;      /* own copy */
    cJSON *metadata;       /* shared read-only ref */
    void *agent;
    void *pi;
    void (*on_gate_request)(WorkflowContext *, WorkflowStep *, const char *);
    int rc;
    StepStatus status;
    char *output;          /* captured output string (owned) */
} ParallelArg;

static void *parallel_thread_fn(void *arg) {
    ParallelArg *pa = arg;

    Workflow tmp_wf = {
        .name = "parallel_sub",
        .steps = pa->step,
        .step_count = 1,
    };
    StepStatus tmp_status = STEP_STATUS_PENDING;
    int tmp_iter = 0;

    WorkflowContext tmp_ctx = {
        .workflow = &tmp_wf,
        .variables = pa->variables,
        .metadata = pa->metadata,
        .agent = pa->agent,
        .pi = pa->pi,
        .on_step_complete = NULL,
        .on_gate_request = pa->on_gate_request,
        .aborted = false,
        .checkpoint_path = NULL,
        .step_statuses = &tmp_status,
        .iteration_counts = &tmp_iter,
    };

    pa->rc = execute_step(&tmp_ctx, 0);
    pa->status = tmp_status;

    /* Grab output from variables if saved. */
    if (pa->step->save_as) {
        cJSON *saved = cJSON_GetObjectItem(pa->variables, pa->step->save_as);
        if (saved && cJSON_IsString(saved)) {
            pa->output = strdup(saved->valuestring);
        } else if (saved) {
            pa->output = cJSON_PrintUnformatted(saved);
        }
    }

    return NULL;
}

static int execute_step(WorkflowContext *ctx, int step_idx) {
    WorkflowStep *step = &ctx->workflow->steps[step_idx];
    ctx->step_statuses[step_idx] = STEP_STATUS_RUNNING;

    if (step->condition && step->type != STEP_CONDITION) {
        char *resolved = interpolate_vars(ctx, step->condition);
        bool skip = (resolved && strlen(resolved) == 0);
        free(resolved);
        if (skip) {
            ctx->step_statuses[step_idx] = STEP_STATUS_SKIPPED;
            return 0;
        }
    }

    cJSON *result = cJSON_CreateObject();
    bool step_failed = false;

    switch (step->type) {
        case STEP_PROMPT: {
            cJSON *prompt = step->config ? cJSON_GetObjectItem(step->config, "prompt") : NULL;
            if (prompt && cJSON_IsString(prompt)) {
                char *resolved = interpolate_vars(ctx, prompt->valuestring);
                cJSON_AddStringToObject(result, "output", resolved ? resolved : "");
                free(resolved);
            }
            break;
        }

        case STEP_BASH: {
            cJSON *cmd = step->config ? cJSON_GetObjectItem(step->config, "command") : NULL;
            if (!cmd || !cJSON_IsString(cmd)) {
                cJSON_AddStringToObject(result, "error", "bash step missing 'command' in config");
                step_failed = true;
                break;
            }
            char *resolved = interpolate_vars(ctx, cmd->valuestring);
            if (!resolved) {
                cJSON_AddStringToObject(result, "error", "failed to interpolate command");
                step_failed = true;
                break;
            }

            StdoutCapture cap = { .buf = str_new(256) };
            ProcessOptions opts = {
                .command = resolved,
                .timeout_ms = step->timeout_ms,
                .on_stdout = capture_stdout,
                .ctx = &cap,
            };
            ProcessResult proc = {0};
            int rc = process_run(&opts, &proc);
            free(resolved);

            if (rc != 0) {
                cJSON_AddStringToObject(result, "error", "process_run failed");
                str_free(&cap.buf);
                step_failed = true;
                break;
            }

            if (proc.timed_out) {
                cJSON_AddStringToObject(result, "error", "command timed out");
                cJSON_AddBoolToObject(result, "timed_out", true);
                str_free(&cap.buf);
                step_failed = true;
                break;
            }

            /* Strip trailing newline for cleaner output. */
            if (cap.buf.len > 0 && cap.buf.data[cap.buf.len - 1] == '\n') {
                cap.buf.data[cap.buf.len - 1] = '\0';
                cap.buf.len--;
            }

            cJSON_AddStringToObject(result, "output", cap.buf.data ? cap.buf.data : "");
            cJSON_AddNumberToObject(result, "exit_code", proc.exit_code);
            str_free(&cap.buf);

            if (proc.exit_code != 0) {
                step_failed = true;
            }
            break;
        }

        case STEP_TOOL: {
            cJSON *tool_name_json = step->config ? cJSON_GetObjectItem(step->config, "tool") : NULL;
            if (!tool_name_json || !cJSON_IsString(tool_name_json)) {
                cJSON_AddStringToObject(result, "error", "tool step missing 'tool' name in config");
                step_failed = true;
                break;
            }
            const char *tool_name = tool_name_json->valuestring;

            PiExtensionAPI *api = (PiExtensionAPI *)ctx->pi;
            if (!api) {
                char buf[256];
                snprintf(buf, sizeof(buf), "no extension API available to execute tool '%s'", tool_name);
                cJSON_AddStringToObject(result, "error", buf);
                step_failed = true;
                break;
            }

            Tool *tool = extension_api_get_tool(api, tool_name);
            if (!tool) {
                char buf[256];
                snprintf(buf, sizeof(buf), "tool '%s' not found in registry", tool_name);
                cJSON_AddStringToObject(result, "error", buf);
                step_failed = true;
                break;
            }

            if (!tool->execute) {
                char buf[256];
                snprintf(buf, sizeof(buf), "tool '%s' has no execute function", tool_name);
                cJSON_AddStringToObject(result, "error", buf);
                step_failed = true;
                break;
            }

            /* Build params from config minus the "tool" key. */
            cJSON *params = cJSON_Duplicate(step->config, true);
            cJSON_DeleteItemFromObject(params, "tool");

            /* Interpolate string values in params. */
            cJSON *child = NULL;
            cJSON_ArrayForEach(child, params) {
                if (cJSON_IsString(child)) {
                    char *interp = interpolate_vars(ctx, child->valuestring);
                    if (interp) {
                        cJSON_SetValuestring(child, interp);
                        free(interp);
                    }
                }
            }

            ContentBlock *content = NULL;
            int content_count = 0;
            cJSON *details = NULL;
            bool terminate = false;

            int rc = tool->execute(
                step->name, params, NULL, NULL, NULL,
                &content, &content_count, &details, &terminate);

            if (rc != 0) {
                cJSON_AddStringToObject(result, "error", "tool execution failed");
                step_failed = true;
            } else if (details) {
                char *details_str = cJSON_PrintUnformatted(details);
                cJSON_AddStringToObject(result, "output", details_str ? details_str : "");
                free(details_str);
            } else {
                cJSON_AddStringToObject(result, "output", "");
            }

            cJSON_Delete(params);
            if (details) cJSON_Delete(details);
            /* Note: content blocks are not freed here since tool ownership semantics vary. */
            break;
        }

        case STEP_HTTP: {
            char *url = step->http.url ? interpolate_vars(ctx, step->http.url) : NULL;
            if (!url || strlen(url) == 0) {
                free(url);
                cJSON_AddStringToObject(result, "error", "http step missing URL");
                step_failed = true;
                break;
            }

            char *body = step->http.body ? interpolate_vars(ctx, step->http.body) : NULL;
            const char *method = step->http.method ? step->http.method : "GET";

            /* Build headers array from cJSON object. */
            const char **hdr_arr = NULL;
            int hdr_count = 0;
            if (step->http.headers && cJSON_IsObject(step->http.headers)) {
                hdr_count = cJSON_GetArraySize(step->http.headers);
                hdr_arr = calloc(hdr_count + 1, sizeof(char *));
                int idx = 0;
                cJSON *h = NULL;
                cJSON_ArrayForEach(h, step->http.headers) {
                    if (cJSON_IsString(h)) {
                        char hbuf[512];
                        snprintf(hbuf, sizeof(hbuf), "%s: %s", h->string, h->valuestring);
                        hdr_arr[idx++] = strdup(hbuf);
                    }
                }
                hdr_arr[idx] = NULL;
            }

            HttpRequest req = {
                .url = url,
                .method = method,
                .headers = hdr_arr,
                .body = body,
                .body_len = body ? strlen(body) : 0,
                .timeout_ms = step->http.timeout_ms,
            };
            HttpResponse resp = {0};

            int rc = http_request(&req, &resp);

            if (rc != 0) {
                cJSON_AddStringToObject(result, "error", "HTTP request failed");
                step_failed = true;
            } else {
                cJSON_AddStringToObject(result, "output", resp.body ? resp.body : "");
                cJSON_AddNumberToObject(result, "status_code", resp.status_code);
            }

            http_response_free(&resp);
            free(url);
            free(body);
            if (hdr_arr) {
                for (int i = 0; hdr_arr[i]; i++) free((char *)hdr_arr[i]);
                free(hdr_arr);
            }
            break;
        }

        case STEP_LOOP: {
            if (!step->loop_over) {
                cJSON_AddStringToObject(result, "error", "loop step missing loop_over");
                step_failed = true;
                break;
            }
            if (!step->loop_body) {
                cJSON_AddStringToObject(result, "error", "loop step missing loop_body");
                step_failed = true;
                break;
            }

            /* Resolve the variable name to a JSON array. */
            cJSON *arr_var = cJSON_GetObjectItem(ctx->variables, step->loop_over);
            if (!arr_var || !cJSON_IsArray(arr_var)) {
                cJSON_AddStringToObject(result, "error", "loop_over variable is not a JSON array");
                step_failed = true;
                break;
            }

            int arr_size = cJSON_GetArraySize(arr_var);
            cJSON *results_arr = cJSON_CreateArray();

            for (int i = 0; i < arr_size && !ctx->aborted; i++) {
                cJSON *item = cJSON_GetArrayItem(arr_var, i);

                /* Set loop.item and loop.index in variables. */
                cJSON *loop_obj = cJSON_GetObjectItem(ctx->variables, "loop");
                if (loop_obj) {
                    cJSON_DeleteItemFromObject(ctx->variables, "loop");
                }
                loop_obj = cJSON_CreateObject();
                cJSON_AddItemToObject(loop_obj, "item", cJSON_Duplicate(item, true));
                cJSON_AddNumberToObject(loop_obj, "index", i);
                cJSON_AddItemToObject(ctx->variables, "loop", loop_obj);

                cJSON *iter_result = cJSON_CreateObject();
                int rc = execute_substep(ctx, step->loop_body, iter_result);
                (void)rc;

                cJSON *out = cJSON_GetObjectItem(iter_result, "output");
                if (out) {
                    cJSON_AddItemToArray(results_arr, cJSON_Duplicate(out, true));
                } else {
                    cJSON_AddItemToArray(results_arr, cJSON_Duplicate(iter_result, true));
                }
                cJSON_Delete(iter_result);
            }

            /* Clean up loop variable. */
            cJSON_DeleteItemFromObject(ctx->variables, "loop");

            char *arr_str = cJSON_PrintUnformatted(results_arr);
            cJSON_AddStringToObject(result, "output", arr_str ? arr_str : "[]");
            free(arr_str);
            cJSON_Delete(results_arr);
            break;
        }

        case STEP_PARALLEL: {
            if (step->parallel_count == 0) {
                cJSON_AddStringToObject(result, "output", "[]");
                break;
            }

            ParallelArg *args = calloc(step->parallel_count, sizeof(ParallelArg));
            pthread_t *threads = calloc(step->parallel_count, sizeof(pthread_t));
            int threads_started = 0;

            for (int i = 0; i < step->parallel_count; i++) {
                args[i].step = step->parallel_steps[i];
                args[i].variables = cJSON_Duplicate(ctx->variables, true);
                args[i].metadata = ctx->metadata;
                args[i].agent = ctx->agent;
                args[i].pi = ctx->pi;
                args[i].on_gate_request = ctx->on_gate_request;
                args[i].output = NULL;

                int rc = pthread_create(&threads[i], NULL, parallel_thread_fn, &args[i]);
                if (rc != 0) {
                    args[i].rc = -1;
                    args[i].status = STEP_STATUS_ERROR;
                } else {
                    threads_started++;
                }
            }

            /* Wait for all threads. */
            for (int i = 0; i < step->parallel_count; i++) {
                if (args[i].status != STEP_STATUS_ERROR || threads_started > 0) {
                    pthread_join(threads[i], NULL);
                }
            }

            /* Collect results. */
            cJSON *par_results = cJSON_CreateArray();
            for (int i = 0; i < step->parallel_count; i++) {
                cJSON *entry = cJSON_CreateObject();
                cJSON_AddStringToObject(entry, "name",
                    step->parallel_steps[i]->name ? step->parallel_steps[i]->name : "");
                if (args[i].output) {
                    cJSON_AddStringToObject(entry, "output", args[i].output);
                }
                cJSON_AddBoolToObject(entry, "success", args[i].status == STEP_STATUS_SUCCESS);

                /* Merge saved variables back into parent context. */
                if (step->parallel_steps[i]->save_as) {
                    cJSON *saved = cJSON_GetObjectItem(args[i].variables,
                                                        step->parallel_steps[i]->save_as);
                    if (saved) {
                        cJSON_DeleteItemFromObject(ctx->variables,
                                                    step->parallel_steps[i]->save_as);
                        cJSON_AddItemToObject(ctx->variables,
                                              step->parallel_steps[i]->save_as,
                                              cJSON_Duplicate(saved, true));
                    }
                }

                cJSON_AddItemToArray(par_results, entry);
                cJSON_Delete(args[i].variables);
                free(args[i].output);
            }

            char *par_str = cJSON_PrintUnformatted(par_results);
            cJSON_AddStringToObject(result, "output", par_str ? par_str : "[]");
            free(par_str);
            cJSON_Delete(par_results);
            free(args);
            free(threads);
            break;
        }

        case STEP_CONDITION: {
            if (!step->condition) {
                cJSON_AddStringToObject(result, "error", "condition step missing condition expression");
                step_failed = true;
                break;
            }
            bool cond_result = expr_eval(ctx, step->condition);
            cJSON_AddBoolToObject(result, "result", cond_result);

            const char *target = cond_result ? step->then_step : step->else_step;
            if (target) {
                cJSON_AddStringToObject(result, "jump_to", target);
                cJSON_AddStringToObject(result, "output", target);
            } else {
                cJSON_AddStringToObject(result, "output", cond_result ? "true" : "false");
            }
            break;
        }

        case STEP_RETRY: {
            /* Retry wraps the step configured in loop_body (reuse the field). */
            if (!step->loop_body) {
                cJSON_AddStringToObject(result, "error", "retry step missing body step");
                step_failed = true;
                break;
            }

            int attempts = step->max_retries > 0 ? step->max_retries : 1;
            int delay_ms = step->retry_delay_ms > 0 ? step->retry_delay_ms : 1000;
            bool succeeded = false;

            for (int attempt = 0; attempt <= attempts && !ctx->aborted; attempt++) {
                if (attempt > 0 && delay_ms > 0) {
                    usleep((useconds_t)delay_ms * 1000);
                }

                cJSON *attempt_result = cJSON_CreateObject();
                int rc = execute_substep(ctx, step->loop_body, attempt_result);

                if (rc == 0) {
                    /* Copy output from successful attempt. */
                    cJSON *out = cJSON_GetObjectItem(attempt_result, "output");
                    if (out) {
                        cJSON_AddItemToObject(result, "output", cJSON_Duplicate(out, true));
                    }
                    cJSON_AddNumberToObject(result, "attempts", attempt + 1);
                    cJSON_Delete(attempt_result);
                    succeeded = true;
                    break;
                }
                cJSON_Delete(attempt_result);
            }

            if (!succeeded) {
                cJSON_AddStringToObject(result, "error", "all retry attempts failed");
                cJSON_AddNumberToObject(result, "attempts", attempts + 1);
                step_failed = true;
            }
            break;
        }

        case STEP_TRANSFORM: {
            cJSON *transform = step->config ? cJSON_GetObjectItem(step->config, "transform") : NULL;
            if (transform && cJSON_IsString(transform)) {
                char *resolved = interpolate_vars(ctx, transform->valuestring);
                cJSON_AddStringToObject(result, "output", resolved ? resolved : "");
                free(resolved);
            }
            break;
        }

        case STEP_GATE: {
            if (ctx->on_gate_request) {
                cJSON *msg = step->config ? cJSON_GetObjectItem(step->config, "message") : NULL;
                ctx->on_gate_request(ctx, step,
                    msg && cJSON_IsString(msg) ? msg->valuestring : "Continue?");
            }
            cJSON_AddStringToObject(result, "result", "yes");
            break;
        }

        case STEP_CHECKPOINT: {
            if (ctx->checkpoint_path) {
                workflow_checkpoint(ctx, ctx->checkpoint_path);
            }
            break;
        }

        case STEP_EMIT: {
            cJSON_AddStringToObject(result, "output", "emitted");
            break;
        }

        default:
            break;
    }

    if (step->save_as) {
        cJSON *output = cJSON_GetObjectItem(result, "output");
        if (output) {
            cJSON_DeleteItemFromObject(ctx->variables, step->save_as);
            cJSON_AddItemToObject(ctx->variables, step->save_as,
                                  cJSON_Duplicate(output, true));
        } else {
            cJSON_DeleteItemFromObject(ctx->variables, step->save_as);
            cJSON_AddItemToObject(ctx->variables, step->save_as,
                                  cJSON_Duplicate(result, true));
        }
    }

    ctx->step_statuses[step_idx] = step_failed ? STEP_STATUS_ERROR : STEP_STATUS_SUCCESS;
    ctx->iteration_counts[step_idx]++;

    if (ctx->on_step_complete) {
        ctx->on_step_complete(ctx, step, result);
    }

    cJSON_Delete(result);

    if (step->goto_step && ctx->iteration_counts[step_idx] < step->max_iterations) {
        for (int i = 0; i < ctx->workflow->step_count; i++) {
            if (strcmp(ctx->workflow->steps[i].name, step->goto_step) == 0) {
                return execute_step(ctx, i);
            }
        }
    }

    return step_failed ? -1 : 0;
}

int workflow_execute(Workflow *wf, WorkflowContext *ctx) {
    if (!wf || !ctx) return -1;

    for (int i = 0; i < wf->step_count; i++) {
        if (ctx->aborted) break;

        int result = execute_step(ctx, i);

        /* For condition steps, follow the jump target. */
        if (wf->steps[i].type == STEP_CONDITION && result == 0) {
            bool cond_val = false;
            if (wf->steps[i].condition) {
                cond_val = expr_eval(ctx, wf->steps[i].condition);
            }
            const char *target = cond_val ? wf->steps[i].then_step : wf->steps[i].else_step;
            if (target) {
                for (int j = 0; j < wf->step_count; j++) {
                    if (strcmp(wf->steps[j].name, target) == 0) {
                        i = j - 1; /* will be incremented by loop */
                        break;
                    }
                }
                continue;
            }
        }

        if (result != 0) return result;

        /* For non-condition steps with then/else, legacy behavior: stop. */
        if (wf->steps[i].type != STEP_CONDITION &&
            (wf->steps[i].then_step || wf->steps[i].else_step)) {
            break;
        }
    }

    return 0;
}

void workflow_abort(WorkflowContext *ctx) {
    if (ctx) ctx->aborted = true;
}

int workflow_checkpoint(WorkflowContext *ctx, const char *path) {
    if (!ctx || !path) return -1;

    cJSON *cp = cJSON_CreateObject();
    cJSON_AddItemToObject(cp, "variables", cJSON_Duplicate(ctx->variables, true));

    cJSON *statuses = cJSON_CreateArray();
    for (int i = 0; i < ctx->workflow->step_count; i++) {
        cJSON_AddItemToArray(statuses, cJSON_CreateNumber(ctx->step_statuses[i]));
    }
    cJSON_AddItemToObject(cp, "statuses", statuses);

    cJSON *iters = cJSON_CreateArray();
    for (int i = 0; i < ctx->workflow->step_count; i++) {
        cJSON_AddItemToArray(iters, cJSON_CreateNumber(ctx->iteration_counts[i]));
    }
    cJSON_AddItemToObject(cp, "iterations", iters);

    char *json_str = cJSON_Print(cp);
    cJSON_Delete(cp);
    if (!json_str) return -1;

    int result = fs_write_file(path, json_str, strlen(json_str));
    free(json_str);
    return result;
}

int workflow_resume(const char *checkpoint_path, WorkflowContext *ctx) {
    if (!checkpoint_path || !ctx) return -1;

    size_t len;
    char *content = fs_read_file(checkpoint_path, &len);
    if (!content) return -1;

    cJSON *cp = cJSON_Parse(content);
    free(content);
    if (!cp) return -1;

    cJSON *vars = cJSON_GetObjectItem(cp, "variables");
    if (vars) {
        if (ctx->variables) cJSON_Delete(ctx->variables);
        ctx->variables = cJSON_Duplicate(vars, true);
    }

    cJSON *statuses = cJSON_GetObjectItem(cp, "statuses");
    if (statuses && cJSON_IsArray(statuses)) {
        int count = cJSON_GetArraySize(statuses);
        for (int i = 0; i < count && i < ctx->workflow->step_count; i++) {
            ctx->step_statuses[i] = (StepStatus)cJSON_GetArrayItem(statuses, i)->valueint;
        }
    }

    cJSON *iters = cJSON_GetObjectItem(cp, "iterations");
    if (iters && cJSON_IsArray(iters)) {
        int count = cJSON_GetArraySize(iters);
        for (int i = 0; i < count && i < ctx->workflow->step_count; i++) {
            ctx->iteration_counts[i] = cJSON_GetArrayItem(iters, i)->valueint;
        }
    }

    cJSON_Delete(cp);

    int resume_from = -1;
    for (int i = 0; i < ctx->workflow->step_count; i++) {
        if (ctx->step_statuses[i] == STEP_STATUS_PENDING) {
            resume_from = i;
            break;
        }
    }

    if (resume_from >= 0) {
        for (int i = resume_from; i < ctx->workflow->step_count; i++) {
            if (ctx->aborted) break;
            execute_step(ctx, i);
        }
    }

    return 0;
}
