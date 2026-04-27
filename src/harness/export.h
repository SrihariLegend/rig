#ifndef RIG_HARNESS_EXPORT_H
#define RIG_HARNESS_EXPORT_H

#include "harness/session.h"
#include <stdbool.h>

typedef struct {
    bool include_thinking;
    bool include_tool_calls;
    bool include_timestamps;
    bool dark_theme;
    char *title;
} ExportConfig;

/* Export session to self-contained HTML file. Returns 0 on success. */
int session_export_html(Session *s, const char *output_path, ExportConfig *config);

/* Export to heap-allocated HTML string. Caller frees. Returns NULL on error. */
char *session_export_html_string(Session *s, ExportConfig *config);

/* Return sensible defaults (thinking on, tool calls on, timestamps on, dark theme). */
ExportConfig export_config_defaults(void);

#endif
