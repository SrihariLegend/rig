#include "print.h"
#include "harness/system_prompt.h"
#include "util/str.h"
#include "util/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Str text_output;
    bool json_mode;
} PrintModeCtx;

static void print_event_handler(AgentEvent *event, void *userdata) {
    PrintModeCtx *ctx = userdata;

    if (ctx->json_mode) {
        const char *type_names[] = {
            "agent_start", "agent_end", "turn_start", "turn_end",
            "message_start", "message_update", "message_end",
            "tool_exec_start", "tool_exec_update", "tool_exec_end",
        };
        if (event->type <= AGENT_EVENT_TOOL_EXEC_END) {
            fprintf(stdout, "{\"type\":\"%s\"", type_names[event->type]);
            if (event->tool_call_id) {
                fprintf(stdout, ",\"tool_call_id\":\"%s\"", event->tool_call_id);
            }
            if (event->tool_name) {
                fprintf(stdout, ",\"tool_name\":\"%s\"", event->tool_name);
            }
            fprintf(stdout, "}\n");
            fflush(stdout);
        }
    }

    if (event->type == AGENT_EVENT_MESSAGE_UPDATE && event->stream_event) {
        StreamEvent *se = event->stream_event;
        if (se->type == EVENT_TEXT_DELTA && se->delta) {
            if (!ctx->json_mode) {
                fprintf(stdout, "%s", se->delta);
                fflush(stdout);
            }
        }
    }

    if (event->type == AGENT_EVENT_MESSAGE_END && event->message) {
        if (event->message->role == ROLE_ASSISTANT) {
            for (int i = 0; i < event->message->content_count; i++) {
                if (event->message->content[i].type == CONTENT_TEXT) {
                    str_append(&ctx->text_output, event->message->content[i].text.text);
                }
            }
        }
    }
}

int print_mode_run(PrintModeOptions *opts) {
    if (!opts || !opts->model || !opts->prompt) return -1;

    char *system_prompt = system_prompt_build(opts->tools, opts->tool_count, opts->cwd);

    AgentState *state = agent_state_create();
    state->model = opts->model;
    state->system_prompt = system_prompt;
    state->tools = opts->tools;
    state->tool_count = opts->tool_count;
    state->thinking_level = opts->thinking;

    PrintModeCtx ctx = { .text_output = str_new(4096), .json_mode = opts->json_mode };

    AgentLoopConfig config = {
        .model = opts->model,
        .tool_execution = TOOL_EXEC_PARALLEL,
        .temperature = -1,
        .max_tokens = opts->model->max_tokens,
        .reasoning = opts->thinking,
        .api_key = (char *)opts->api_key,
        .timeout_ms = 120000,
    };

    Message *prompt_msg = message_create_user(opts->prompt);
    int rc = agent_prompt(state, &prompt_msg, 1, &config, print_event_handler, &ctx);

    if (!opts->json_mode) {
        fprintf(stdout, "\n");
    }

    str_free(&ctx.text_output);
    agent_state_free(state);

    return rc;
}
