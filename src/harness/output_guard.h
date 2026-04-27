#ifndef RIG_HARNESS_OUTPUT_GUARD_H
#define RIG_HARNESS_OUTPUT_GUARD_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    size_t max_output_bytes;
    int max_output_lines;
    int truncation_lines;
    bool warn_on_truncation;
} OutputGuardConfig;

typedef struct {
    char *content;
    size_t content_len;
    bool was_truncated;
    size_t original_bytes;
    int original_lines;
} GuardedOutput;

GuardedOutput *output_guard_apply(const char *raw_output, size_t raw_len,
                                   OutputGuardConfig *config);
void guarded_output_free(GuardedOutput *go);

OutputGuardConfig output_guard_defaults(void);

#endif
