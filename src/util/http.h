#ifndef PI_HTTP_H
#define PI_HTTP_H

#include "cjson/cJSON.h"
#include <stdbool.h>
#include <stddef.h>

// HTTP response
typedef struct {
    int status_code;
    char *body;
    size_t body_len;
    char *headers;          // raw response headers
} HttpResponse;

void http_response_free(HttpResponse *r);

// Simple HTTP request
typedef struct {
    const char *url;
    const char *method;         // "GET", "POST", etc. Default: "GET"
    const char **headers;       // NULL-terminated array of "Key: Value" strings
    const char *body;
    size_t body_len;
    int timeout_ms;             // 0 = no timeout
    bool verbose;               // curl verbose logging
} HttpRequest;

// Synchronous HTTP request. Returns 0 on success, -1 on error.
int http_request(const HttpRequest *req, HttpResponse *resp);

// SSE (Server-Sent Events) stream parser
typedef struct {
    char *event_type;           // current "event:" value (owned)
    char *data_buf;             // accumulated "data:" lines (owned)
    size_t data_len;
    size_t data_cap;
    bool has_data;              // true if any "data:" field seen
    char *line_buf;             // partial line buffer (owned)
    size_t line_len;
    size_t line_cap;
    void (*on_event)(const char *event_type, const char *data, void *ctx);
    void *ctx;
} SSEParser;

SSEParser *sse_parser_create(void (*on_event)(const char *event_type, const char *data, void *ctx), void *ctx);
void sse_parser_feed(SSEParser *p, const char *chunk, size_t len);
void sse_parser_destroy(SSEParser *p);

// Streaming HTTP POST with SSE parsing.
// Calls on_event for each SSE event. Returns 0 on success.
typedef struct {
    const char *url;
    const char **headers;       // NULL-terminated "Key: Value" strings
    const char *body;
    size_t body_len;
    int timeout_ms;
    void (*on_event)(const char *event_type, const char *data, void *ctx);
    void *ctx;
    volatile bool *abort_flag;  // set to true to abort streaming
} SSERequest;

int http_stream_sse(const SSERequest *req);

// Raw byte streaming POST (no SSE parsing)
typedef struct {
    const char *url;
    const char **headers;
    const char *body;
    size_t body_len;
    int timeout_ms;
    void (*on_data)(const unsigned char *data, size_t len, void *ctx);
    void *ctx;
    volatile bool *abort_flag;
} RawStreamRequest;

int http_stream_raw(const RawStreamRequest *req);

// Global init/cleanup (call once)
void http_global_init(void);
void http_global_cleanup(void);

#endif
