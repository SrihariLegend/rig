#ifndef PI_AWS_EVENTSTREAM_H
#define PI_AWS_EVENTSTREAM_H

#include <stddef.h>

typedef void (*EventStreamCallback)(const char *json_event, void *ctx);

typedef struct {
    EventStreamCallback on_event;
    void *ctx;
    unsigned char *buf;
    size_t buf_len;
    size_t buf_cap;
} EventStreamParser;

EventStreamParser *eventstream_parser_create(EventStreamCallback cb, void *ctx);
void eventstream_parser_feed(EventStreamParser *p, const unsigned char *data, size_t len);
void eventstream_parser_free(EventStreamParser *p);

char *base64_decode_alloc(const char *input, size_t *out_len);

#endif
