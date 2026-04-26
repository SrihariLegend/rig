/* output_guard.c — truncate large tool output to stay within limits */
#include "harness/output_guard.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define DEFAULT_MAX_BYTES  (512 * 1024)
#define DEFAULT_MAX_LINES  10000
#define DEFAULT_TRUNC_LINES 50

OutputGuardConfig output_guard_defaults(void) {
    return (OutputGuardConfig){
        .max_output_bytes = DEFAULT_MAX_BYTES,
        .max_output_lines = DEFAULT_MAX_LINES,
        .truncation_lines = DEFAULT_TRUNC_LINES,
        .warn_on_truncation = true,
    };
}

/* Count newlines in buffer */
static int count_lines(const char *data, int len) {
    int lines = 0;
    for (int i = 0; i < len; i++) {
        if (data[i] == '\n') lines++;
    }
    /* Count the last line even without trailing newline */
    if (len > 0 && data[len - 1] != '\n') lines++;
    return lines;
}

/* Find the byte offset just after the Nth newline (or end of buffer) */
static int find_line_offset(const char *data, int len, int target_line) {
    int line = 0;
    for (int i = 0; i < len; i++) {
        if (data[i] == '\n') {
            line++;
            if (line >= target_line) return i + 1;
        }
    }
    return len;
}

/* Find the byte offset of the start of the Nth-from-last line.
   We want the last target_lines complete lines. Walk backwards, counting
   newlines. The first newline from the end terminates the last line, so
   we need target_lines+1 newlines (or BOF) to capture target_lines lines. */
static int find_line_offset_from_end(const char *data, int len, int target_lines) {
    int newlines = 0;
    /* Skip a trailing newline at the very end (it terminates the last line,
       not an empty line after it) */
    int start = len - 1;
    if (start >= 0 && data[start] == '\n') start--;

    for (int i = start; i >= 0; i--) {
        if (data[i] == '\n') {
            newlines++;
            if (newlines >= target_lines) return i + 1;
        }
    }
    return 0;
}

GuardedOutput *output_guard_apply(const char *raw_output, int raw_len,
                                   OutputGuardConfig *config) {
    OutputGuardConfig cfg;
    if (config) {
        cfg = *config;
    } else {
        cfg = output_guard_defaults();
    }

    GuardedOutput *go = calloc(1, sizeof(GuardedOutput));
    if (!go) return NULL;

    /* Handle NULL/empty input */
    if (!raw_output || raw_len <= 0) {
        go->content = strdup("");
        go->content_len = 0;
        go->was_truncated = false;
        go->original_bytes = 0;
        go->original_lines = 0;
        return go;
    }

    go->original_bytes = raw_len;
    go->original_lines = count_lines(raw_output, raw_len);

    bool exceeds_bytes = raw_len > cfg.max_output_bytes;
    bool exceeds_lines = go->original_lines > cfg.max_output_lines;

    if (!exceeds_bytes && !exceeds_lines) {
        /* No truncation needed */
        go->content = strndup(raw_output, (size_t)raw_len);
        go->content_len = raw_len;
        go->was_truncated = false;
        return go;
    }

    /* Truncation needed: keep first N + banner + last N lines */
    go->was_truncated = true;
    int trunc = cfg.truncation_lines;
    if (trunc < 1) trunc = 1;

    int first_end = find_line_offset(raw_output, raw_len, trunc);
    int last_start = find_line_offset_from_end(raw_output, raw_len, trunc);

    /* Ensure we don't overlap */
    if (last_start <= first_end) {
        /* Not enough lines to separate — just use the whole thing up to byte limit */
        int use_len = raw_len < cfg.max_output_bytes ? raw_len : cfg.max_output_bytes;
        go->content = strndup(raw_output, (size_t)use_len);
        go->content_len = use_len;
        return go;
    }

    int truncated_lines = go->original_lines - (trunc * 2);
    char banner[128];
    int banner_len = snprintf(banner, sizeof(banner),
        "\n... (%d lines truncated) ...\n", truncated_lines);

    int total = first_end + banner_len + (raw_len - last_start);
    go->content = malloc((size_t)total + 1);
    if (!go->content) {
        free(go);
        return NULL;
    }

    memcpy(go->content, raw_output, (size_t)first_end);
    memcpy(go->content + first_end, banner, (size_t)banner_len);
    memcpy(go->content + first_end + banner_len,
           raw_output + last_start, (size_t)(raw_len - last_start));
    go->content[total] = '\0';
    go->content_len = total;

    return go;
}

void guarded_output_free(GuardedOutput *go) {
    if (!go) return;
    free(go->content);
    free(go);
}
