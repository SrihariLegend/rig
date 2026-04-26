#include "sigv4.h"
#include "util/log.h"
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdbool.h>
#include <time.h>

/* ---- Hex encoding ---- */

static void hex_encode(const unsigned char *data, size_t len, char *out) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2]     = hex[data[i] >> 4];
        out[i * 2 + 1] = hex[data[i] & 0x0f];
    }
    out[len * 2] = '\0';
}

/* ---- SHA256 hash ---- */

static void sha256_hash(const void *data, size_t len, unsigned char out[SHA256_DIGEST_LENGTH]) {
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, data, len);
    SHA256_Final(out, &ctx);
}

static void sha256_hex(const void *data, size_t len, char out[65]) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    sha256_hash(data, len, digest);
    hex_encode(digest, SHA256_DIGEST_LENGTH, out);
}

/* ---- HMAC-SHA256 ---- */

static void hmac_sha256(const void *key, size_t key_len,
                        const void *data, size_t data_len,
                        unsigned char out[SHA256_DIGEST_LENGTH]) {
    unsigned int out_len = SHA256_DIGEST_LENGTH;
    HMAC(EVP_sha256(), key, (int)key_len, data, data_len, out, &out_len);
}

/* ---- URL parsing helpers ---- */

static int parse_host_path(const char *url, char *host, size_t host_sz,
                           char *path, size_t path_sz) {
    /* Skip scheme */
    const char *p = url;
    if (strncmp(p, "https://", 8) == 0) p += 8;
    else if (strncmp(p, "http://", 7) == 0) p += 7;

    const char *slash = strchr(p, '/');
    if (slash) {
        size_t hlen = (size_t)(slash - p);
        if (hlen >= host_sz) hlen = host_sz - 1;
        memcpy(host, p, hlen);
        host[hlen] = '\0';
        snprintf(path, path_sz, "%s", slash);
    } else {
        snprintf(host, host_sz, "%s", p);
        snprintf(path, path_sz, "/");
    }
    return 0;
}

/* URI-encode a single character for canonical URI (RFC 3986) */
static bool is_unreserved(char c) {
    return isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~';
}

static void uri_encode(const char *src, char *dst, size_t dst_sz, bool encode_slash) {
    size_t pos = 0;
    for (const char *p = src; *p && pos < dst_sz - 4; p++) {
        if (is_unreserved(*p) || (*p == '/' && !encode_slash)) {
            dst[pos++] = *p;
        } else {
            pos += (size_t)snprintf(dst + pos, dst_sz - pos, "%%%02X", (unsigned char)*p);
        }
    }
    dst[pos] = '\0';
}

/* ---- Timestamp ---- */

static void get_amz_date(char date[17], char datestamp[9]) {
    time_t now = time(NULL);
    struct tm utc;
    gmtime_r(&now, &utc);
    strftime(date, 17, "%Y%m%dT%H%M%SZ", &utc);
    strftime(datestamp, 9, "%Y%m%d", &utc);
}

/* ---- SigV4 signing ---- */

int sigv4_sign_request(const SigV4Request *req, SigV4Headers *out) {
    if (!req || !out) return -1;
    memset(out, 0, sizeof(*out));

    char amz_date[17];
    char datestamp[9];
    get_amz_date(amz_date, datestamp);

    /* 1. Hash the payload */
    char payload_hash[65];
    if (req->body && req->body_len > 0) {
        sha256_hex(req->body, (size_t)req->body_len, payload_hash);
    } else {
        sha256_hex("", 0, payload_hash);
    }

    /* 2. Parse URL */
    char host[256];
    char path[512];
    parse_host_path(req->url, host, sizeof(host), path, sizeof(path));

    char canonical_uri[1024];
    uri_encode(path, canonical_uri, sizeof(canonical_uri), false);

    /* 3. Build canonical headers (host + x-amz-date + x-amz-security-token + content-type) */
    /* We sign: content-type, host, x-amz-content-sha256, x-amz-date, [x-amz-security-token] */
    char canonical_headers[2048];
    char signed_headers[256];

    /* Find content-type from original headers */
    const char *content_type = NULL;
    if (req->headers) {
        for (int i = 0; req->headers[i]; i++) {
            if (strncasecmp(req->headers[i], "Content-Type:", 13) == 0) {
                content_type = req->headers[i] + 13;
                while (*content_type == ' ') content_type++;
                break;
            }
        }
    }

    if (req->session_token) {
        snprintf(canonical_headers, sizeof(canonical_headers),
            "%s%shost:%s\nx-amz-content-sha256:%s\nx-amz-date:%s\nx-amz-security-token:%s\n",
            content_type ? "content-type:" : "",
            content_type ? content_type : "",
            host, payload_hash, amz_date, req->session_token);
        if (content_type) {
            /* Insert newline after content-type */
            char tmp[2048];
            snprintf(tmp, sizeof(tmp),
                "content-type:%s\nhost:%s\nx-amz-content-sha256:%s\nx-amz-date:%s\nx-amz-security-token:%s\n",
                content_type, host, payload_hash, amz_date, req->session_token);
            snprintf(canonical_headers, sizeof(canonical_headers), "%s", tmp);
            snprintf(signed_headers, sizeof(signed_headers),
                "content-type;host;x-amz-content-sha256;x-amz-date;x-amz-security-token");
        } else {
            snprintf(canonical_headers, sizeof(canonical_headers),
                "host:%s\nx-amz-content-sha256:%s\nx-amz-date:%s\nx-amz-security-token:%s\n",
                host, payload_hash, amz_date, req->session_token);
            snprintf(signed_headers, sizeof(signed_headers),
                "host;x-amz-content-sha256;x-amz-date;x-amz-security-token");
        }
    } else {
        if (content_type) {
            snprintf(canonical_headers, sizeof(canonical_headers),
                "content-type:%s\nhost:%s\nx-amz-content-sha256:%s\nx-amz-date:%s\n",
                content_type, host, payload_hash, amz_date);
            snprintf(signed_headers, sizeof(signed_headers),
                "content-type;host;x-amz-content-sha256;x-amz-date");
        } else {
            snprintf(canonical_headers, sizeof(canonical_headers),
                "host:%s\nx-amz-content-sha256:%s\nx-amz-date:%s\n",
                host, payload_hash, amz_date);
            snprintf(signed_headers, sizeof(signed_headers),
                "host;x-amz-content-sha256;x-amz-date");
        }
    }

    /* 4. Build canonical request */
    const char *method = req->method ? req->method : "POST";
    char canonical_request[4096];
    snprintf(canonical_request, sizeof(canonical_request),
        "%s\n%s\n\n%s\n%s\n%s",
        method, canonical_uri, canonical_headers, signed_headers, payload_hash);

    /* 5. Hash the canonical request */
    char canonical_request_hash[65];
    sha256_hex(canonical_request, strlen(canonical_request), canonical_request_hash);

    /* 6. Build string to sign */
    char credential_scope[128];
    snprintf(credential_scope, sizeof(credential_scope),
        "%s/%s/%s/aws4_request", datestamp, req->region, req->service);

    char string_to_sign[512];
    snprintf(string_to_sign, sizeof(string_to_sign),
        "AWS4-HMAC-SHA256\n%s\n%s\n%s",
        amz_date, credential_scope, canonical_request_hash);

    /* 7. Derive signing key */
    char key_buf[64];
    snprintf(key_buf, sizeof(key_buf), "AWS4%s", req->secret_key);

    unsigned char k_date[SHA256_DIGEST_LENGTH];
    hmac_sha256(key_buf, strlen(key_buf), datestamp, strlen(datestamp), k_date);

    unsigned char k_region[SHA256_DIGEST_LENGTH];
    hmac_sha256(k_date, SHA256_DIGEST_LENGTH, req->region, strlen(req->region), k_region);

    unsigned char k_service[SHA256_DIGEST_LENGTH];
    hmac_sha256(k_region, SHA256_DIGEST_LENGTH, req->service, strlen(req->service), k_service);

    unsigned char k_signing[SHA256_DIGEST_LENGTH];
    hmac_sha256(k_service, SHA256_DIGEST_LENGTH, "aws4_request", 12, k_signing);

    /* 8. Calculate signature */
    unsigned char signature_raw[SHA256_DIGEST_LENGTH];
    hmac_sha256(k_signing, SHA256_DIGEST_LENGTH,
                string_to_sign, strlen(string_to_sign), signature_raw);

    char signature_hex[65];
    hex_encode(signature_raw, SHA256_DIGEST_LENGTH, signature_hex);

    /* 9. Build Authorization header */
    char auth_value[1024];
    snprintf(auth_value, sizeof(auth_value),
        "AWS4-HMAC-SHA256 Credential=%s/%s, SignedHeaders=%s, Signature=%s",
        req->access_key, credential_scope, signed_headers, signature_hex);

    /* 10. Populate output */
    size_t auth_len = strlen("Authorization: ") + strlen(auth_value) + 1;
    out->authorization = malloc(auth_len);
    snprintf(out->authorization, auth_len, "Authorization: %s", auth_value);

    size_t date_len = strlen("X-Amz-Date: ") + strlen(amz_date) + 1;
    out->x_amz_date = malloc(date_len);
    snprintf(out->x_amz_date, date_len, "X-Amz-Date: %s", amz_date);

    size_t sha_len = strlen("X-Amz-Content-Sha256: ") + strlen(payload_hash) + 1;
    out->x_amz_content_sha256 = malloc(sha_len);
    snprintf(out->x_amz_content_sha256, sha_len, "X-Amz-Content-Sha256: %s", payload_hash);

    if (req->session_token) {
        size_t tok_len = strlen("X-Amz-Security-Token: ") + strlen(req->session_token) + 1;
        out->x_amz_security_token = malloc(tok_len);
        snprintf(out->x_amz_security_token, tok_len, "X-Amz-Security-Token: %s", req->session_token);
    }

    return 0;
}

void sigv4_headers_free(SigV4Headers *h) {
    if (!h) return;
    free(h->authorization);
    free(h->x_amz_date);
    free(h->x_amz_content_sha256);
    free(h->x_amz_security_token);
    memset(h, 0, sizeof(*h));
}

const char **sigv4_merge_headers(const char **original, const SigV4Headers *signed_hdrs, int *count) {
    int orig_count = 0;
    if (original) {
        while (original[orig_count]) orig_count++;
    }

    /* At most 4 extra headers + NULL terminator */
    int max_total = orig_count + 5;
    const char **merged = calloc((size_t)max_total, sizeof(char *));
    int n = 0;

    /* Copy original headers */
    for (int i = 0; i < orig_count; i++) {
        merged[n++] = original[i];
    }

    /* Add signed headers */
    if (signed_hdrs->authorization)       merged[n++] = signed_hdrs->authorization;
    if (signed_hdrs->x_amz_date)         merged[n++] = signed_hdrs->x_amz_date;
    if (signed_hdrs->x_amz_content_sha256) merged[n++] = signed_hdrs->x_amz_content_sha256;
    if (signed_hdrs->x_amz_security_token) merged[n++] = signed_hdrs->x_amz_security_token;

    merged[n] = NULL;
    if (count) *count = n;
    return merged;
}
