#include "interactive.h"
#include "pi.h"
#include "harness/tools/tools.h"
#include "harness/model_registry.h"
#include "ai/providers/anthropic.h"
#include "ai/providers/openai.h"
#include "ai/providers/google.h"
#include "ai/providers/bedrock.h"
#include "ai/providers/mistral.h"
#include "util/log.h"
#include "tui/lantern.h"
#include "tui/linestore.h"
#include "tui/lantern_render.h"
#include "tui/terminal.h"
#include "tui/keys.h"
#include "tui/ansi.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <poll.h>
#include <signal.h>

/* ---- State ---- */

typedef enum {
    ISTATE_IDLE,
    ISTATE_STREAMING,
} InteractivePhase;

typedef struct {
    PiInstance *pi;
    Session *session;
    AgentState *agent;
    AgentLoopConfig agent_config;

    Lantern *lantern;
    LineStore *store;
    LanternRenderer *renderer;

    /* Input buffer */
    char *input_buf;
    int input_len;
    int input_cap;
    int input_cursor;

    InteractivePhase phase;
    uint32_t msg_counter;
    bool thinking_active;
    char *last_prompt;
    int last_prompt_store_idx;

    const Model *model;
    int total_tokens;

    pthread_mutex_t mutex;
    volatile bool needs_render;
    volatile bool running;

    Tool tools[6];
    int tool_count;
    char cwd[4096];
    char *api_key;
} InteractiveState;

/* ---- Forward Decls ---- */

static void on_agent_event(AgentEvent *event, void *userdata);
static void handle_submit(InteractiveState *state);
static void *agent_thread_fn(void *arg);
static void input_insert(InteractiveState *state, const char *text, int len);

/* ---- Input Handling ---- */

static void input_init(InteractiveState *state) {
    state->input_cap = 256;
    state->input_buf = calloc(state->input_cap, 1);
    state->input_len = 0;
    state->input_cursor = 0;
}

static void input_insert(InteractiveState *state, const char *text, int len) {
    while (state->input_len + len >= state->input_cap) {
        int new_cap = state->input_cap * 2;
        char *new_buf = realloc(state->input_buf, new_cap);
        if (!new_buf) return;
        state->input_buf = new_buf;
        state->input_cap = new_cap;
    }
    memmove(state->input_buf + state->input_cursor + len,
            state->input_buf + state->input_cursor,
            state->input_len - state->input_cursor);
    memcpy(state->input_buf + state->input_cursor, text, len);
    state->input_cursor += len;
    state->input_len += len;
    state->input_buf[state->input_len] = '\0';
}

static void input_backspace(InteractiveState *state) {
    if (state->input_cursor <= 0) return;
    memmove(state->input_buf + state->input_cursor - 1,
            state->input_buf + state->input_cursor,
            state->input_len - state->input_cursor);
    state->input_cursor--;
    state->input_len--;
    state->input_buf[state->input_len] = '\0';
}

static void input_clear(InteractiveState *state) {
    state->input_len = 0;
    state->input_cursor = 0;
    state->input_buf[0] = '\0';
}

static void sync_input_to_renderer(InteractiveState *state) {
    lantern_renderer_set_input(state->renderer, state->input_buf, state->input_cursor);
    state->needs_render = true;
}

/* ---- Agent Events ---- */

static void on_agent_event(AgentEvent *event, void *userdata) {
    InteractiveState *state = userdata;
    if (!state) return;

    switch (event->type) {
    case AGENT_EVENT_MESSAGE_START:
        if (event->message && event->message->role == ROLE_ASSISTANT) {
            pthread_mutex_lock(&state->mutex);
            state->msg_counter++;
            linestore_begin_message(state->store, state->msg_counter);
            state->needs_render = true;
            pthread_mutex_unlock(&state->mutex);
        }
        break;

    case AGENT_EVENT_MESSAGE_UPDATE:
        if (!event->stream_event) break;
        switch (event->stream_event->type) {
        case EVENT_TEXT_DELTA:
            if (event->stream_event->delta) {
                pthread_mutex_lock(&state->mutex);
                linestore_append_assistant_text(state->store, event->stream_event->delta);
                if (state->renderer->auto_scroll) {
                    int total = linestore_screen_row_count(state->store);
                    int vp = state->renderer->term_height - 1;
                    state->renderer->scroll_offset = total - vp;
                    if (state->renderer->scroll_offset < 0)
                        state->renderer->scroll_offset = 0;
                }
                state->needs_render = true;
                pthread_mutex_unlock(&state->mutex);
            }
            break;
        case EVENT_THINKING_START:
            state->thinking_active = true;
            break;
        case EVENT_THINKING_END:
            state->thinking_active = false;
            break;
        case EVENT_TOOLCALL_START:
            break;
        case EVENT_TOOLCALL_END:
            break;
        case EVENT_DONE:
            state->phase = ISTATE_IDLE;
            state->thinking_active = false;
            if (event->stream_event->message) {
                state->total_tokens += event->stream_event->message->usage.total_tokens;
            }
            if (!event->stream_event->message) {
                pthread_mutex_lock(&state->mutex);
                linestore_add_error(state->store, "no response from API");
                pthread_mutex_unlock(&state->mutex);
            }
            pthread_mutex_lock(&state->mutex);
            lantern_renderer_set_breathing(state->renderer, false, NULL);
            state->needs_render = true;
            pthread_mutex_unlock(&state->mutex);
            break;
        case EVENT_ERROR:
            state->phase = ISTATE_IDLE;
            if (event->stream_event->error_message) {
                pthread_mutex_lock(&state->mutex);
                linestore_add_error(state->store, event->stream_event->error_message);
                state->needs_render = true;
                pthread_mutex_unlock(&state->mutex);
            }
            break;
        default:
            break;
        }
        break;

    case AGENT_EVENT_MESSAGE_END:
        if (event->message && event->message->role == ROLE_ASSISTANT) {
            pthread_mutex_lock(&state->mutex);
            linestore_flush_stream(state->store);
            state->renderer->auto_scroll = true;
            state->needs_render = true;
            pthread_mutex_unlock(&state->mutex);
            if (state->session) {
                cJSON *msg_data = cJSON_CreateObject();
                cJSON_AddStringToObject(msg_data, "role", "assistant");
                Str full_text = str_new(256);
                for (int i = 0; i < event->message->content_count; i++) {
                    if (event->message->content[i].type == CONTENT_TEXT) {
                        str_append(&full_text, event->message->content[i].text.text);
                    }
                }
                cJSON_AddStringToObject(msg_data, "text", full_text.data);
                session_append(state->session, ENTRY_MESSAGE, msg_data);
                session_flush(state->session);
                cJSON_Delete(msg_data);
                str_free(&full_text);
            }
            pthread_mutex_lock(&state->mutex);
            linestore_add_blank(state->store);
            state->needs_render = true;
            pthread_mutex_unlock(&state->mutex);
        } else if (event->message && event->message->role == ROLE_TOOL_RESULT) {
            pthread_mutex_lock(&state->mutex);
            for (int i = 0; i < event->message->content_count; i++) {
                if (event->message->content[i].type == CONTENT_TEXT &&
                    event->message->content[i].text.text) {
                    linestore_add_tool_output(state->store, event->message->content[i].text.text);
                }
            }
            state->needs_render = true;
            pthread_mutex_unlock(&state->mutex);
        }
        break;

    case AGENT_EVENT_TOOL_EXEC_START:
        if (event->tool_name) {
            pthread_mutex_lock(&state->mutex);
            char *args_str = event->args ? cJSON_PrintUnformatted(event->args) : NULL;
            if (args_str && strlen(args_str) > 200) {
                args_str[197] = '.';
                args_str[198] = '.';
                args_str[199] = '.';
                args_str[200] = '\0';
            }
            linestore_add_tool_start(state->store, event->tool_name, args_str);
            free(args_str);
            lantern_renderer_set_breathing(state->renderer, true, event->tool_name);
            state->needs_render = true;
            pthread_mutex_unlock(&state->mutex);
        }
        break;

    case AGENT_EVENT_TOOL_EXEC_END:
        if (event->tool_name) {
            pthread_mutex_lock(&state->mutex);
            linestore_add_tool_done(state->store, event->tool_name);
            lantern_renderer_set_breathing(state->renderer, false, NULL);
            state->needs_render = true;
            pthread_mutex_unlock(&state->mutex);
        }
        break;

    default:
        break;
    }
}

/* ---- Submit ---- */

static void handle_submit(InteractiveState *state) {
    if (state->input_len == 0) return;
    if (!state->model || !state->api_key) {
        pthread_mutex_lock(&state->mutex);
        linestore_add_error(state->store, "no API key configured");
        state->needs_render = true;
        pthread_mutex_unlock(&state->mutex);
        input_clear(state);
        sync_input_to_renderer(state);
        return;
    }

    char *prompt_text = strdup(state->input_buf);
    free(state->last_prompt);
    state->last_prompt = strdup(state->input_buf);
    input_clear(state);
    sync_input_to_renderer(state);

    pthread_mutex_lock(&state->mutex);
    state->msg_counter++;
    state->last_prompt_store_idx = state->store->count;
    linestore_add_user_text(state->store, prompt_text);
    linestore_add_blank(state->store);
    state->needs_render = true;
    pthread_mutex_unlock(&state->mutex);

    if (state->session) {
        cJSON *msg_data = cJSON_CreateObject();
        cJSON_AddStringToObject(msg_data, "role", "user");
        cJSON_AddStringToObject(msg_data, "text", prompt_text);
        session_append(state->session, ENTRY_MESSAGE, msg_data);
        session_flush(state->session);
        cJSON_Delete(msg_data);
    }

    state->phase = ISTATE_STREAMING;
    state->renderer->is_streaming = true;

    typedef struct { InteractiveState *state; char *prompt; } AgentThreadArg;
    AgentThreadArg *arg = malloc(sizeof(AgentThreadArg));
    arg->state = state;
    arg->prompt = prompt_text;

    pthread_t tid;
    pthread_create(&tid, NULL, agent_thread_fn, arg);
    pthread_detach(tid);
}

static void *agent_thread_fn(void *raw_arg) {
    typedef struct { InteractiveState *state; char *prompt; } AgentThreadArg;
    AgentThreadArg *arg = raw_arg;
    InteractiveState *state = arg->state;
    char *prompt_text = arg->prompt;
    free(arg);

    LOG_INFO("Agent thread: prompt='%.100s'", prompt_text);

    Message *prompt_msg = message_create_user(prompt_text);
    int rc = agent_prompt(state->agent, &prompt_msg, 1, &state->agent_config,
                          on_agent_event, state);

    LOG_INFO("Agent thread: agent_prompt returned %d", rc);

    state->phase = ISTATE_IDLE;
    pthread_mutex_lock(&state->mutex);
    state->renderer->is_streaming = false;
    state->needs_render = true;
    pthread_mutex_unlock(&state->mutex);

    free(prompt_text);
    return NULL;
}

/* ---- Helpers ---- */

static void collect_output(const char *data, size_t len, void *ctx) {
    str_append_len((Str *)ctx, data, len);
}

/* ---- Slash Commands ---- */

static void cmd_output(InteractiveState *state, const char *text) {
    linestore_add_system(state->store, text);
}

static void cmd_finish(InteractiveState *state) {
    state->needs_render = true;
    pthread_mutex_unlock(&state->mutex);
    input_clear(state);
    sync_input_to_renderer(state);
}

static bool handle_slash_command(InteractiveState *state) {
    const char *full = state->input_buf + 1;
    char cmd[64] = {0};
    const char *arg = NULL;

    const char *space = strchr(full, ' ');
    if (space) {
        int len = (int)(space - full);
        if (len > 63) len = 63;
        memcpy(cmd, full, len);
        arg = space + 1;
        while (*arg == ' ') arg++;
        if (!*arg) arg = NULL;
    } else {
        strncpy(cmd, full, 63);
    }

    /* /exit /quit /q */
    if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0 || strcmp(cmd, "q") == 0) {
        state->running = false;
        return true;
    }

    /* /clear — reset context, keep display */
    if (strcmp(cmd, "clear") == 0) {
        pthread_mutex_lock(&state->mutex);
        agent_state_reset(state->agent);
        state->total_tokens = 0;
        cmd_output(state, "context cleared");
        cmd_finish(state);
        return true;
    }

    /* /model [name] or /model list */
    if (strcmp(cmd, "model") == 0) {
        pthread_mutex_lock(&state->mutex);
        if (!arg) {
            char buf[256];
            snprintf(buf, sizeof(buf), "model: %s (%s)",
                     state->model ? state->model->name : "none",
                     state->model ? state->model->id : "none");
            cmd_output(state, buf);
            if (state->total_tokens > 0) {
                snprintf(buf, sizeof(buf), "tokens: %d", state->total_tokens);
                cmd_output(state, buf);
            }
        } else if (strcmp(arg, "list") == 0) {
            const char *provider = auth_get_active_provider();
            int count = 0;
            const Model **all = models_get_all(provider, &count);
            for (int i = 0; i < count; i++) {
                char buf[256];
                const char *marker = (state->model && strcmp(state->model->id, all[i]->id) == 0) ? " *" : "";
                snprintf(buf, sizeof(buf), "  %s (%s)%s", all[i]->name, all[i]->id, marker);
                cmd_output(state, buf);
            }
        } else {
            const char *provider = auth_get_active_provider();
            int count = 0;
            const Model **all = models_get_all(provider, &count);
            const Model *found = NULL;
            for (int i = 0; i < count; i++) {
                if (strstr(all[i]->id, arg) || strstr(all[i]->name, arg)) {
                    found = all[i];
                    break;
                }
            }
            if (found) {
                char *key = auth_get_api_key(found->provider);
                if (key) {
                    state->model = found;
                    free(state->api_key);
                    state->api_key = key;
                    state->agent->model = found;
                    state->agent_config.model = found;
                    state->agent_config.max_tokens = found->max_tokens;
                    state->agent_config.api_key = key;
                    free(state->agent->system_prompt);
                    state->agent->system_prompt = system_prompt_build(
                        state->tools, state->tool_count, state->cwd);
                    char buf[256];
                    snprintf(buf, sizeof(buf), "switched to %s", found->name);
                    cmd_output(state, buf);
                } else {
                    cmd_output(state, "no API key for that model's provider");
                }
            } else {
                cmd_output(state, "model not found — try /model list");
            }
        }
        cmd_finish(state);
        return true;
    }

    /* /tools */
    if (strcmp(cmd, "tools") == 0) {
        pthread_mutex_lock(&state->mutex);
        for (int i = 0; i < state->tool_count; i++) {
            char buf[256];
            snprintf(buf, sizeof(buf), "  %s — %s",
                     state->tools[i].name,
                     state->tools[i].description ? state->tools[i].description : "");
            cmd_output(state, buf);
        }
        cmd_finish(state);
        return true;
    }

    /* /run <command> */
    if (strcmp(cmd, "run") == 0) {
        if (!arg) {
            pthread_mutex_lock(&state->mutex);
            cmd_output(state, "usage: /run <command>");
            cmd_finish(state);
            return true;
        }
        pthread_mutex_lock(&state->mutex);
        char label[256];
        snprintf(label, sizeof(label), "$ %s", arg);
        cmd_output(state, label);
        pthread_mutex_unlock(&state->mutex);

        Str output = str_new(4096);
        ProcessOptions opts = {
            .command = arg,
            .cwd = state->cwd,
            .timeout_ms = 30000,
            .on_stdout = collect_output,
            .on_stderr = collect_output,
            .ctx = &output,
        };
        ProcessResult result;
        process_run(&opts, &result);

        pthread_mutex_lock(&state->mutex);
        if (output.len > 0) {
            linestore_add_tool_output(state->store, output.data);
        }
        char rc_buf[64];
        snprintf(rc_buf, sizeof(rc_buf), "exit %d", result.exit_code);
        cmd_output(state, rc_buf);
        str_free(&output);
        cmd_finish(state);
        return true;
    }

    /* /undo */
    if (strcmp(cmd, "undo") == 0) {
        pthread_mutex_lock(&state->mutex);
        if (state->agent->message_count >= 2) {
            /* Remove last assistant + user message pair from agent state */
            state->agent->message_count -= 2;
        }
        /* Remove lines until we hit a previous user text or empty */
        int target = state->store->count;
        int user_msgs_found = 0;
        for (int i = state->store->count - 1; i >= 0; i--) {
            if (state->store->lines[i].type == LINE_USER_TEXT) {
                user_msgs_found++;
                if (user_msgs_found == 1) {
                    target = i;
                    break;
                }
            }
        }
        while (state->store->count > target) {
            state->store->count--;
            state->store->total_screen_rows -= state->store->lines[state->store->count].wrap_count;
            free(state->store->lines[state->store->count].raw_text);
            free(state->store->lines[state->store->count].spans);
            state->store->lines[state->store->count].raw_text = NULL;
            state->store->lines[state->store->count].spans = NULL;
        }
        cmd_output(state, "undone");
        cmd_finish(state);
        return true;
    }

    /* /diff */
    if (strcmp(cmd, "diff") == 0) {
        pthread_mutex_lock(&state->mutex);
        cmd_output(state, "$ git diff --stat");
        pthread_mutex_unlock(&state->mutex);

        Str output = str_new(4096);
        ProcessOptions opts = {
            .command = "git diff --stat",
            .cwd = state->cwd,
            .timeout_ms = 10000,
            .on_stdout = collect_output,
            .on_stderr = collect_output,
            .ctx = &output,
        };
        ProcessResult result;
        process_run(&opts, &result);

        pthread_mutex_lock(&state->mutex);
        if (output.len > 0) {
            linestore_add_tool_output(state->store, output.data);
        } else {
            cmd_output(state, "no changes");
        }
        (void)result;
        str_free(&output);
        cmd_finish(state);
        return true;
    }

    /* /find <query> */
    if (strcmp(cmd, "find") == 0) {
        if (!arg) {
            pthread_mutex_lock(&state->mutex);
            cmd_output(state, "usage: /find <query>");
            cmd_finish(state);
            return true;
        }
        /* Send as a prompt to the agent with search instruction */
        char *prompt = malloc(strlen(arg) + 128);
        if (prompt) {
            snprintf(prompt, strlen(arg) + 128,
                "Search the codebase for: %s\nUse grep and read tools. "
                "Report relevant files and line numbers. Be concise.", arg);
            input_clear(state);
            /* Insert as prompt text and submit */
            input_insert(state, prompt, (int)strlen(prompt));
            free(prompt);
            handle_submit(state);
        }
        return true;
    }

    /* /context */
    if (strcmp(cmd, "context") == 0) {
        pthread_mutex_lock(&state->mutex);
        char buf[256];
        snprintf(buf, sizeof(buf), "messages: %d", state->agent->message_count);
        cmd_output(state, buf);
        snprintf(buf, sizeof(buf), "tokens used: %d", state->total_tokens);
        cmd_output(state, buf);
        int ctx = state->model ? state->model->context_window : 0;
        if (ctx > 0) {
            snprintf(buf, sizeof(buf), "context window: %d", ctx);
            cmd_output(state, buf);
        }
        snprintf(buf, sizeof(buf), "tools: %d", state->tool_count);
        cmd_output(state, buf);
        snprintf(buf, sizeof(buf), "display lines: %d", state->store->count);
        cmd_output(state, buf);
        cmd_finish(state);
        return true;
    }

    /* /sessions */
    if (strcmp(cmd, "sessions") == 0) {
        pthread_mutex_lock(&state->mutex);
        const char *dir = config_sessions_dir();
        if (!dir) { cmd_output(state, "no sessions directory"); cmd_finish(state); return true; }
        char list_cmd[512];
        snprintf(list_cmd, sizeof(list_cmd), "ls -1t '%s'/*.jsonl 2>/dev/null | head -20", dir);
        pthread_mutex_unlock(&state->mutex);

        Str output = str_new(1024);
        ProcessOptions opts = {
            .command = list_cmd,
            .timeout_ms = 5000,
            .on_stdout = collect_output,
            .ctx = &output,
        };
        ProcessResult result;
        process_run(&opts, &result);

        pthread_mutex_lock(&state->mutex);
        if (output.len > 0) {
            linestore_add_tool_output(state->store, output.data);
        } else {
            cmd_output(state, "no sessions found");
        }
        if (state->session) {
            char buf[256];
            snprintf(buf, sizeof(buf), "current: %s",
                     state->session->session_id ? state->session->session_id : "none");
            cmd_output(state, buf);
        }
        (void)result;
        str_free(&output);
        cmd_finish(state);
        return true;
    }

    /* /fork */
    if (strcmp(cmd, "fork") == 0) {
        pthread_mutex_lock(&state->mutex);
        if (state->session) {
            const char *dir = config_sessions_dir();
            Session *new_session = session_create(dir);
            if (new_session) {
                char buf[256];
                snprintf(buf, sizeof(buf), "forked to session %s", new_session->session_id);
                cmd_output(state, buf);
                session_free(state->session);
                state->session = new_session;
            } else {
                cmd_output(state, "failed to create new session");
            }
        }
        cmd_finish(state);
        return true;
    }

    /* /theme <name> */
    if (strcmp(cmd, "theme") == 0) {
        pthread_mutex_lock(&state->mutex);
        if (!arg) {
            cmd_output(state, "themes: default, midnight, ember, ghost, daylight");
            cmd_output(state, "usage: /theme <name>");
        } else if (strcmp(arg, "default") == 0) {
            state->lantern->config = lantern_defaults();
            lantern_rebuild_lut(state->lantern, state->renderer->term_height);
            cmd_output(state, "theme: default");
        } else if (strcmp(arg, "midnight") == 0) {
            state->lantern->config.warmth = (RGB){180, 190, 210};
            state->lantern->config.coolness = (RGB){60, 70, 90};
            state->lantern->config.accent = (RGB){100, 150, 220};
            lantern_rebuild_lut(state->lantern, state->renderer->term_height);
            cmd_output(state, "theme: midnight");
        } else if (strcmp(arg, "ember") == 0) {
            state->lantern->config.warmth = (RGB){240, 200, 170};
            state->lantern->config.coolness = (RGB){120, 80, 60};
            state->lantern->config.accent = (RGB){255, 120, 50};
            lantern_rebuild_lut(state->lantern, state->renderer->term_height);
            cmd_output(state, "theme: ember");
        } else if (strcmp(arg, "ghost") == 0) {
            state->lantern->config.warmth = (RGB){200, 210, 220};
            state->lantern->config.coolness = (RGB){60, 65, 75};
            state->lantern->config.accent = (RGB){136, 170, 204};
            state->lantern->config.floor = 0.02f;
            lantern_rebuild_lut(state->lantern, state->renderer->term_height);
            cmd_output(state, "theme: ghost");
        } else if (strcmp(arg, "daylight") == 0) {
            state->lantern->config.warmth = (RGB){40, 36, 30};
            state->lantern->config.coolness = (RGB){140, 135, 130};
            state->lantern->config.accent = (RGB){139, 105, 20};
            lantern_rebuild_lut(state->lantern, state->renderer->term_height);
            cmd_output(state, "theme: daylight");
        } else {
            cmd_output(state, "unknown theme — try: default, midnight, ember, ghost, daylight");
        }
        cmd_finish(state);
        return true;
    }

    /* /help [command] */
    if (strcmp(cmd, "help") == 0) {
        pthread_mutex_lock(&state->mutex);
        if (!arg) {
            cmd_output(state, "/help [cmd]  — show help");
            cmd_output(state, "/model [name|list] — show/switch model");
            cmd_output(state, "/tools       — list available tools");
            cmd_output(state, "/run <cmd>   — run shell command directly");
            cmd_output(state, "/find <query> — search codebase via agent");
            cmd_output(state, "/diff        — git diff --stat");
            cmd_output(state, "/undo        — remove last exchange");
            cmd_output(state, "/context     — show context window usage");
            cmd_output(state, "/sessions    — list saved sessions");
            cmd_output(state, "/fork        — fork to new session");
            cmd_output(state, "/theme <name> — switch color theme");
            cmd_output(state, "/clear       — clear conversation");
            cmd_output(state, "/exit        — quit (/q)");
        } else if (strcmp(arg, "model") == 0) {
            cmd_output(state, "/model         — show current model and token count");
            cmd_output(state, "/model list    — list all available models for your provider");
            cmd_output(state, "/model <name>  — switch to model matching name (partial match)");
        } else if (strcmp(arg, "theme") == 0) {
            cmd_output(state, "/theme         — list available themes");
            cmd_output(state, "/theme <name>  — switch theme");
            cmd_output(state, "  default  — warm amber on dark");
            cmd_output(state, "  midnight — cool blue");
            cmd_output(state, "  ember    — hot orange");
            cmd_output(state, "  ghost    — pale blue, deep fade");
            cmd_output(state, "  daylight — dark text (for light terminals)");
        } else if (strcmp(arg, "find") == 0) {
            cmd_output(state, "/find <query>  — semantic search via agent");
            cmd_output(state, "sends query to the model with instruction to grep/read");
        } else if (strcmp(arg, "run") == 0) {
            cmd_output(state, "/run <cmd>     — run shell command, show output");
            cmd_output(state, "bypasses the agent, runs directly");
        } else {
            char buf[128];
            snprintf(buf, sizeof(buf), "no help for '%s'", arg);
            cmd_output(state, buf);
        }
        cmd_finish(state);
        return true;
    }

    /* Unknown command */
    pthread_mutex_lock(&state->mutex);
    char buf[128];
    snprintf(buf, sizeof(buf), "unknown command: /%s — try /help", cmd);
    linestore_add_error(state->store, buf);
    cmd_finish(state);
    return true;
}

/* ---- Key Processing ---- */

static bool handle_key(InteractiveState *state, const ParsedKey *key) {
    LOG_DEBUG("KEY: id='%s' phase=%d raw_len=%d", key->id, state->phase, key->raw_len);
    if (key_matches(key, "pageup")) {
        lantern_renderer_scroll_up(state->renderer, state->renderer->term_height - 2);
        return true;
    }
    if (key_matches(key, "pagedown")) {
        lantern_renderer_scroll_down(state->renderer, state->renderer->term_height - 2);
        return true;
    }
    if (key_matches(key, "ctrl+u")) {
        lantern_renderer_scroll_up(state->renderer, state->renderer->term_height / 2);
        return true;
    }
    if (key_matches(key, "ctrl+d")) {
        lantern_renderer_scroll_down(state->renderer, state->renderer->term_height / 2);
        return true;
    }
    if (key_matches(key, "end")) {
        lantern_renderer_scroll_to_bottom(state->renderer);
        return true;
    }
    if (key_matches(key, "up") || key_matches(key, "shift+up")) {
        lantern_renderer_scroll_up(state->renderer, 3);
        return true;
    }
    if (key_matches(key, "down") || key_matches(key, "shift+down")) {
        lantern_renderer_scroll_down(state->renderer, 3);
        return true;
    }
    if (key_matches(key, "enter")) {
        if (state->phase == ISTATE_IDLE) {
            if (state->input_len > 0 && state->input_buf[0] == '/') {
                handle_slash_command(state);
            } else {
                handle_submit(state);
            }
        }
        return true;
    }
    if (key_matches(key, "ctrl+c")) {
        if (state->phase == ISTATE_STREAMING) {
            agent_abort(state->agent);
            state->phase = ISTATE_IDLE;
            state->renderer->is_streaming = false;
            pthread_mutex_lock(&state->mutex);
            linestore_flush_stream(state->store);
            linestore_add_system(state->store, "interrupted");
            state->needs_render = true;
            pthread_mutex_unlock(&state->mutex);
        } else {
            state->running = false;
        }
        return true;
    }
    if (key_matches(key, "escape") || key_matches(key, "ctrl+z")) {
        if (state->phase == ISTATE_STREAMING) {
            agent_abort(state->agent);
            state->phase = ISTATE_IDLE;
            state->renderer->is_streaming = false;

            pthread_mutex_lock(&state->mutex);
            /* If no assistant content yet, restore prompt to input */
            bool has_response = (state->store->count > state->store->stream_start_idx);
            if (!has_response && state->last_prompt) {
                /* Remove user text + blank we added */
                while (state->store->count > state->last_prompt_store_idx) {
                    state->store->count--;
                    state->store->total_screen_rows -= state->store->lines[state->store->count].wrap_count;
                    free(state->store->lines[state->store->count].raw_text);
                    free(state->store->lines[state->store->count].spans);
                    state->store->lines[state->store->count].raw_text = NULL;
                    state->store->lines[state->store->count].spans = NULL;
                }
                input_insert(state, state->last_prompt, (int)strlen(state->last_prompt));
                sync_input_to_renderer(state);
            } else {
                linestore_flush_stream(state->store);
                linestore_add_system(state->store, "interrupted");
            }
            state->needs_render = true;
            pthread_mutex_unlock(&state->mutex);
        }
        return true;
    }
    if (key_matches(key, "ctrl+l")) {
        lantern_renderer_render_full(state->renderer);
        return true;
    }
    if (key_matches(key, "backspace")) {
        input_backspace(state);
        sync_input_to_renderer(state);
        state->needs_render = true;
        return true;
    }
    if (key_matches(key, "ctrl+a") || key_matches(key, "home")) {
        state->input_cursor = 0;
        sync_input_to_renderer(state);
        state->needs_render = true;
        return true;
    }
    if (key_matches(key, "ctrl+e")) {
        state->input_cursor = state->input_len;
        sync_input_to_renderer(state);
        state->needs_render = true;
        return true;
    }
    if (key_matches(key, "ctrl+u")) {
        memmove(state->input_buf, state->input_buf + state->input_cursor,
                state->input_len - state->input_cursor);
        state->input_len -= state->input_cursor;
        state->input_cursor = 0;
        state->input_buf[state->input_len] = '\0';
        sync_input_to_renderer(state);
        state->needs_render = true;
        return true;
    }
    if (key_matches(key, "ctrl+k")) {
        state->input_len = state->input_cursor;
        state->input_buf[state->input_len] = '\0';
        sync_input_to_renderer(state);
        state->needs_render = true;
        return true;
    }
    if (key_matches(key, "left") || key_matches(key, "ctrl+b")) {
        if (state->input_cursor > 0) state->input_cursor--;
        sync_input_to_renderer(state);
        state->needs_render = true;
        return true;
    }
    if (key_matches(key, "right") || key_matches(key, "ctrl+f")) {
        if (state->input_cursor < state->input_len) state->input_cursor++;
        sync_input_to_renderer(state);
        state->needs_render = true;
        return true;
    }

    /* Printable characters */
    if (key->printable[0]) {
        input_insert(state, key->printable, (int)strlen(key->printable));
        sync_input_to_renderer(state);
        state->needs_render = true;
        return true;
    }

    return false;
}

/* ---- Main Loop ---- */

static volatile sig_atomic_t g_winch = 0;
static void winch_handler(int sig) { (void)sig; g_winch = 1; }

int interactive_mode_start(PiInstance *pi, const char *session_id,
                           const char *model_pattern, const char *provider) {
    if (!pi) return -1;

    const char *agent_dir = config_agent_dir();
    if (agent_dir) {
        fs_mkdir_p(agent_dir);
        char log_path[512];
        snprintf(log_path, sizeof(log_path), "%s/rig.log", agent_dir);
        pi_log_open(log_path);
        pi_log_set_level(LOG_DEBUG);
    }
    LOG_INFO("=== Pi starting ===");

    http_global_init();
    ai_registry_init();
    models_init();
    anthropic_register();
    openai_completions_register();
    openai_responses_register();
    google_provider_register();
    bedrock_provider_register();
    mistral_provider_register();

    /* Use saved auth provider if no --provider flag */
    const char *effective_provider = provider;
    if (!effective_provider) {
        effective_provider = auth_get_active_provider();
    }

    const Model *model = NULL;
    char *api_key = NULL;

    int all_count = 0;
    const Model **all_models = models_get_all(effective_provider, &all_count);
    for (int i = 0; i < all_count && !api_key; i++) {
        if (model_pattern) {
            if (!strstr(all_models[i]->id, model_pattern) &&
                !strstr(all_models[i]->name, model_pattern)) continue;
        }
        char *key = auth_get_api_key(all_models[i]->provider);
        if (key) { model = all_models[i]; api_key = key; }
    }

    LOG_INFO("Model: %s (%s)", model ? model->id : "none", model ? model->provider : "none");

    /* Create state */
    InteractiveState *state = calloc(1, sizeof(InteractiveState));
    state->pi = pi;
    state->model = model;
    state->api_key = api_key;
    state->phase = ISTATE_IDLE;
    state->running = true;
    pthread_mutex_init(&state->mutex, NULL);

    input_init(state);

    /* Agent */
    state->agent = agent_state_create();
    state->agent->model = model;
    state->agent->thinking_level = THINKING_OFF;

    getcwd(state->cwd, sizeof(state->cwd));
    state->tools[state->tool_count++] = tool_bash_create(state->cwd);
    state->tools[state->tool_count++] = tool_read_create();
    state->tools[state->tool_count++] = tool_write_create();
    state->tools[state->tool_count++] = tool_edit_create();
    state->tools[state->tool_count++] = tool_grep_create();
    state->tools[state->tool_count++] = tool_ls_create();

    state->agent->tools = state->tools;
    state->agent->tool_count = state->tool_count;
    state->agent->system_prompt = system_prompt_build(state->tools, state->tool_count, state->cwd);

    state->agent_config = (AgentLoopConfig){
        .model = model,
        .tool_execution = TOOL_EXEC_PARALLEL,
        .temperature = -1,
        .max_tokens = model ? model->max_tokens : 4096,
        .reasoning = THINKING_OFF,
        .api_key = state->api_key,
        .timeout_ms = 120000,
        .abort_flag = &state->agent->abort_requested,
    };

    /* Lantern */
    state->lantern = lantern_create(NULL);
    lantern_detect_color_tier(state->lantern);
    state->store = linestore_create();
    state->renderer = lantern_renderer_create(state->lantern, state->store);

    /* Session */
    const char *sessions_dir = config_sessions_dir();
    if (session_id) {
        Str path = str_new(256);
        str_appendf(&path, "%s/%s.jsonl", sessions_dir, session_id);
        state->session = session_load(path.data);
        str_free(&path);
        if (!state->session) {
            state->session = session_create(sessions_dir);
        }
    } else {
        state->session = session_create(sessions_dir);
    }

    /* Terminal setup */
    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "Error: Interactive mode requires a terminal.\n");
        free(state);
        return -1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = winch_handler;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGWINCH, &sa, NULL);

    terminal_enter_alt_screen();
    terminal_enter_raw_mode();
    terminal_enable_kitty_keyboard();
    terminal_enable_bracketed_paste();

    int tw, th;
    terminal_get_size(&tw, &th);
    lantern_renderer_resize(state->renderer, tw, th);

    /* Startup content */
    linestore_add_blank(state->store);

    if (!model || !api_key) {
        linestore_add_error(state->store, "no API key configured");
        linestore_add_system(state->store,
            "set ANTHROPIC_API_KEY, OPENAI_API_KEY, GOOGLE_API_KEY, MISTRAL_API_KEY, or AWS_BEARER_TOKEN_BEDROCK");
        linestore_add_blank(state->store);
    }

    lantern_renderer_render_full(state->renderer);

    /* Event loop */
    struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
    int tick_counter = 0;

    while (state->running) {
        if (g_winch) {
            g_winch = 0;
            terminal_get_size(&tw, &th);
            lantern_renderer_resize(state->renderer, tw, th);
            lantern_renderer_render_full(state->renderer);
        }

        int ready = poll(&pfd, 1, 16);

        if (ready > 0 && (pfd.revents & POLLIN)) {
            char buf[256];
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                ParsedKey key = key_parse(buf, (int)n);
                handle_key(state, &key);
                if (!state->running) break;
            }
        }

        /* Spinner tick every ~200ms (12 frames * 16ms) */
        tick_counter++;
        if (tick_counter >= 12) {
            tick_counter = 0;
            lantern_renderer_tick_spinner(state->renderer);
        }

        pthread_mutex_lock(&state->mutex);
        bool do_render = state->needs_render || state->renderer->dirty;
        state->needs_render = false;
        pthread_mutex_unlock(&state->mutex);
        if (do_render) {
            lantern_renderer_render(state->renderer);
        }
    }

    /* Cleanup */
    terminal_disable_bracketed_paste();
    terminal_disable_kitty_keyboard();
    terminal_show_cursor();
    terminal_exit_raw_mode();
    terminal_exit_alt_screen();

    lantern_renderer_free(state->renderer);
    linestore_free(state->store);
    lantern_free(state->lantern);

    if (state->agent) agent_state_free(state->agent);
    if (state->session) session_free(state->session);
    pthread_mutex_destroy(&state->mutex);
    free(state->input_buf);
    free(state->api_key);
    free(state->last_prompt);
    free(state);

    ai_registry_cleanup();
    http_global_cleanup();
    pi_log_close();

    return 0;
}
