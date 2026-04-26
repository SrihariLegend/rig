#include "http.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Memory buffer for curl write callback
typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} MemBuf;

static void membuf_init(MemBuf *buf) {
    buf->data = NULL;
    buf->size = 0;
    buf->capacity = 0;
}

static int membuf_append(MemBuf *buf, const char *data, size_t len) {
    if (buf->size + len > buf->capacity) {
        size_t new_cap = buf->capacity == 0 ? 4096 : buf->capacity * 2;
        while (new_cap < buf->size + len) {
            new_cap *= 2;
        }

        char *new_data = realloc(buf->data, new_cap);
        if (!new_data) return -1;

        buf->data = new_data;
        buf->capacity = new_cap;
    }

    memcpy(buf->data + buf->size, data, len);
    buf->size += len;

    return 0;
}

static void membuf_free(MemBuf *buf) {
    if (buf->data) {
        free(buf->data);
        buf->data = NULL;
    }
    buf->size = 0;
    buf->capacity = 0;
}

// Curl write callback
static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    MemBuf *buf = (MemBuf *)userdata;
    size_t total = size * nmemb;

    if (membuf_append(buf, ptr, total) != 0) {
        return 0;  // Signal error to curl
    }

    return total;
}

// Curl header callback
static size_t header_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    MemBuf *buf = (MemBuf *)userdata;
    size_t total = size * nmemb;

    if (membuf_append(buf, ptr, total) != 0) {
        return 0;
    }

    return total;
}

void http_global_init(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void http_global_cleanup(void) {
    curl_global_cleanup();
}

void http_response_free(HttpResponse *r) {
    if (!r) return;

    if (r->body) {
        free(r->body);
        r->body = NULL;
    }

    if (r->headers) {
        free(r->headers);
        r->headers = NULL;
    }

    r->body_len = 0;
    r->status_code = 0;
}

int http_request(const HttpRequest *req, HttpResponse *resp) {
    if (!req || !req->url || !resp) return -1;

    memset(resp, 0, sizeof(HttpResponse));

    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    MemBuf body_buf, header_buf;
    membuf_init(&body_buf);
    membuf_init(&header_buf);

    int result = -1;
    struct curl_slist *curl_headers = NULL;

    // Set URL
    curl_easy_setopt(curl, CURLOPT_URL, req->url);

    // Set method
    const char *method = req->method ? req->method : "GET";
    if (strcmp(method, "GET") == 0) {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    } else if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
    } else if (strcmp(method, "PUT") == 0) {
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    } else if (strcmp(method, "DELETE") == 0) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    }

    // Set headers
    if (req->headers) {
        for (int i = 0; req->headers[i] != NULL; i++) {
            curl_headers = curl_slist_append(curl_headers, req->headers[i]);
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers);
    }

    // Set body
    if (req->body && req->body_len > 0) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req->body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)req->body_len);
    }

    // Set timeout
    if (req->timeout_ms > 0) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)req->timeout_ms);
    }

    // Set verbose
    if (req->verbose) {
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    }

    // Set write callbacks
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body_buf);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &header_buf);

    // Perform request
    CURLcode res = curl_easy_perform(curl);

    if (res == CURLE_OK) {
        // Get status code
        long status_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
        resp->status_code = (int)status_code;

        // Copy body (add null terminator)
        if (body_buf.size > 0) {
            resp->body = malloc(body_buf.size + 1);
            if (resp->body) {
                memcpy(resp->body, body_buf.data, body_buf.size);
                resp->body[body_buf.size] = '\0';
                resp->body_len = body_buf.size;
            }
        }

        // Copy headers (add null terminator)
        if (header_buf.size > 0) {
            resp->headers = malloc(header_buf.size + 1);
            if (resp->headers) {
                memcpy(resp->headers, header_buf.data, header_buf.size);
                resp->headers[header_buf.size] = '\0';
            }
        }

        result = 0;
    }

    // Cleanup
    if (curl_headers) {
        curl_slist_free_all(curl_headers);
    }
    curl_easy_cleanup(curl);
    membuf_free(&body_buf);
    membuf_free(&header_buf);

    return result;
}

// SSE Parser implementation

SSEParser *sse_parser_create(void (*on_event)(const char *event_type, const char *data, void *ctx), void *ctx) {
    if (!on_event) return NULL;

    SSEParser *p = calloc(1, sizeof(SSEParser));
    if (!p) return NULL;

    p->on_event = on_event;
    p->ctx = ctx;

    return p;
}

static void sse_parser_reset_event(SSEParser *p) {
    if (p->event_type) {
        free(p->event_type);
        p->event_type = NULL;
    }
    p->data_len = 0;
    p->has_data = false;
}

static void sse_parser_dispatch(SSEParser *p) {
    if (!p->has_data && !p->event_type) return;

    // Null-terminate data
    if (p->data_len > 0 && p->data_buf[p->data_len - 1] == '\n') {
        p->data_len--;  // Remove trailing newline
    }

    if (p->data_cap > p->data_len) {
        p->data_buf[p->data_len] = '\0';
    } else {
        // Ensure space for null terminator
        char *new_buf = realloc(p->data_buf, p->data_len + 1);
        if (new_buf) {
            p->data_buf = new_buf;
            p->data_cap = p->data_len + 1;
            p->data_buf[p->data_len] = '\0';
        }
    }

    const char *event_type = p->event_type ? p->event_type : "message";
    const char *data = p->data_len > 0 ? p->data_buf : "";

    p->on_event(event_type, data, p->ctx);

    sse_parser_reset_event(p);
}

static void sse_parser_process_line(SSEParser *p, const char *line, size_t len) {
    // Empty line = dispatch event
    if (len == 0) {
        sse_parser_dispatch(p);
        return;
    }

    // Comment line
    if (line[0] == ':') {
        return;
    }

    // Find colon separator
    const char *colon = memchr(line, ':', len);
    if (!colon) return;

    size_t field_len = colon - line;
    const char *value = colon + 1;
    size_t value_len = len - field_len - 1;

    // Skip leading space in value
    if (value_len > 0 && value[0] == ' ') {
        value++;
        value_len--;
    }

    // Handle event field
    if (field_len == 5 && memcmp(line, "event", 5) == 0) {
        if (p->event_type) free(p->event_type);
        p->event_type = malloc(value_len + 1);
        if (p->event_type) {
            memcpy(p->event_type, value, value_len);
            p->event_type[value_len] = '\0';
        }
        return;
    }

    // Handle data field
    if (field_len == 4 && memcmp(line, "data", 4) == 0) {
        p->has_data = true;
        // Append to data buffer with newline separator
        if (p->data_len > 0) {
            // Add newline between multiple data lines
            if (p->data_len + 1 > p->data_cap) {
                size_t new_cap = p->data_cap == 0 ? 4096 : p->data_cap * 2;
                char *new_buf = realloc(p->data_buf, new_cap);
                if (!new_buf) return;
                p->data_buf = new_buf;
                p->data_cap = new_cap;
            }
            p->data_buf[p->data_len++] = '\n';
        }

        // Append value
        if (p->data_len + value_len > p->data_cap) {
            size_t new_cap = p->data_cap == 0 ? 4096 : p->data_cap * 2;
            while (new_cap < p->data_len + value_len) {
                new_cap *= 2;
            }
            char *new_buf = realloc(p->data_buf, new_cap);
            if (!new_buf) return;
            p->data_buf = new_buf;
            p->data_cap = new_cap;
        }

        memcpy(p->data_buf + p->data_len, value, value_len);
        p->data_len += value_len;
        return;
    }

    // Ignore other fields (id, retry)
}

void sse_parser_feed(SSEParser *p, const char *chunk, size_t len) {
    if (!p || !chunk) return;

    for (size_t i = 0; i < len; i++) {
        char c = chunk[i];

        // Handle line endings: \n, \r\n, \r
        if (c == '\n') {
            // Process line
            sse_parser_process_line(p, p->line_buf, p->line_len);
            p->line_len = 0;
        } else if (c == '\r') {
            // Process line
            sse_parser_process_line(p, p->line_buf, p->line_len);
            p->line_len = 0;

            // Check for \r\n (peek next char)
            if (i + 1 < len && chunk[i + 1] == '\n') {
                i++;  // Skip \n
            }
        } else {
            // Append to line buffer
            if (p->line_len + 1 > p->line_cap) {
                size_t new_cap = p->line_cap == 0 ? 1024 : p->line_cap * 2;
                char *new_buf = realloc(p->line_buf, new_cap);
                if (!new_buf) return;
                p->line_buf = new_buf;
                p->line_cap = new_cap;
            }

            p->line_buf[p->line_len++] = c;
        }
    }
}

void sse_parser_destroy(SSEParser *p) {
    if (!p) return;

    if (p->event_type) free(p->event_type);
    if (p->data_buf) free(p->data_buf);
    if (p->line_buf) free(p->line_buf);

    free(p);
}

// SSE streaming implementation

typedef struct {
    SSEParser *parser;
    volatile bool *abort_flag;
} SSEStreamContext;

static size_t sse_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    SSEStreamContext *ctx = (SSEStreamContext *)userdata;
    size_t total = size * nmemb;

    if (ctx->abort_flag && *ctx->abort_flag) {
        return 0;  // Signal abort to curl
    }

    sse_parser_feed(ctx->parser, ptr, total);

    return total;
}

static int sse_progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                                  curl_off_t ultotal, curl_off_t ulnow) {
    (void)dltotal;
    (void)dlnow;
    (void)ultotal;
    (void)ulnow;

    SSEStreamContext *ctx = (SSEStreamContext *)clientp;

    if (ctx->abort_flag && *ctx->abort_flag) {
        return 1;  // Abort transfer
    }

    return 0;  // Continue
}

int http_stream_sse(const SSERequest *req) {
    if (!req || !req->url || !req->on_event) return -1;

    SSEParser *parser = sse_parser_create(req->on_event, req->ctx);
    if (!parser) return -1;

    CURL *curl = curl_easy_init();
    if (!curl) {
        sse_parser_destroy(parser);
        return -1;
    }

    SSEStreamContext stream_ctx = {
        .parser = parser,
        .abort_flag = req->abort_flag
    };

    int result = -1;
    struct curl_slist *curl_headers = NULL;

    // Set URL
    curl_easy_setopt(curl, CURLOPT_URL, req->url);

    // POST method
    curl_easy_setopt(curl, CURLOPT_POST, 1L);

    // Set headers
    if (req->headers) {
        for (int i = 0; req->headers[i] != NULL; i++) {
            curl_headers = curl_slist_append(curl_headers, req->headers[i]);
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers);
    }

    // Set body
    if (req->body && req->body_len > 0) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req->body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)req->body_len);
    }

    // Set timeout
    if (req->timeout_ms > 0) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)req->timeout_ms);
    }

    // Set write callback
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, sse_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &stream_ctx);

    // Set progress callback for abort
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, sse_progress_callback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &stream_ctx);

    // Perform request
    CURLcode res = curl_easy_perform(curl);

    if (res == CURLE_OK || res == CURLE_ABORTED_BY_CALLBACK) {
        result = 0;
    }

    // Cleanup
    if (curl_headers) {
        curl_slist_free_all(curl_headers);
    }
    curl_easy_cleanup(curl);
    sse_parser_destroy(parser);

    return result;
}
