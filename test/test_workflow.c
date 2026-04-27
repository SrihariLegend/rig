/* test_workflow.c — tests for workflow parsing, validation, execution, expr */
#include "test.h"
#include "harness/workflow/workflow.h"
#include "harness/workflow/expr.h"
#include "util/fs.h"
#include "util/str.h"
#include "cjson/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ========== Parsing JSON ========== */

TEST(workflow_parse_json_basic) {
    const char *json = "{\"name\":\"test\",\"steps\":[{\"name\":\"s1\",\"type\":\"prompt\",\"config\":{\"prompt\":\"hello\"}}]}";
    Workflow *wf = workflow_parse_json(json);
    ASSERT_NOT_NULL(wf);
    ASSERT_STR_EQ(wf->name, "test");
    ASSERT_EQ(wf->step_count, 1);
    ASSERT_STR_EQ(wf->steps[0].name, "s1");
    ASSERT_EQ(wf->steps[0].type, STEP_PROMPT);
    workflow_free(wf);
}

TEST(workflow_parse_json_null) {
    ASSERT_NULL(workflow_parse_json(NULL));
}

TEST(workflow_parse_json_invalid) {
    ASSERT_NULL(workflow_parse_json("not json"));
}

TEST(workflow_parse_json_bash_step) {
    const char *json = "{\"name\":\"w\",\"steps\":[{\"name\":\"run\",\"type\":\"bash\",\"config\":{\"command\":\"echo hi\"}}]}";
    Workflow *wf = workflow_parse_json(json);
    ASSERT_NOT_NULL(wf);
    ASSERT_EQ(wf->steps[0].type, STEP_BASH);
    workflow_free(wf);
}

TEST(workflow_parse_json_condition) {
    const char *json = "{\"name\":\"w\",\"steps\":[{\"name\":\"cond\",\"type\":\"condition\",\"condition\":\"x == 'yes'\",\"then\":\"s2\",\"else\":\"s3\"}]}";
    Workflow *wf = workflow_parse_json(json);
    ASSERT_EQ(wf->steps[0].type, STEP_CONDITION);
    ASSERT_STR_EQ(wf->steps[0].condition, "x == 'yes'");
    ASSERT_STR_EQ(wf->steps[0].then_step, "s2");
    ASSERT_STR_EQ(wf->steps[0].else_step, "s3");
    workflow_free(wf);
}

TEST(workflow_parse_json_parallel) {
    const char *json = "{\"name\":\"w\",\"steps\":[{\"name\":\"par\",\"type\":\"parallel\",\"steps\":[{\"name\":\"a\",\"type\":\"bash\",\"config\":{\"command\":\"echo a\"}},{\"name\":\"b\",\"type\":\"bash\",\"config\":{\"command\":\"echo b\"}}]}]}";
    Workflow *wf = workflow_parse_json(json);
    ASSERT_EQ(wf->steps[0].type, STEP_PARALLEL);
    ASSERT_EQ(wf->steps[0].parallel_count, 2);
    workflow_free(wf);
}

TEST(workflow_parse_json_loop) {
    const char *json = "{\"name\":\"w\",\"steps\":[{\"name\":\"lp\",\"loop_over\":\"items\",\"loop_body\":{\"name\":\"inner\",\"type\":\"bash\",\"config\":{\"command\":\"echo ${loop.item}\"}}}]}";
    Workflow *wf = workflow_parse_json(json);
    ASSERT_EQ(wf->steps[0].type, STEP_LOOP);
    ASSERT_STR_EQ(wf->steps[0].loop_over, "items");
    ASSERT_NOT_NULL(wf->steps[0].loop_body);
    workflow_free(wf);
}

TEST(workflow_parse_json_defaults) {
    const char *json = "{\"name\":\"w\",\"defaults\":{\"key\":\"val\"},\"steps\":[{\"name\":\"s1\",\"type\":\"prompt\"}]}";
    Workflow *wf = workflow_parse_json(json);
    ASSERT_NOT_NULL(wf->defaults);
    workflow_free(wf);
}

/* ========== Parsing YAML ========== */

TEST(workflow_parse_yaml_basic) {
    const char *yaml = "name: test-yaml\nsteps:\n  - name: s1\n    type: bash\n    config:\n      command: echo hi\n";
    const char *path = "/tmp/rig_test_wf.yaml";
    fs_write_file(path, yaml, strlen(yaml));
    Workflow *wf = workflow_parse_yaml(path);
    ASSERT_NOT_NULL(wf);
    ASSERT_STR_EQ(wf->name, "test-yaml");
    ASSERT_EQ(wf->step_count, 1);
    ASSERT_EQ(wf->steps[0].type, STEP_BASH);
    workflow_free(wf);
    unlink(path);
}

TEST(workflow_parse_yaml_null) {
    ASSERT_NULL(workflow_parse_yaml(NULL));
}

TEST(workflow_parse_yaml_missing) {
    ASSERT_NULL(workflow_parse_yaml("/tmp/rig_nonexistent.yaml"));
}

/* ========== Validation ========== */

TEST(workflow_validate_ok) {
    const char *json = "{\"name\":\"valid\",\"steps\":[{\"name\":\"s1\",\"type\":\"prompt\"}]}";
    Workflow *wf = workflow_parse_json(json);
    char *err = NULL;
    ASSERT_EQ(workflow_validate(wf, &err), 0);
    ASSERT_NULL(err);
    workflow_free(wf);
}

TEST(workflow_validate_null) {
    char *err = NULL;
    ASSERT_EQ(workflow_validate(NULL, &err), -1);
    free(err);
}

TEST(workflow_validate_no_name) {
    const char *json = "{\"steps\":[{\"name\":\"s1\",\"type\":\"prompt\"}]}";
    Workflow *wf = workflow_parse_json(json);
    char *err = NULL;
    ASSERT_EQ(workflow_validate(wf, &err), -1);
    ASSERT_NOT_NULL(err);
    free(err);
    workflow_free(wf);
}

TEST(workflow_validate_no_steps) {
    const char *json = "{\"name\":\"w\",\"steps\":[]}";
    Workflow *wf = workflow_parse_json(json);
    char *err = NULL;
    ASSERT_EQ(workflow_validate(wf, &err), -1);
    free(err);
    workflow_free(wf);
}

TEST(workflow_validate_duplicate_names) {
    const char *json = "{\"name\":\"w\",\"steps\":[{\"name\":\"dup\",\"type\":\"prompt\"},{\"name\":\"dup\",\"type\":\"prompt\"}]}";
    Workflow *wf = workflow_parse_json(json);
    char *err = NULL;
    ASSERT_EQ(workflow_validate(wf, &err), -1);
    ASSERT_TRUE(strstr(err, "duplicate") != NULL);
    free(err);
    workflow_free(wf);
}

/* ========== Execution ========== */

TEST(workflow_execute_bash_step) {
    const char *json = "{\"name\":\"w\",\"steps\":[{\"name\":\"run\",\"type\":\"bash\",\"config\":{\"command\":\"echo hello\"},\"save_as\":\"output\"}]}";
    Workflow *wf = workflow_parse_json(json);
    WorkflowContext *ctx = workflow_context_create(wf);
    int rc = workflow_execute(wf, ctx);
    ASSERT_EQ(rc, 0);
    cJSON *out = cJSON_GetObjectItem(ctx->variables, "output");
    ASSERT_NOT_NULL(out);
    ASSERT_STR_EQ(out->valuestring, "hello");
    workflow_context_free(ctx);
    workflow_free(wf);
}

TEST(workflow_execute_bash_fail) {
    const char *json = "{\"name\":\"w\",\"steps\":[{\"name\":\"fail\",\"type\":\"bash\",\"config\":{\"command\":\"exit 1\"}}]}";
    Workflow *wf = workflow_parse_json(json);
    WorkflowContext *ctx = workflow_context_create(wf);
    int rc = workflow_execute(wf, ctx);
    ASSERT_TRUE(rc != 0);
    workflow_context_free(ctx);
    workflow_free(wf);
}

TEST(workflow_execute_variable_interpolation) {
    const char *json = "{\"name\":\"w\",\"defaults\":{\"greeting\":\"world\"},\"steps\":[{\"name\":\"run\",\"type\":\"bash\",\"config\":{\"command\":\"echo ${greeting}\"},\"save_as\":\"result\"}]}";
    Workflow *wf = workflow_parse_json(json);
    WorkflowContext *ctx = workflow_context_create(wf);
    workflow_execute(wf, ctx);
    cJSON *r = cJSON_GetObjectItem(ctx->variables, "result");
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r->valuestring, "world");
    workflow_context_free(ctx);
    workflow_free(wf);
}

TEST(workflow_execute_loop) {
    const char *json = "{\"name\":\"w\",\"steps\":[{\"name\":\"lp\",\"loop_over\":\"items\",\"loop_body\":{\"name\":\"inner\",\"type\":\"bash\",\"config\":{\"command\":\"echo item\"}}}]}";
    Workflow *wf = workflow_parse_json(json);
    WorkflowContext *ctx = workflow_context_create(wf);
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToArray(arr, cJSON_CreateString("a"));
    cJSON_AddItemToArray(arr, cJSON_CreateString("b"));
    cJSON_AddItemToObject(ctx->variables, "items", arr);
    int rc = workflow_execute(wf, ctx);
    ASSERT_EQ(rc, 0);
    workflow_context_free(ctx);
    workflow_free(wf);
}

TEST(workflow_execute_parallel) {
    const char *json = "{\"name\":\"w\",\"steps\":[{\"name\":\"par\",\"type\":\"parallel\",\"steps\":[{\"name\":\"a\",\"type\":\"bash\",\"config\":{\"command\":\"echo a\"},\"save_as\":\"out_a\"},{\"name\":\"b\",\"type\":\"bash\",\"config\":{\"command\":\"echo b\"},\"save_as\":\"out_b\"}]}]}";
    Workflow *wf = workflow_parse_json(json);
    WorkflowContext *ctx = workflow_context_create(wf);
    int rc = workflow_execute(wf, ctx);
    ASSERT_EQ(rc, 0);
    workflow_context_free(ctx);
    workflow_free(wf);
}

TEST(workflow_abort) {
    const char *json = "{\"name\":\"w\",\"steps\":[{\"name\":\"s1\",\"type\":\"prompt\"}]}";
    Workflow *wf = workflow_parse_json(json);
    WorkflowContext *ctx = workflow_context_create(wf);
    workflow_abort(ctx);
    ASSERT_TRUE(ctx->aborted);
    workflow_context_free(ctx);
    workflow_free(wf);
}

/* ========== Expression Evaluator ========== */

TEST(expr_eval_true_var) {
    const char *json = "{\"name\":\"w\",\"steps\":[{\"name\":\"s\",\"type\":\"prompt\"}]}";
    Workflow *wf = workflow_parse_json(json);
    WorkflowContext *ctx = workflow_context_create(wf);
    cJSON_AddStringToObject(ctx->variables, "enabled", "yes");
    ASSERT_TRUE(expr_eval(ctx, "enabled"));
    workflow_context_free(ctx);
    workflow_free(wf);
}

TEST(expr_eval_false_var) {
    const char *json = "{\"name\":\"w\",\"steps\":[{\"name\":\"s\",\"type\":\"prompt\"}]}";
    Workflow *wf = workflow_parse_json(json);
    WorkflowContext *ctx = workflow_context_create(wf);
    cJSON_AddStringToObject(ctx->variables, "enabled", "false");
    ASSERT_FALSE(expr_eval(ctx, "enabled"));
    workflow_context_free(ctx);
    workflow_free(wf);
}

TEST(expr_eval_equality) {
    const char *json = "{\"name\":\"w\",\"steps\":[{\"name\":\"s\",\"type\":\"prompt\"}]}";
    Workflow *wf = workflow_parse_json(json);
    WorkflowContext *ctx = workflow_context_create(wf);
    cJSON_AddStringToObject(ctx->variables, "color", "red");
    ASSERT_TRUE(expr_eval(ctx, "color == 'red'"));
    ASSERT_FALSE(expr_eval(ctx, "color == 'blue'"));
    workflow_context_free(ctx);
    workflow_free(wf);
}

TEST(expr_eval_not_equal) {
    const char *json = "{\"name\":\"w\",\"steps\":[{\"name\":\"s\",\"type\":\"prompt\"}]}";
    Workflow *wf = workflow_parse_json(json);
    WorkflowContext *ctx = workflow_context_create(wf);
    cJSON_AddStringToObject(ctx->variables, "x", "a");
    ASSERT_TRUE(expr_eval(ctx, "x != 'b'"));
    workflow_context_free(ctx);
    workflow_free(wf);
}

TEST(expr_eval_and) {
    const char *json = "{\"name\":\"w\",\"steps\":[{\"name\":\"s\",\"type\":\"prompt\"}]}";
    Workflow *wf = workflow_parse_json(json);
    WorkflowContext *ctx = workflow_context_create(wf);
    cJSON_AddStringToObject(ctx->variables, "a", "yes");
    cJSON_AddStringToObject(ctx->variables, "b", "yes");
    ASSERT_TRUE(expr_eval(ctx, "a AND b"));
    workflow_context_free(ctx);
    workflow_free(wf);
}

TEST(expr_eval_or) {
    const char *json = "{\"name\":\"w\",\"steps\":[{\"name\":\"s\",\"type\":\"prompt\"}]}";
    Workflow *wf = workflow_parse_json(json);
    WorkflowContext *ctx = workflow_context_create(wf);
    cJSON_AddStringToObject(ctx->variables, "a", "false");
    cJSON_AddStringToObject(ctx->variables, "b", "yes");
    ASSERT_TRUE(expr_eval(ctx, "a OR b"));
    workflow_context_free(ctx);
    workflow_free(wf);
}

TEST(expr_eval_not) {
    const char *json = "{\"name\":\"w\",\"steps\":[{\"name\":\"s\",\"type\":\"prompt\"}]}";
    Workflow *wf = workflow_parse_json(json);
    WorkflowContext *ctx = workflow_context_create(wf);
    cJSON_AddStringToObject(ctx->variables, "x", "false");
    ASSERT_TRUE(expr_eval(ctx, "NOT x"));
    workflow_context_free(ctx);
    workflow_free(wf);
}

TEST(expr_eval_contains) {
    const char *json = "{\"name\":\"w\",\"steps\":[{\"name\":\"s\",\"type\":\"prompt\"}]}";
    Workflow *wf = workflow_parse_json(json);
    WorkflowContext *ctx = workflow_context_create(wf);
    cJSON_AddStringToObject(ctx->variables, "msg", "hello world");
    ASSERT_TRUE(expr_eval(ctx, "msg contains 'world'"));
    ASSERT_FALSE(expr_eval(ctx, "msg contains 'xyz'"));
    workflow_context_free(ctx);
    workflow_free(wf);
}

TEST(expr_eval_null) {
    ASSERT_FALSE(expr_eval(NULL, "x"));
    ASSERT_FALSE(expr_eval(NULL, NULL));
}

/* ========== Checkpoint ========== */

TEST(workflow_checkpoint_resume) {
    const char *json = "{\"name\":\"w\",\"steps\":[{\"name\":\"s1\",\"type\":\"bash\",\"config\":{\"command\":\"echo done\"},\"save_as\":\"r1\"},{\"name\":\"s2\",\"type\":\"bash\",\"config\":{\"command\":\"echo two\"},\"save_as\":\"r2\"}]}";
    Workflow *wf = workflow_parse_json(json);
    WorkflowContext *ctx = workflow_context_create(wf);

    /* Execute first step only */
    ctx->step_statuses[0] = STEP_STATUS_SUCCESS;
    cJSON_AddStringToObject(ctx->variables, "r1", "done");

    const char *cp_path = "/tmp/rig_test_checkpoint.json";
    ASSERT_EQ(workflow_checkpoint(ctx, cp_path), 0);
    ASSERT_TRUE(fs_exists(cp_path));

    workflow_context_free(ctx);

    /* Resume */
    WorkflowContext *ctx2 = workflow_context_create(wf);
    ASSERT_EQ(workflow_resume(cp_path, ctx2), 0);
    cJSON *r2 = cJSON_GetObjectItem(ctx2->variables, "r2");
    ASSERT_NOT_NULL(r2);

    workflow_context_free(ctx2);
    workflow_free(wf);
    unlink(cp_path);
}

TEST(workflow_resolve_var_basic) {
    const char *json = "{\"name\":\"w\",\"steps\":[{\"name\":\"s\",\"type\":\"prompt\"}]}";
    Workflow *wf = workflow_parse_json(json);
    WorkflowContext *ctx = workflow_context_create(wf);
    cJSON_AddStringToObject(ctx->variables, "greeting", "hello");
    char *v = workflow_resolve_var(ctx, "greeting");
    ASSERT_STR_EQ(v, "hello");
    free(v);
    workflow_context_free(ctx);
    workflow_free(wf);
}

TEST(workflow_resolve_var_missing) {
    const char *json = "{\"name\":\"w\",\"steps\":[{\"name\":\"s\",\"type\":\"prompt\"}]}";
    Workflow *wf = workflow_parse_json(json);
    WorkflowContext *ctx = workflow_context_create(wf);
    char *v = workflow_resolve_var(ctx, "missing");
    ASSERT_STR_EQ(v, "");
    free(v);
    workflow_context_free(ctx);
    workflow_free(wf);
}

/* ========== ADVERSARIAL: Malformed YAML ========== */

TEST(adv_yaml_invalid_syntax) {
    const char *yaml = "{{{{invalid yaml content[[[";
    const char *path = "/tmp/rig_adv_yaml_invalid.yaml";
    fs_write_file(path, yaml, strlen(yaml));
    Workflow *wf = workflow_parse_yaml(path);
    /* Should return NULL or a partial parse, must not crash */
    if (wf) workflow_free(wf);
    unlink(path);
}

TEST(adv_yaml_no_name_field) {
    const char *yaml = "description: no name here\nsteps:\n  - name: s1\n    type: bash\n";
    const char *path = "/tmp/rig_adv_yaml_noname.yaml";
    fs_write_file(path, yaml, strlen(yaml));
    Workflow *wf = workflow_parse_yaml(path);
    /* wf should parse but validation should fail */
    if (wf) {
        char *err = NULL;
        int rc = workflow_validate(wf, &err);
        ASSERT_EQ(rc, -1); /* missing name */
        free(err);
        workflow_free(wf);
    }
    unlink(path);
}

TEST(adv_yaml_empty_steps) {
    const char *yaml = "name: empty\nsteps: []\n";
    const char *path = "/tmp/rig_adv_yaml_emptysteps.yaml";
    fs_write_file(path, yaml, strlen(yaml));
    Workflow *wf = workflow_parse_yaml(path);
    if (wf) {
        char *err = NULL;
        int rc = workflow_validate(wf, &err);
        ASSERT_EQ(rc, -1); /* no steps */
        free(err);
        workflow_free(wf);
    }
    unlink(path);
}

TEST(adv_yaml_binary_data) {
    /* Write binary garbage as YAML */
    const char *path = "/tmp/rig_adv_yaml_binary.yaml";
    char bin[256];
    for (int i = 0; i < 256; i++) bin[i] = (char)i;
    fs_write_file(path, bin, 256);
    Workflow *wf = workflow_parse_yaml(path);
    /* Must not crash */
    if (wf) workflow_free(wf);
    unlink(path);
}

TEST(adv_yaml_extremely_large) {
    /* Write a 100KB YAML file */
    const char *path = "/tmp/rig_adv_yaml_large.yaml";
    size_t total = 100 * 1024;
    char *buf = malloc(total + 1);
    int pos = snprintf(buf, total, "name: large\nsteps:\n");
    for (int i = 0; i < 500 && pos < (int)total - 100; i++) {
        pos += snprintf(buf + pos, total - pos,
            "  - name: step_%d\n    type: prompt\n    prompt: \"%0*d\"\n",
            i, 100, 0);
    }
    buf[pos] = '\0';
    fs_write_file(path, buf, pos);
    Workflow *wf = workflow_parse_yaml(path);
    /* Must not crash */
    if (wf) workflow_free(wf);
    free(buf);
    unlink(path);
}

/* ========== ADVERSARIAL: Expression Evaluator ========== */

TEST(adv_expr_empty) {
    const char *json = "{\"name\":\"w\",\"steps\":[{\"name\":\"s\",\"type\":\"prompt\"}]}";
    Workflow *wf = workflow_parse_json(json);
    WorkflowContext *ctx = workflow_context_create(wf);
    /* Empty expression should not crash, should return false */
    ASSERT_FALSE(expr_eval(ctx, ""));
    workflow_context_free(ctx);
    workflow_free(wf);
}

TEST(adv_expr_just_operators) {
    const char *json = "{\"name\":\"w\",\"steps\":[{\"name\":\"s\",\"type\":\"prompt\"}]}";
    Workflow *wf = workflow_parse_json(json);
    WorkflowContext *ctx = workflow_context_create(wf);
    /* Just operators, no operands - must not crash */
    bool result = expr_eval(ctx, "AND AND");
    (void)result; /* just verify no crash */
    workflow_context_free(ctx);
    workflow_free(wf);
}

TEST(adv_expr_deeply_nested_parens) {
    const char *json = "{\"name\":\"w\",\"steps\":[{\"name\":\"s\",\"type\":\"prompt\"}]}";
    Workflow *wf = workflow_parse_json(json);
    WorkflowContext *ctx = workflow_context_create(wf);
    cJSON_AddStringToObject(ctx->variables, "x", "yes");
    /* Deeply nested parentheses */
    bool result = expr_eval(ctx, "((((((x))))))");
    ASSERT_TRUE(result);
    workflow_context_free(ctx);
    workflow_free(wf);
}

TEST(adv_expr_missing_operand) {
    const char *json = "{\"name\":\"w\",\"steps\":[{\"name\":\"s\",\"type\":\"prompt\"}]}";
    Workflow *wf = workflow_parse_json(json);
    WorkflowContext *ctx = workflow_context_create(wf);
    /* Missing left operand for == */
    bool result = expr_eval(ctx, "== 'foo'");
    /* Must not crash */
    (void)result;
    workflow_context_free(ctx);
    workflow_free(wf);
}

TEST(adv_expr_unterminated_string) {
    const char *json = "{\"name\":\"w\",\"steps\":[{\"name\":\"s\",\"type\":\"prompt\"}]}";
    Workflow *wf = workflow_parse_json(json);
    WorkflowContext *ctx = workflow_context_create(wf);
    cJSON_AddStringToObject(ctx->variables, "x", "val");
    /* Unterminated string literal */
    bool result = expr_eval(ctx, "x == 'unclosed");
    /* Must not crash */
    (void)result;
    workflow_context_free(ctx);
    workflow_free(wf);
}

TEST(adv_expr_very_long_expression) {
    const char *json = "{\"name\":\"w\",\"steps\":[{\"name\":\"s\",\"type\":\"prompt\"}]}";
    Workflow *wf = workflow_parse_json(json);
    WorkflowContext *ctx = workflow_context_create(wf);
    /* Build a 1KB expression: x OR x OR x OR ... */
    cJSON_AddStringToObject(ctx->variables, "x", "yes");
    char expr[1200];
    int pos = 0;
    for (int i = 0; i < 100 && pos < 1100; i++) {
        if (i > 0) pos += snprintf(expr + pos, sizeof(expr) - pos, " OR ");
        pos += snprintf(expr + pos, sizeof(expr) - pos, "x");
    }
    bool result = expr_eval(ctx, expr);
    ASSERT_TRUE(result);
    workflow_context_free(ctx);
    workflow_free(wf);
}

TEST(adv_expr_null_context) {
    ASSERT_FALSE(expr_eval(NULL, "x == 'y'"));
}

TEST(adv_expr_null_expr) {
    const char *json = "{\"name\":\"w\",\"steps\":[{\"name\":\"s\",\"type\":\"prompt\"}]}";
    Workflow *wf = workflow_parse_json(json);
    WorkflowContext *ctx = workflow_context_create(wf);
    ASSERT_FALSE(expr_eval(ctx, NULL));
    workflow_context_free(ctx);
    workflow_free(wf);
}

/* RESOURCE EXHAUSTION: workflow with 100 steps - execute completes */
TEST(workflow_100_steps) {
    Str json = str_from("{\"name\":\"big\",\"steps\":[");
    for (int i = 0; i < 100; i++) {
        if (i > 0) str_append(&json, ",");
        str_appendf(&json,
            "{\"name\":\"s%d\",\"type\":\"bash\",\"config\":{\"command\":\"echo %d\"}}",
            i, i);
    }
    str_append(&json, "]}");
    Workflow *wf = workflow_parse_json(json.data);
    ASSERT_NOT_NULL(wf);
    ASSERT_EQ(wf->step_count, 100);
    WorkflowContext *ctx = workflow_context_create(wf);
    int rc = workflow_execute(wf, ctx);
    ASSERT_EQ(rc, 0);
    workflow_context_free(ctx);
    workflow_free(wf);
    str_free(&json);
}

/* RESOURCE EXHAUSTION: variable with huge value (100KB string) */
TEST(workflow_resolve_var_huge) {
    const char *json = "{\"name\":\"w\",\"steps\":[{\"name\":\"s\",\"type\":\"prompt\"}]}";
    Workflow *wf = workflow_parse_json(json);
    WorkflowContext *ctx = workflow_context_create(wf);
    Str big = str_new(102400);
    for (int i = 0; i < 102400; i++) {
        str_append_char(&big, 'x');
    }
    cJSON_AddStringToObject(ctx->variables, "huge", big.data);
    char *v = workflow_resolve_var(ctx, "huge");
    ASSERT_NOT_NULL(v);
    ASSERT_EQ(strlen(v), 102400);
    free(v);
    workflow_context_free(ctx);
    workflow_free(wf);
    str_free(&big);
}

/* RESOURCE EXHAUSTION: deeply nested variable interpolation */
TEST(workflow_nested_var_interpolation) {
    const char *json = "{\"name\":\"w\",\"defaults\":{\"a\":\"${b}\",\"b\":\"${c}\",\"c\":\"final\"},\"steps\":[{\"name\":\"run\",\"type\":\"bash\",\"config\":{\"command\":\"echo ${a}\"},\"save_as\":\"result\"}]}";
    Workflow *wf = workflow_parse_json(json);
    ASSERT_NOT_NULL(wf);
    WorkflowContext *ctx = workflow_context_create(wf);
    int rc = workflow_execute(wf, ctx);
    /* Doesn't crash; result may or may not be fully resolved depending on impl */
    (void)rc; /* just verify no crash */
    workflow_context_free(ctx);
    workflow_free(wf);
}

int main(void) {
    TEST_SUITE("Workflow Parse JSON");
    RUN_TEST(workflow_parse_json_basic);
    RUN_TEST(workflow_parse_json_null);
    RUN_TEST(workflow_parse_json_invalid);
    RUN_TEST(workflow_parse_json_bash_step);
    RUN_TEST(workflow_parse_json_condition);
    RUN_TEST(workflow_parse_json_parallel);
    RUN_TEST(workflow_parse_json_loop);
    RUN_TEST(workflow_parse_json_defaults);

    TEST_SUITE("Workflow Parse YAML");
    RUN_TEST(workflow_parse_yaml_basic);
    RUN_TEST(workflow_parse_yaml_null);
    RUN_TEST(workflow_parse_yaml_missing);

    TEST_SUITE("Workflow Validation");
    RUN_TEST(workflow_validate_ok);
    RUN_TEST(workflow_validate_null);
    RUN_TEST(workflow_validate_no_name);
    RUN_TEST(workflow_validate_no_steps);
    RUN_TEST(workflow_validate_duplicate_names);

    TEST_SUITE("Workflow Execution");
    RUN_TEST(workflow_execute_bash_step);
    RUN_TEST(workflow_execute_bash_fail);
    RUN_TEST(workflow_execute_variable_interpolation);
    RUN_TEST(workflow_execute_loop);
    RUN_TEST(workflow_execute_parallel);
    RUN_TEST(workflow_abort);

    TEST_SUITE("Expression Evaluator");
    RUN_TEST(expr_eval_true_var);
    RUN_TEST(expr_eval_false_var);
    RUN_TEST(expr_eval_equality);
    RUN_TEST(expr_eval_not_equal);
    RUN_TEST(expr_eval_and);
    RUN_TEST(expr_eval_or);
    RUN_TEST(expr_eval_not);
    RUN_TEST(expr_eval_contains);
    RUN_TEST(expr_eval_null);

    TEST_SUITE("Checkpoint");
    RUN_TEST(workflow_checkpoint_resume);
    RUN_TEST(workflow_resolve_var_basic);
    RUN_TEST(workflow_resolve_var_missing);

    TEST_SUITE("ADVERSARIAL: Malformed YAML");
    RUN_TEST(adv_yaml_invalid_syntax);
    RUN_TEST(adv_yaml_no_name_field);
    RUN_TEST(adv_yaml_empty_steps);
    RUN_TEST(adv_yaml_binary_data);
    RUN_TEST(adv_yaml_extremely_large);

    TEST_SUITE("ADVERSARIAL: Expression Evaluator");
    RUN_TEST(adv_expr_empty);
    RUN_TEST(adv_expr_just_operators);
    RUN_TEST(adv_expr_deeply_nested_parens);
    RUN_TEST(adv_expr_missing_operand);
    RUN_TEST(adv_expr_unterminated_string);
    RUN_TEST(adv_expr_very_long_expression);
    RUN_TEST(adv_expr_null_context);
    RUN_TEST(adv_expr_null_expr);

    TEST_SUITE("RESOURCE EXHAUSTION: Workflow");
    RUN_TEST(workflow_100_steps);
    RUN_TEST(workflow_resolve_var_huge);
    RUN_TEST(workflow_nested_var_interpolation);

    TEST_REPORT();
}
