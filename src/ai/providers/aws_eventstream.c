#include "aws_eventstream.h"
#include "cjson/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

static const unsigned char b64_table[256] = {
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,62,64,64,64,63,
    52,53,54,55,56,57,58,59,60,61,64,64,64,0,64,64,
    64, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
    15,16,17,18,19,20,21,22,23,24,25,64,64,64,64,64,
    64,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
    41,42,43,44,45,46,47,48,49,50,51,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
};

char *base64_decode_alloc(const char *input, size_t *out_len) {
    if (!input) return NULL;
    size_t in_len = strlen(input);
    size_t alloc = (in_len / 4) * 3 + 4;
    unsigned char *out = malloc(alloc);
    if (!out) return NULL;

    size_t j = 0;
    uint32_t accum = 0;
    int bits = 0;

    for (size_t i = 0; i < in_len; i++) {
        unsigned char c = (unsigned char)input[i];
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        unsigned char v = b64_table[c];
        if (v == 64) continue;
        accum = (accum << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (j >= alloc - 1) break;
            out[j++] = (unsigned char)(accum >> bits) & 0xFF;
        }
    }
    out[j] = '\0';
    if (out_len) *out_len = j;
    return (char *)out;
}

static uint32_t read_u32_be(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

EventStreamParser *eventstream_parser_create(EventStreamCallback cb, void *ctx) {
    EventStreamParser *p = calloc(1, sizeof(EventStreamParser));
    if (!p) return NULL;
    p->on_event = cb;
    p->ctx = ctx;
    p->buf_cap = 65536;
    p->buf = malloc(p->buf_cap);
    return p;
}

static void process_frame(EventStreamParser *p, const unsigned char *frame, size_t total_len) {
    if (total_len < 16) return;

    uint32_t headers_len = read_u32_be(frame + 4);
    size_t prelude_size = 12;
    size_t message_crc_size = 4;

    if (prelude_size + headers_len + message_crc_size > total_len) return;

    size_t payload_len = total_len - prelude_size - headers_len - message_crc_size;
    const unsigned char *payload = frame + prelude_size + headers_len;

    if (payload_len == 0 || payload_len > total_len) return;

    char *payload_str = malloc(payload_len + 1);
    if (!payload_str) return;
    memcpy(payload_str, payload, payload_len);
    payload_str[payload_len] = '\0';

    cJSON *json = cJSON_Parse(payload_str);
    free(payload_str);
    if (!json) return;

    cJSON *bytes = cJSON_GetObjectItem(json, "bytes");
    if (bytes && cJSON_IsString(bytes)) {
        size_t decoded_len = 0;
        char *decoded = base64_decode_alloc(bytes->valuestring, &decoded_len);
        if (decoded && decoded_len > 0) {
            p->on_event(decoded, p->ctx);
        }
        free(decoded);
    } else {
        /* Error frame or unexpected format — pass raw payload to handler */
        char *raw = cJSON_PrintUnformatted(json);
        if (raw) {
            char err_json[1024];
            snprintf(err_json, sizeof(err_json),
                "{\"type\":\"error\",\"error\":{\"type\":\"api_error\",\"message\":\"%.*s\"}}",
                800, raw);
            p->on_event(err_json, p->ctx);
            free(raw);
        }
    }

    cJSON_Delete(json);
}

void eventstream_parser_feed(EventStreamParser *p, const unsigned char *data, size_t len) {
    if (!p || !data || len == 0) return;

    if (p->buf_len + len > p->buf_cap) {
        while (p->buf_len + len > p->buf_cap) p->buf_cap *= 2;
        unsigned char *new_buf = realloc(p->buf, p->buf_cap);
        if (!new_buf) return;
        p->buf = new_buf;
    }
    memcpy(p->buf + p->buf_len, data, len);
    p->buf_len += len;

    while (p->buf_len >= 4) {
        uint32_t total_len = read_u32_be(p->buf);

        if (total_len < 16 || total_len > 16 * 1024 * 1024) {
            p->buf_len = 0;
            return;
        }

        if (p->buf_len < total_len) break;

        process_frame(p, p->buf, total_len);

        if (p->buf_len > total_len) {
            memmove(p->buf, p->buf + total_len, p->buf_len - total_len);
        }
        p->buf_len -= total_len;
    }
}

void eventstream_parser_free(EventStreamParser *p) {
    if (!p) return;
    free(p->buf);
    free(p);
}
