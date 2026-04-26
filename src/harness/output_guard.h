#ifndef PI_HARNESS_OUTPUT_GUARD_H
#define PI_HARNESS_OUTPUT_GUARD_H

#include <stdbool.h>

typedef struct {
    int max_output_bytes;     /* default 512KB */
    int max_output_lines;     /* default 10000 */
    int truncation_lines;     /* show first N + last N lines, default 50 */
    bool warn_on_truncation;  /* default true */
} OutputGuardConfig;

typedef struct {
    char *content;
    int content_len;
    bool was_truncated;
    int original_bytes;
    int original_lines;
} GuardedOutput;

GuardedOutput *output_guard_apply(const char *raw_output, int raw_len,
                                   OutputGuardConfig *config);
void guarded_output_free(GuardedOutput *go);

OutputGuardConfig output_guard_defaults(void);

#endif
