#ifndef PI_LINESTORE_H
#define PI_LINESTORE_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    LINE_BLANK,
    LINE_USER_TEXT,
    LINE_ASSISTANT_TEXT,
    LINE_HEADING,
    LINE_CODE,
    LINE_CODE_LANG,
    LINE_BLOCKQUOTE,
    LINE_LIST_ITEM,
    LINE_TOOL_START,
    LINE_TOOL_OUTPUT,
    LINE_TOOL_DONE,
    LINE_ERROR,
    LINE_SYSTEM,
    LINE_SEPARATOR,
    LINE_TABLE_ROW,
    LINE_TABLE_SEPARATOR,
    LINE_SPLASH,
} LineType;

typedef enum {
    SPAN_PLAIN   = 0,
    SPAN_BOLD    = 1 << 0,
    SPAN_ITALIC  = 1 << 1,
    SPAN_CODE    = 1 << 2,
    SPAN_ACCENT  = 1 << 3,
    SPAN_STRIKE  = 1 << 4,
} SpanFlags;

typedef struct {
    const char *text;
    int len;
    SpanFlags flags;
    float brightness;   /* 0.0-1.0, used by LINE_SPLASH */
} Span;

typedef struct {
    LineType type;
    uint16_t indent;
    uint16_t heading_level;
    uint32_t msg_index;

    char *raw_text;
    Span *spans;
    int span_count;

    uint16_t wrap_count;
    float brightness;    /* 0.0-1.0, used by LINE_SPLASH */
} StoreLine;

typedef struct {
    StoreLine *lines;
    int count;
    int capacity;
    int total_screen_rows;
    int content_width;

    uint32_t current_msg;
    bool in_code_block;
    char code_lang[32];

    char *stream_buf;
    int stream_len;
    int stream_cap;
    int stream_start_idx;
    int stream_start_rows;
    bool needs_scroll_reset;
} LineStore;

LineStore *linestore_create(void);
void linestore_free(LineStore *ls);
void linestore_clear(LineStore *ls);

void linestore_set_width(LineStore *ls, int content_width);
void linestore_reflow(LineStore *ls);

void linestore_add_blank(LineStore *ls);
void linestore_add_system(LineStore *ls, const char *text);
void linestore_add_user_text(LineStore *ls, const char *text);
void linestore_begin_message(LineStore *ls, uint32_t msg_index);

void linestore_append_assistant_text(LineStore *ls, const char *text);
void linestore_flush_stream(LineStore *ls);
void linestore_add_tool_start(LineStore *ls, const char *name, const char *args);
void linestore_add_tool_output(LineStore *ls, const char *text);
void linestore_add_tool_done(LineStore *ls, const char *name);
void linestore_add_error(LineStore *ls, const char *text);
void linestore_add_splash(LineStore *ls, const char *text, float brightness);
void linestore_add_splash_layered(LineStore *ls, const char *text, const char *layers);

int linestore_screen_row_count(const LineStore *ls);

typedef struct {
    int line_index;
    int wrap_offset;
} ScreenRowRef;

ScreenRowRef linestore_row_to_line(const LineStore *ls, int screen_row);

void linestore_parse_inline_spans(StoreLine *line);
int linestore_compute_wrap(const char *text, int content_width, int indent);

#endif
