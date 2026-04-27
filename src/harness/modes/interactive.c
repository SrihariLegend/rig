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

/* ---- Key Processing ---- */

static bool handle_key(InteractiveState *state, const ParsedKey *key) {
    if (key_matches(key, "pageup")) {
        lantern_renderer_scroll_up(state->renderer, state->renderer->term_height - 2);
        return true;
    }
    if (key_matches(key, "pagedown")) {
        lantern_renderer_scroll_down(state->renderer, state->renderer->term_height - 2);
        return true;
    }
    if (key_matches(key, "end")) {
        lantern_renderer_scroll_to_bottom(state->renderer);
        return true;
    }
    if (key_matches(key, "up") || key_matches(key, "shift+up")) {
        lantern_renderer_scroll_up(state->renderer, 1);
        return true;
    }
    if (key_matches(key, "down") || key_matches(key, "shift+down")) {
        lantern_renderer_scroll_down(state->renderer, 1);
        return true;
    }
    if (key_matches(key, "enter")) {
        if (state->phase == ISTATE_IDLE) {
            /* Check for slash commands */
            if (state->input_len > 0 && state->input_buf[0] == '/') {
                const char *cmd = state->input_buf + 1;
                if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0 || strcmp(cmd, "q") == 0) {
                    state->running = false;
                    return true;
                }
                if (strcmp(cmd, "clear") == 0) {
                    pthread_mutex_lock(&state->mutex);
                    while (state->store->count > 0) {
                        state->store->count--;
                        state->store->total_screen_rows -= state->store->lines[state->store->count].wrap_count;
                        free(state->store->lines[state->store->count].raw_text);
                        free(state->store->lines[state->store->count].spans);
                        state->store->lines[state->store->count].raw_text = NULL;
                        state->store->lines[state->store->count].spans = NULL;
                    }
                    state->store->total_screen_rows = 0;
                    state->renderer->scroll_offset = 0;
                    state->renderer->auto_scroll = true;
                    state->needs_render = true;
                    pthread_mutex_unlock(&state->mutex);
                    input_clear(state);
                    sync_input_to_renderer(state);
                    return true;
                }
                if (strcmp(cmd, "model") == 0) {
                    pthread_mutex_lock(&state->mutex);
                    const char *name = state->model ? state->model->name : "none";
                    const char *id = state->model ? state->model->id : "none";
                    char buf[256];
                    snprintf(buf, sizeof(buf), "model: %s (%s)", name, id);
                    linestore_add_system(state->store, buf);
                    if (state->total_tokens > 0) {
                        snprintf(buf, sizeof(buf), "tokens used: %d", state->total_tokens);
                        linestore_add_system(state->store, buf);
                    }
                    state->needs_render = true;
                    pthread_mutex_unlock(&state->mutex);
                    input_clear(state);
                    sync_input_to_renderer(state);
                    return true;
                }
                if (strcmp(cmd, "help") == 0) {
                    pthread_mutex_lock(&state->mutex);
                    linestore_add_system(state->store, "/help    — show this");
                    linestore_add_system(state->store, "/model   — show current model");
                    linestore_add_system(state->store, "/clear   — clear conversation");
                    linestore_add_system(state->store, "/exit    — quit (/q also works)");
                    state->needs_render = true;
                    pthread_mutex_unlock(&state->mutex);
                    input_clear(state);
                    sync_input_to_renderer(state);
                    return true;
                }
                /* Unknown command */
                pthread_mutex_lock(&state->mutex);
                char buf[128];
                snprintf(buf, sizeof(buf), "unknown command: %s", state->input_buf);
                linestore_add_error(state->store, buf);
                state->needs_render = true;
                pthread_mutex_unlock(&state->mutex);
                input_clear(state);
                sync_input_to_renderer(state);
                return true;
            }
            handle_submit(state);
        }
        return true;
    }
    if (key_matches(key, "ctrl+c")) {
        if (state->phase == ISTATE_STREAMING) {
            agent_abort(state->agent);
            state->phase = ISTATE_IDLE;
            pthread_mutex_lock(&state->mutex);
            linestore_add_system(state->store, "interrupted");
            state->needs_render = true;
            pthread_mutex_unlock(&state->mutex);
        } else {
            state->running = false;
        }
        return true;
    }
    if (key_matches(key, "escape")) {
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
        snprintf(log_path, sizeof(log_path), "%s/pi.log", agent_dir);
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

    return 0;
}
