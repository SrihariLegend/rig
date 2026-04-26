#include "compaction.h"
#include "util/log.h"
#include <stdlib.h>
#include <string.h>

/* ---- Defaults ---- */

CompactionConfig compaction_config_default(void) {
    return (CompactionConfig){
        .enabled = true,
        .reserve_tokens = 16384,
        .keep_recent_tokens = 20000,
    };
}

/* ---- Token estimation ---- */

static int estimate_content_block_tokens(const ContentBlock *b) {
    if (!b) return 0;
    int chars = 0;
    switch (b->type) {
        case CONTENT_TEXT:
            if (b->text.text) chars = (int)strlen(b->text.text);
            break;
        case CONTENT_THINKING:
            if (b->thinking.thinking) chars = (int)strlen(b->thinking.thinking);
            break;
        case CONTENT_TOOL_CALL:
            if (b->tool_call.name) chars += (int)strlen(b->tool_call.name);
            if (b->tool_call.partial_json) chars += (int)strlen(b->tool_call.partial_json);
            if (b->tool_call.arguments) {
                char *s = cJSON_PrintUnformatted(b->tool_call.arguments);
                if (s) {
                    chars += (int)strlen(s);
                    free(s);
                }
            }
            break;
        case CONTENT_IMAGE:
            /* Images count as roughly 1000 tokens */
            return 1000;
    }
    /* Rough approximation: 1 token per 4 characters */
    return chars > 0 ? (chars + 3) / 4 : 0;
}

int estimate_tokens_message(const Message *msg) {
    if (!msg) return 0;
    int total = 0;
    for (int i = 0; i < msg->content_count; i++) {
        total += estimate_content_block_tokens(&msg->content[i]);
    }
    /* Add small overhead for message structure (role, metadata) */
    total += 4;
    return total;
}

int estimate_tokens_messages(Message **msgs, int count) {
    if (!msgs || count <= 0) return 0;
    int total = 0;
    for (int i = 0; i < count; i++) {
        total += estimate_tokens_message(msgs[i]);
    }
    return total;
}

/* ---- Compaction check ---- */

bool needs_compaction(Message **msgs, int msg_count, int context_window,
                      CompactionConfig *config) {
    if (!msgs || msg_count <= 0 || !config || !config->enabled) return false;

    int estimated = estimate_tokens_messages(msgs, msg_count);
    int threshold = context_window - config->reserve_tokens;
    return estimated > threshold;
}

/* ---- Compaction ---- */

int compact_messages(Message **msgs, int msg_count, int context_window,
                     CompactionConfig *config, Message ***out, int *out_count) {
    if (!out || !out_count) return -1;
    if (!msgs || msg_count <= 0) {
        *out = NULL;
        *out_count = 0;
        return 0;
    }

    CompactionConfig cfg = config ? *config : compaction_config_default();

    int estimated = estimate_tokens_messages(msgs, msg_count);
    int budget = context_window - cfg.reserve_tokens;

    /* If within budget, clone everything as-is */
    if (estimated <= budget) {
        Message **result = calloc((size_t)msg_count, sizeof(Message *));
        for (int i = 0; i < msg_count; i++) {
            result[i] = message_clone(msgs[i]);
        }
        *out = result;
        *out_count = msg_count;
        return 0;
    }

    /* Strategy: keep first message (system) + as many recent messages as fit */
    int first_tokens = estimate_tokens_message(msgs[0]);

    /* Walk backwards from the end, accumulating recent messages */
    int recent_tokens = 0;
    int recent_start = msg_count; /* exclusive start of recent window */

    for (int i = msg_count - 1; i >= 1; i--) {
        int msg_tokens = estimate_tokens_message(msgs[i]);
        if (recent_tokens + msg_tokens + first_tokens > budget) {
            break;
        }
        recent_tokens += msg_tokens;
        recent_start = i;
    }

    /* If we can't even fit the first message + 1 recent, just keep last message */
    if (recent_start >= msg_count) {
        recent_start = msg_count - 1;
    }

    int kept_count = 1 + (msg_count - recent_start); /* first + recent tail */
    Message **result = calloc((size_t)kept_count, sizeof(Message *));
    int n = 0;

    /* Keep the first message */
    result[n++] = message_clone(msgs[0]);

    /* Keep the recent tail */
    for (int i = recent_start; i < msg_count; i++) {
        result[n++] = message_clone(msgs[i]);
    }

    int dropped = msg_count - kept_count;
    LOG_INFO("Compaction: kept %d messages (dropped %d middle), "
             "estimated %d -> %d tokens",
             kept_count, dropped, estimated, first_tokens + recent_tokens);

    *out = result;
    *out_count = n;
    return 0;
}
