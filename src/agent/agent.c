#include "agent.h"
#include "ai/validation.h"
#include "util/log.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* ---- Message Queue ---- */

void queue_init(MessageQueue *q, QueueMode mode) {
    memset(q, 0, sizeof(*q));
    q->mode = mode;
}

void queue_enqueue(MessageQueue *q, Message *msg) {
    if (q->count >= q->capacity) {
        int new_cap = q->capacity ? q->capacity * 2 : 8;
        Message **new_items = realloc(q->items, (size_t)new_cap * sizeof(Message *));
        if (!new_items) return;
        q->items = new_items;
        q->capacity = new_cap;
    }
    q->items[q->count++] = msg;
}

int queue_drain(MessageQueue *q, Message ***out, int *count) {
    if (q->count == 0) { *out = NULL; *count = 0; return 0; }

    if (q->mode == QUEUE_ALL) {
        *out = q->items;
        *count = q->count;
        q->items = NULL;
        q->count = 0;
        q->capacity = 0;
    } else {
        *out = malloc(sizeof(Message *));
        (*out)[0] = q->items[0];
        *count = 1;
        memmove(q->items, q->items + 1, (size_t)(q->count - 1) * sizeof(Message *));
        q->count--;
    }
    return 0;
}

bool queue_has_items(const MessageQueue *q) {
    return q->count > 0;
}

void queue_clear(MessageQueue *q) {
    for (int i = 0; i < q->count; i++) {
        message_free(q->items[i]);
    }
    q->count = 0;
}

void queue_free(MessageQueue *q) {
    queue_clear(q);
    free(q->items);
    q->items = NULL;
    q->capacity = 0;
}

/* ---- Agent State ---- */

AgentState *agent_state_create(void) {
    AgentState *s = calloc(1, sizeof(AgentState));
    if (!s) return NULL;
    queue_init(&s->steering_queue, QUEUE_ONE_AT_A_TIME);
    queue_init(&s->follow_up_queue, QUEUE_ONE_AT_A_TIME);
    return s;
}

void agent_state_add_message(AgentState *state, Message *msg) {
    if (state->message_count >= state->message_capacity) {
        int new_cap = state->message_capacity ? state->message_capacity * 2 : 16;
        Message **new_msgs = realloc(state->messages, (size_t)new_cap * sizeof(Message *));
        if (!new_msgs) return;
        state->messages = new_msgs;
        state->message_capacity = new_cap;
    }
    state->messages[state->message_count++] = msg;
}

void agent_state_reset(AgentState *state) {
    for (int i = 0; i < state->message_count; i++) {
        message_free(state->messages[i]);
    }
    state->message_count = 0;
    state->is_streaming = false;
    state->streaming_message = NULL;
    state->abort_requested = false;
    queue_clear(&state->steering_queue);
    queue_clear(&state->follow_up_queue);
}

void agent_state_free(AgentState *state) {
    if (!state) return;
    agent_state_reset(state);
    free(state->messages);
    free(state->system_prompt);
    queue_free(&state->steering_queue);
    queue_free(&state->follow_up_queue);
    free(state);
}

void agent_abort(AgentState *state) {
    if (state) state->abort_requested = true;
}

/* ---- Tool Execution ---- */

typedef struct {
    Tool *tool;
    ContentBlock *tool_call;
    cJSON *validated_args;
    ContentBlock *result_content;
    int result_count;
    cJSON *result_details;
    bool terminate;
    bool is_error;
    char *error_reason;
} ToolExecJob;

static void prepare_tool_call(AgentState *state, ToolExecJob *job, ContentBlock *tc,
                               AgentLoopConfig *config, Message *assistant_msg) {
    job->tool_call = tc;
    job->tool = NULL;

    for (int i = 0; i < state->tool_count; i++) {
        if (strcmp(state->tools[i].name, tc->tool_call.name) == 0) {
            job->tool = &state->tools[i];
            break;
        }
    }

    if (!job->tool) {
        job->is_error = true;
        job->error_reason = strdup("tool not found");
        job->result_content = malloc(sizeof(ContentBlock));
        job->result_content[0] = content_text("Tool not found", NULL);
        job->result_count = 1;
        return;
    }

    cJSON *args = tc->tool_call.arguments ? cJSON_Duplicate(tc->tool_call.arguments, 1) : cJSON_CreateObject();

    if (job->tool->parameters) {
        coerce_tool_arguments(job->tool->parameters, args);
    }

    ValidationResult vr = validate_tool_arguments(job->tool, args);
    if (!vr.valid) {
        job->is_error = true;
        char msg[512];
        snprintf(msg, sizeof(msg), "Validation error at %s: %s", vr.path ? vr.path : "$", vr.error ? vr.error : "unknown");
        job->error_reason = strdup(msg);
        job->result_content = malloc(sizeof(ContentBlock));
        job->result_content[0] = content_text(msg, NULL);
        job->result_count = 1;
        validation_result_free(&vr);
        cJSON_Delete(args);
        return;
    }
    validation_result_free(&vr);

    if (config->before_tool_call) {
        BeforeToolCallContext ctx = { .assistant_message = assistant_msg, .tool_call = tc, .args = args };
        BeforeToolCallResult br = {0};
        config->before_tool_call(&ctx, &br);
        if (br.block) {
            job->is_error = true;
            job->error_reason = br.reason ? br.reason : strdup("blocked by hook");
            job->result_content = malloc(sizeof(ContentBlock));
            job->result_content[0] = content_text(job->error_reason, NULL);
            job->result_count = 1;
            cJSON_Delete(args);
            return;
        }
    }

    job->validated_args = args;
}

static void execute_tool_call(ToolExecJob *job, volatile bool *abort) {
    if (job->is_error || !job->tool) return;

    char *args_str = job->validated_args ? cJSON_PrintUnformatted(job->validated_args) : NULL;
    LOG_INFO("Tool exec: %s args=%s", job->tool->name, args_str ? args_str : "{}");
    free(args_str);

    int rc = job->tool->execute(
        job->tool_call->tool_call.id,
        job->validated_args,
        (void *)abort,
        NULL, NULL,
        &job->result_content,
        &job->result_count,
        &job->result_details,
        &job->terminate
    );

    if (rc != 0) {
        job->is_error = true;
        if (!job->result_content) {
            job->result_content = malloc(sizeof(ContentBlock));
            job->result_content[0] = content_text("Tool execution failed", NULL);
            job->result_count = 1;
        }
    }
}

static void finalize_tool_call(ToolExecJob *job, AgentLoopConfig *config, Message *assistant_msg) {
    if (config->after_tool_call && job->tool) {
        AfterToolCallContext ctx = {
            .assistant_message = assistant_msg,
            .tool_call = job->tool_call,
            .args = job->validated_args,
            .result_content = job->result_content,
            .result_count = job->result_count,
            .result_details = job->result_details,
            .is_error = job->is_error,
        };
        AfterToolCallResult ar = {0};
        config->after_tool_call(&ctx, &ar);
        if (ar.has_overrides) {
            if (ar.content) {
                free(job->result_content);
                job->result_content = ar.content;
                job->result_count = ar.content_count;
            }
            if (ar.details) {
                cJSON_Delete(job->result_details);
                job->result_details = ar.details;
            }
            job->is_error = ar.is_error;
            job->terminate = ar.terminate;
        }
    }
    cJSON_Delete(job->validated_args);
    job->validated_args = NULL;
}

static void job_free(ToolExecJob *job) {
    free(job->error_reason);
}

typedef struct {
    ToolExecJob *job;
    volatile bool *abort;
} ThreadArg;

static void *tool_thread_fn(void *arg) {
    ThreadArg *ta = arg;
    execute_tool_call(ta->job, ta->abort);
    return NULL;
}

static int execute_tool_calls(AgentState *state, Message *assistant_msg,
                               AgentLoopConfig *config, AgentEventCallback cb, void *userdata,
                               Message ***out_results, int *out_count, bool *should_continue) {
    int tc_count = 0;
    for (int i = 0; i < assistant_msg->content_count; i++) {
        if (assistant_msg->content[i].type == CONTENT_TOOL_CALL) tc_count++;
    }
    if (tc_count == 0) { *out_count = 0; *should_continue = false; return 0; }

    ToolExecJob *jobs = calloc((size_t)tc_count, sizeof(ToolExecJob));
    int j = 0;
    for (int i = 0; i < assistant_msg->content_count; i++) {
        if (assistant_msg->content[i].type == CONTENT_TOOL_CALL) {
            prepare_tool_call(state, &jobs[j], &assistant_msg->content[i], config, assistant_msg);

            AgentEvent ev = { .type = AGENT_EVENT_TOOL_EXEC_START,
                              .tool_call_id = assistant_msg->content[i].tool_call.id,
                              .tool_name = assistant_msg->content[i].tool_call.name,
                              .args = assistant_msg->content[i].tool_call.arguments };
            cb(&ev, userdata);
            j++;
        }
    }

    bool any_sequential = false;
    for (int i = 0; i < tc_count; i++) {
        if (jobs[i].tool && jobs[i].tool->execution_mode == EXEC_SEQUENTIAL) {
            any_sequential = true;
            break;
        }
    }

    bool use_parallel = (config->tool_execution == TOOL_EXEC_PARALLEL) && !any_sequential && tc_count > 1;

    if (use_parallel) {
        pthread_t *threads = malloc((size_t)tc_count * sizeof(pthread_t));
        ThreadArg *targs = malloc((size_t)tc_count * sizeof(ThreadArg));
        bool *thread_created = calloc((size_t)tc_count, sizeof(bool));

        if (!threads || !targs || !thread_created) {
            free(threads); free(targs); free(thread_created);
            for (int i = 0; i < tc_count; i++) {
                if (!jobs[i].is_error)
                    execute_tool_call(&jobs[i], &state->abort_requested);
            }
            goto finalize;
        }

        for (int i = 0; i < tc_count; i++) {
            if (jobs[i].is_error) continue;
            targs[i] = (ThreadArg){ .job = &jobs[i], .abort = &state->abort_requested };
            if (pthread_create(&threads[i], NULL, tool_thread_fn, &targs[i]) == 0) {
                thread_created[i] = true;
            } else {
                execute_tool_call(&jobs[i], &state->abort_requested);
            }
        }

        for (int i = 0; i < tc_count; i++) {
            if (thread_created[i]) {
                pthread_join(threads[i], NULL);
            }
        }

        free(threads);
        free(targs);
        free(thread_created);
    } else {
        for (int i = 0; i < tc_count; i++) {
            if (!jobs[i].is_error) {
                execute_tool_call(&jobs[i], &state->abort_requested);
            }
        }
    }

    finalize:
    for (int i = 0; i < tc_count; i++) {
        finalize_tool_call(&jobs[i], config, assistant_msg);
    }

    Message **results = malloc((size_t)tc_count * sizeof(Message *));
    bool all_terminate = tc_count > 0;

    for (int i = 0; i < tc_count; i++) {
        AgentEvent ev = { .type = AGENT_EVENT_TOOL_EXEC_END,
                          .tool_call_id = jobs[i].tool_call->tool_call.id,
                          .tool_name = jobs[i].tool_call->tool_call.name,
                          .is_error = jobs[i].is_error };
        cb(&ev, userdata);

        results[i] = message_create_tool_result(
            jobs[i].tool_call->tool_call.id,
            jobs[i].tool_call->tool_call.name,
            jobs[i].result_content, jobs[i].result_count,
            jobs[i].result_details, jobs[i].is_error
        );

        if (!jobs[i].terminate) all_terminate = false;

        AgentEvent msg_ev = { .type = AGENT_EVENT_MESSAGE_START, .message = results[i] };
        cb(&msg_ev, userdata);
        msg_ev.type = AGENT_EVENT_MESSAGE_END;
        cb(&msg_ev, userdata);

        job_free(&jobs[i]);
    }

    free(jobs);

    *out_results = results;
    *out_count = tc_count;
    *should_continue = !all_terminate;
    return 0;
}

/* ---- Stream Callback Bridge ---- */

typedef struct {
    AgentState *state;
    AgentEventCallback cb;
    void *userdata;
    Message *final_message;
} StreamBridge;

static void stream_bridge_cb(StreamEvent *event, void *userdata) {
    StreamBridge *bridge = userdata;
    AgentState *state = bridge->state;

    if (event->type == EVENT_START) {
        state->streaming_message = event->partial;
        AgentEvent ev = { .type = AGENT_EVENT_MESSAGE_START, .message = event->partial };
        bridge->cb(&ev, bridge->userdata);
    }
    else if (event->type >= EVENT_TEXT_START && event->type <= EVENT_TOOLCALL_END) {
        state->streaming_message = event->partial;
        AgentEvent ev = { .type = AGENT_EVENT_MESSAGE_UPDATE, .message = event->partial, .stream_event = event };
        bridge->cb(&ev, bridge->userdata);
    }
    else if (event->type == EVENT_DONE) {
        state->streaming_message = NULL;
        bridge->final_message = event->message;
        AgentEvent ev = { .type = AGENT_EVENT_MESSAGE_END, .message = event->message };
        bridge->cb(&ev, bridge->userdata);
    }
    else if (event->type == EVENT_ERROR) {
        state->streaming_message = NULL;
        bridge->final_message = event->message;
        AgentEvent ev = { .type = AGENT_EVENT_MESSAGE_END, .message = event->message };
        bridge->cb(&ev, bridge->userdata);
    }
}

/* ---- Core Loop ---- */

static int default_convert_to_llm(Message **msgs, int count, Message ***out, int *out_count) {
    *out = msgs;
    *out_count = count;
    return 0;
}

static int run_loop(AgentState *state, AgentLoopConfig *config,
                    AgentEventCallback cb, void *userdata) {
    bool has_more_tool_calls = true;
    Message **pending = NULL;
    int pending_count = 0;

    while (!state->abort_requested) {
        if (config->get_steering_messages) {
            config->get_steering_messages(&pending, &pending_count);
        }

        while (has_more_tool_calls || pending_count > 0) {
            if (state->abort_requested) break;

            LOG_INFO("Agent loop: new turn, msg_count=%d, tool_calls=%d",
                     state->message_count, has_more_tool_calls);

            AgentEvent turn_start = { .type = AGENT_EVENT_TURN_START };
            cb(&turn_start, userdata);

            for (int i = 0; i < pending_count; i++) {
                agent_state_add_message(state, pending[i]);
            }
            if (pending_count > 0) { free(pending); pending = NULL; pending_count = 0; }

            Message **llm_msgs = state->messages;
            int llm_count = state->message_count;

            int (*convert)(Message **, int, Message ***, int *) =
                config->convert_to_llm ? config->convert_to_llm : default_convert_to_llm;
            Message **converted = NULL;
            int converted_count = 0;
            convert(llm_msgs, llm_count, &converted, &converted_count);

            /* Shallow copy — flat_msgs aliases content pointers in state->messages.
             * ai_stream_simple must not retain pointers after returning. */
            Message *flat_msgs = NULL;
            if (converted_count > 0) {
                flat_msgs = malloc((size_t)converted_count * sizeof(Message));
                for (int i = 0; i < converted_count; i++) {
                    flat_msgs[i] = *converted[i];
                }
            }

            SimpleStreamOptions opts = {
                .base = {
                    .temperature = config->temperature,
                    .max_tokens = config->max_tokens,
                    .api_key = config->api_key,
                    .timeout_ms = config->timeout_ms,
                    .abort_flag = &state->abort_requested,
                },
                .reasoning = config->reasoning,
                .thinking_budgets = config->thinking_budgets,
            };

            char *allocated_key = NULL;
            if (config->get_api_key && config->model) {
                config->get_api_key(config->model->provider, &allocated_key);
                if (allocated_key) opts.base.api_key = allocated_key;
            }

            state->is_streaming = true;
            StreamBridge bridge = { .state = state, .cb = cb, .userdata = userdata, .final_message = NULL };

            ai_stream_simple(config->model, flat_msgs, converted_count,
                            state->system_prompt, state->tools, state->tool_count,
                            &opts, stream_bridge_cb, &bridge);

            state->is_streaming = false;
            free(flat_msgs);
            free(allocated_key);

            /* convert_to_llm may return the original array (aliased) or a new one.
             * If new, we free the array. Individual Message* are owned by the
             * converter and must be freed there if allocated. */
            if (converted != llm_msgs) {
                free(converted);
            }

            Message *assistant = bridge.final_message;
            if (!assistant) {
                LOG_WARN("Agent: no message from API, emitting error");
                Message *err_msg = calloc(1, sizeof(Message));
                err_msg->role = ROLE_ASSISTANT;
                message_add_content(err_msg, content_text("(no response from API — may be a temporary error, try again)", NULL));
                AgentEvent ev = { .type = AGENT_EVENT_MESSAGE_START, .message = err_msg };
                cb(&ev, userdata);
                ev.type = AGENT_EVENT_MESSAGE_END;
                cb(&ev, userdata);
                assistant = err_msg;
            }
            if (assistant) {
                LOG_INFO("Agent: got assistant message, content_count=%d", assistant->content_count);
                for (int ci = 0; ci < assistant->content_count; ci++) {
                    const char *ct = "?";
                    switch (assistant->content[ci].type) {
                        case CONTENT_TEXT: ct = "text"; break;
                        case CONTENT_TOOL_CALL: ct = "tool_call"; break;
                        case CONTENT_THINKING: ct = "thinking"; break;
                        case CONTENT_IMAGE: ct = "image"; break;
                    }
                    LOG_DEBUG("Agent:   content[%d] type=%s", ci, ct);
                    if (assistant->content[ci].type == CONTENT_TOOL_CALL) {
                        LOG_DEBUG("Agent:     tool=%s id=%s",
                                  assistant->content[ci].tool_call.name ? assistant->content[ci].tool_call.name : "?",
                                  assistant->content[ci].tool_call.id ? assistant->content[ci].tool_call.id : "?");
                    }
                }
                agent_state_add_message(state, assistant);
            } else {
                LOG_WARN("Agent: no assistant message from API");
            }

            has_more_tool_calls = false;
            if (assistant && !state->abort_requested) {
                Message **tool_results = NULL;
                int result_count = 0;
                bool should_continue = false;

                execute_tool_calls(state, assistant, config, cb, userdata,
                                  &tool_results, &result_count, &should_continue);
                LOG_INFO("Agent: tool execution done, result_count=%d, continue=%d", result_count, should_continue);

                for (int i = 0; i < result_count; i++) {
                    agent_state_add_message(state, tool_results[i]);
                }
                free(tool_results);

                has_more_tool_calls = should_continue;
            }

            AgentEvent turn_end = { .type = AGENT_EVENT_TURN_END, .message = assistant };
            cb(&turn_end, userdata);

            if (config->get_steering_messages) {
                config->get_steering_messages(&pending, &pending_count);
            }
        }

        Message **follow_ups = NULL;
        int follow_count = 0;
        if (config->get_follow_up_messages) {
            config->get_follow_up_messages(&follow_ups, &follow_count);
        }
        if (follow_count > 0) {
            pending = follow_ups;
            pending_count = follow_count;
            has_more_tool_calls = true;
            continue;
        }
        break;
    }

    return 0;
}

int agent_prompt(AgentState *state, Message **prompts, int prompt_count,
                 AgentLoopConfig *config, AgentEventCallback cb, void *userdata) {
    if (!state || !config || !cb) return -1;

    state->abort_requested = false;
    state->is_streaming = false;

    for (int i = 0; i < prompt_count; i++) {
        agent_state_add_message(state, prompts[i]);
        AgentEvent ev = { .type = AGENT_EVENT_MESSAGE_START, .message = prompts[i] };
        cb(&ev, userdata);
        ev.type = AGENT_EVENT_MESSAGE_END;
        cb(&ev, userdata);
    }

    AgentEvent start = { .type = AGENT_EVENT_AGENT_START };
    cb(&start, userdata);

    int result = run_loop(state, config, cb, userdata);

    AgentEvent end = { .type = AGENT_EVENT_AGENT_END };
    cb(&end, userdata);

    return result;
}

int agent_continue(AgentState *state, AgentLoopConfig *config,
                   AgentEventCallback cb, void *userdata) {
    if (!state || !config || !cb) return -1;
    if (state->message_count == 0) return -1;

    state->abort_requested = false;

    AgentEvent start = { .type = AGENT_EVENT_AGENT_START };
    cb(&start, userdata);

    int result = run_loop(state, config, cb, userdata);

    AgentEvent end = { .type = AGENT_EVENT_AGENT_END };
    cb(&end, userdata);

    return result;
}
