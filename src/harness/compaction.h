#ifndef PI_HARNESS_COMPACTION_H
#define PI_HARNESS_COMPACTION_H

#include "ai/types.h"
#include <stdbool.h>

typedef struct {
    bool enabled;
    int reserve_tokens;      /* default 16384 */
    int keep_recent_tokens;  /* default 20000 */
} CompactionConfig;

/* Default configuration */
CompactionConfig compaction_config_default(void);

/* Estimate token count for a single message (rough: chars/4) */
int estimate_tokens_message(const Message *msg);

/* Estimate total tokens for an array of message pointers */
int estimate_tokens_messages(Message **msgs, int count);

/*
 * Compact messages to fit within token budget.
 * Keeps the first message (system context) + the most recent messages
 * that fit in keep_recent_tokens. Drops middle messages.
 *
 * Returns 0 on success, -1 on error.
 * Sets *out to a new heap-allocated array of cloned Message pointers.
 * Sets *out_count to the number of messages in the output.
 * Caller must free each message with message_free() and free the array.
 */
int compact_messages(Message **msgs, int msg_count, int context_window,
                     CompactionConfig *config, Message ***out, int *out_count);

/*
 * Check if compaction is needed based on current token usage vs context window.
 * Returns true if estimated tokens exceed (context_window - reserve_tokens).
 */
bool needs_compaction(Message **msgs, int msg_count, int context_window,
                      CompactionConfig *config);

#endif
