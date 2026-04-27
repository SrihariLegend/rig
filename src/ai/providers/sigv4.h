#ifndef RIG_PROVIDER_SIGV4_H
#define RIG_PROVIDER_SIGV4_H

/*
 * AWS Signature Version 4 request signing.
 *
 * Adds Authorization, X-Amz-Date, and X-Amz-Content-Sha256 headers
 * to an HTTP request for AWS service authentication.
 */

/* Maximum number of headers that sigv4_sign_request can produce */
#define SIGV4_MAX_HEADERS 8

typedef struct {
    const char *method;        /* "POST", "GET", etc. */
    const char *url;           /* Full URL including scheme */
    const char **headers;      /* NULL-terminated "Key: Value" array (input) */
    const char *body;          /* Request body (may be NULL) */
    int body_len;              /* Body length */
    const char *region;        /* AWS region, e.g. "us-east-1" */
    const char *service;       /* AWS service, e.g. "bedrock" */
    const char *access_key;    /* AWS_ACCESS_KEY_ID */
    const char *secret_key;    /* AWS_SECRET_ACCESS_KEY */
    const char *session_token; /* AWS_SESSION_TOKEN (may be NULL) */
} SigV4Request;

typedef struct {
    char *authorization;       /* "Authorization: AWS4-HMAC-SHA256 ..." (heap) */
    char *x_amz_date;         /* "X-Amz-Date: 20260101T000000Z" (heap) */
    char *x_amz_content_sha256; /* "X-Amz-Content-Sha256: <hex>" (heap) */
    char *x_amz_security_token; /* "X-Amz-Security-Token: ..." (heap, may be NULL) */
} SigV4Headers;

/*
 * Sign an AWS request using SigV4.
 * Returns 0 on success, -1 on error.
 * Caller must call sigv4_headers_free() when done.
 */
int sigv4_sign_request(const SigV4Request *req, SigV4Headers *out);

/* Free heap-allocated header strings */
void sigv4_headers_free(SigV4Headers *h);

/*
 * Build a NULL-terminated header array merging original headers with signed headers.
 * Returns heap-allocated array. Caller frees the array (not the strings — they
 * point into the original headers[] and SigV4Headers).
 */
const char **sigv4_merge_headers(const char **original, const SigV4Headers *signed_hdrs, int *count);

#endif
