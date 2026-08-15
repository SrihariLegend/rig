/* Rig auth — OAuth engine + per-provider flows. Port of pi's auth/oauth/.
 * Two generic engines (PKCE-callback, device-code) cover 5 flows; codex and
 * copilot are custom (3-step / 2-step). */
#include "auth.h"
#include "util/http.h"
#include "util/log.h"
#include "util/str.h"
#include "cjson/cJSON.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <time.h>

#define REFRESH_TIMEOUT_MS 15000

static int64_t now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static int64_t expiry_from(double expires_in_s) {
    return now_ms() + (int64_t)(expires_in_s * 1000) - 5 * 60 * 1000;
}
static const char *jstr(cJSON *o, const char *k) {
    cJSON *v = cJSON_GetObjectItem(o, k);
    return (v && cJSON_IsString(v)) ? v->valuestring : NULL;
}
static double jnum(cJSON *o, const char *k, double def) {
    cJSON *v = cJSON_GetObjectItem(o, k);
    return (v && cJSON_IsNumber(v)) ? v->valuedouble : def;
}

/* ---- base64url + url-encode + PKCE ---- */
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static char *b64url(const unsigned char *d, size_t n) {
    size_t ol = (n + 2) / 3 * 4;
    char *o = malloc(ol + 1); if (!o) return NULL;
    size_t i, j = 0;
    for (i = 0; i + 2 < n; i += 3) {
        unsigned v = (d[i]<<16)|(d[i+1]<<8)|d[i+2];
        o[j++]=B64[v>>18&63]; o[j++]=B64[v>>12&63]; o[j++]=B64[v>>6&63]; o[j++]=B64[v&63];
    }
    if (i < n) {
        unsigned v = d[i]<<16; if (i+1<n) v|=d[i+1]<<8;
        o[j++]=B64[v>>18&63]; o[j++]=B64[v>>12&63];
        o[j++]=(i+1<n)?B64[v>>6&63]:0;
    }
    o[j]=0; return o;
}

static char *url_encode(const char *s) {
    size_t n = strlen(s); char *o = malloc(n*3+1); if (!o) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~') o[j++]=c;
        else j += sprintf(o+j, "%%%02X", c);
    }
    o[j]=0; return o;
}

static char *form_encode(const char *const *pairs) {
    Str s = str_new(256);
    for (int i = 0; pairs[i] && pairs[i+1]; i += 2) {
        char *k = url_encode(pairs[i]), *v = url_encode(pairs[i+1]);
        if (s.len) str_append_char(&s, '&');
        str_append(&s, k); str_append_char(&s, '='); str_append(&s, v);
        free(k); free(v);
    }
    return str_take(&s);
}

static void sha256(const unsigned char *d, size_t n, unsigned char out[32]) {
    EVP_MD_CTX *c = EVP_MD_CTX_new(); if (!c) return;
    EVP_DigestInit_ex(c, EVP_sha256(), NULL);
    EVP_DigestUpdate(c, d, n);
    EVP_DigestFinal_ex(c, out, NULL);
    EVP_MD_CTX_free(c);
}

static int pkce_gen(char **verifier, char **challenge) {
    unsigned char raw[32];
    if (RAND_bytes(raw, 32) != 1) return -1;
    *verifier = b64url(raw, 32);
    unsigned char dg[32];
    sha256((const unsigned char *)*verifier, strlen(*verifier), dg);
    *challenge = b64url(dg, 32);
    return (*verifier && *challenge) ? 0 : -1;
}

/* ---- HTTP POST helpers ---- */
static char *post_json(const char *url, const char *body, AuthAbort *ab, int timeout_ms) {
    const char *h[] = {"Content-Type: application/json","Accept: application/json",NULL};
    HttpRequest req = {.url=url,.method="POST",.headers=h,.body=body,.body_len=strlen(body),.timeout_ms=timeout_ms};
    HttpResponse resp = {0};
    if (auth_aborted(ab) || http_request(&req, &resp) != 0) { http_response_free(&resp); return NULL; }
    if (resp.status_code < 200 || resp.status_code >= 300) {
        LOG_ERROR("oauth POST %s HTTP %d", url, resp.status_code); http_response_free(&resp); return NULL; }
    char *d = malloc(resp.body_len+1); if (d) { memcpy(d, resp.body, resp.body_len); d[resp.body_len]=0; }
    http_response_free(&resp); return d;
}
static char *post_form(const char *url, const char *body, AuthAbort *ab, int timeout_ms) {
    const char *h[] = {"Content-Type: application/x-www-form-urlencoded","Accept: application/json",NULL};
    HttpRequest req = {.url=url,.method="POST",.headers=h,.body=body,.body_len=strlen(body),.timeout_ms=timeout_ms};
    HttpResponse resp = {0};
    if (auth_aborted(ab) || http_request(&req, &resp) != 0) { http_response_free(&resp); return NULL; }
    if (resp.status_code < 200 || resp.status_code >= 300) {
        LOG_ERROR("oauth form POST %s HTTP %d", url, resp.status_code); http_response_free(&resp); return NULL; }
    char *d = malloc(resp.body_len+1); if (d) { memcpy(d, resp.body, resp.body_len); d[resp.body_len]=0; }
    http_response_free(&resp); return d;
}

/* ---- manual input parse: redirect URL / "code=.." / raw code ---- */
static void parse_auth_input(const char *input, char **code, char **state) {
    *code = *state = NULL;
    if (!input) return;
    const char *v = input; while (*v && isspace((unsigned char)*v)) v++;
    const char *q = strchr(v, '?'); if (q) v = q + 1;
    /* manual pair split (no strtok: callback thread may use it concurrently) */
    while (*v) {
        const char *amp = strchr(v, '&');
        size_t len = amp ? (size_t)(amp - v) : strlen(v);
        const char *eq = memchr(v, '=', len);
        size_t klen = eq ? (size_t)(eq - v) : len;
        const char *val = eq ? eq + 1 : NULL;
        size_t vlen = val ? (len - klen - 1) : 0;
        if (klen == 4 && strncmp(v, "code", 4) == 0 && val) *code = strndup(val, vlen);
        else if (klen == 5 && strncmp(v, "state", 5) == 0 && val) *state = strndup(val, vlen);
        else if (!eq && !*code) *code = strndup(v, len);
        if (!amp) break;
        v = amp + 1;
    }
}

/* ---- callback server ---- */
typedef struct {
    int fd, port; char *expected_state; char *code, *state;
    volatile bool done; pthread_t thread; bool started;
} CBServer;

static const char *ok_html(void) {
    return "<!doctype html><html><head><meta charset=utf-8></head>"
           "<body style=font-family:system-ui;padding:2em><h2>Authentication complete. You can close this window.</h2></body></html>";
}
static const char *err_html(const char *m) {
    static char b[512];
    snprintf(b,sizeof b,"<!doctype html><html><head><meta charset=utf-8></head>"
        "<body style=font-family:system-ui;padding:2em><h2>%s</h2></body></html>", m);
    return b;
}

static void *cb_thread(void *arg) {
    CBServer *s = arg;
    while (!s->done) {
        struct pollfd pf = {.fd=s->fd,.events=POLLIN};
        if (poll(&pf, 1, 200) <= 0) continue;
        int cfd = accept(s->fd, NULL, NULL); if (cfd < 0) continue;
        char buf[4096] = {0}; read(cfd, buf, sizeof(buf)-1);
        char *p = strchr(buf, ' '); if (!p) { close(cfd); continue; }
        p++; char *sp = strchr(p, ' '); if (sp) *sp = 0;
        const char *q = strchr(p, '?');
        char *code = NULL, *state = NULL;
        if (q) {
            char *dup = strdup(q+1); char *t = strtok(dup, "&");
            while (t) { char *eq = strchr(t,'=');
                if (eq) { *eq=0; if (!strcmp(t,"code")) code=strdup(eq+1); else if (!strcmp(t,"state")) state=strdup(eq+1); }
                t = strtok(NULL,"&"); }
            free(dup);
        }
        const char *html;
        if (!code || !state) html = err_html("Missing code or state.");
        else if (s->expected_state && strcmp(state, s->expected_state)!=0) html = err_html("State mismatch.");
        else { html = ok_html(); s->code=code; s->state=state; s->done=true; }
        if (s->code != code) { free(code); free(state); }
        char hdr[512]; int hl = snprintf(hdr,sizeof hdr,
            "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: %zu\r\n\r\n%s",
            strlen(html), html);
        write(cfd, hdr, hl); close(cfd);
        if (s->done) break;
    }
    return NULL;
}

static CBServer *cb_start(int port, const char *state) {
    CBServer *s = calloc(1, sizeof(*s)); if (!s) return NULL;
    s->fd = socket(AF_INET, SOCK_STREAM, 0); if (s->fd < 0) { free(s); return NULL; }
    int opt = 1; setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
    struct sockaddr_in a = {.sin_family=AF_INET,.sin_port=htons(port),.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
    if (bind(s->fd,(struct sockaddr*)&a,sizeof a)<0 || listen(s->fd,1)<0) { close(s->fd); free(s); return NULL; }
    socklen_t al = sizeof a; getsockname(s->fd,(struct sockaddr*)&a,&al); s->port = ntohs(a.sin_port);
    s->expected_state = state ? strdup(state) : NULL;
    if (pthread_create(&s->thread, NULL, cb_thread, s) != 0) { close(s->fd); free(s->expected_state); free(s); return NULL; }
    s->started = true; return s;
}
static void cb_redirect(CBServer *s, char *buf, size_t sz) {
    snprintf(buf, sz, "http://127.0.0.1:%d/callback", s->port);
}
static void cb_free(CBServer *s) {
    if (!s) return;
    s->done = true;
    if (s->started) pthread_join(s->thread, NULL);
    if (s->fd >= 0) close(s->fd);
    free(s->expected_state); free(s->code); free(s->state); free(s);
}

/* ---- device-code poller ---- */
typedef enum { DEV_PENDING, DEV_SLOW, DEV_FAILED, DEV_DONE } DevSt;
typedef DevSt (*DevPoll)(void *ud, Credential *out, const char **msg);

static int device_poll(DevPoll fn, void *ud, int interval_s, int expires_s, AuthAbort *ab, Credential *out) {
    int ms = (interval_s > 0 ? interval_s : 5) * 1000; if (ms < 1000) ms = 1000;
    int64_t deadline = now_ms() + (int64_t)expires_s * 1000;
    while (now_ms() < deadline) {
        if (auth_aborted(ab)) return -1;
        const char *msg = NULL; DevSt st = fn(ud, out, &msg);
        if (st == DEV_DONE) return 0;
        if (st == DEV_FAILED) { LOG_ERROR("device code: %s", msg?msg:"?"); return -1; }
        if (st == DEV_SLOW) ms += 5000;
        int64_t end = now_ms() + ms;
        while (now_ms() < end) { if (auth_aborted(ab)) return -1; usleep(100000); }
    }
    return -1;
}

/* ---- to_auth: apiKey = access ---- */
static int to_auth_apikey(const Credential *in, AuthResult *out) {
    if (!in->access) return -1;
    out->api_key = strdup(in->access);
    return 0;
}

/* ============================================================ generic PKCE-callback */
typedef struct {
    const char *client_id, *authorize_url, *token_url, *scopes;
    int port; const char *response_field; bool permanent, minimal, form;
} PkceCfg;

static int pkce_login(const PkceCfg *c, AuthInteraction *ia, Credential *out) {
    char *verifier = NULL, *challenge = NULL;
    if (pkce_gen(&verifier, &challenge) != 0) return -1;
    CBServer *s = cb_start(c->port, verifier); if (!s) { free(verifier); free(challenge); return -1; }
    char redirect[128]; cb_redirect(s, redirect, sizeof redirect);

    Str url = str_new(512); str_appendf(&url, "%s?", c->authorize_url);
    if (c->minimal) {
        char *cb = url_encode(redirect);
        str_appendf(&url, "callback_url=%s&code_challenge=%s&code_challenge_method=S256", cb, challenge); free(cb);
    } else {
        str_appendf(&url, "response_type=code&client_id=%s&redirect_uri=%s&code_challenge=%s&code_challenge_method=S256&state=%s",
            c->client_id, redirect, challenge, verifier);
        if (c->scopes) { str_append(&url, "&scope="); char *sc = url_encode(c->scopes); str_append(&url, sc); free(sc); }
    }
    AuthEvent eu = {.type=EVENT_AUTH_URL,.url=url.data,
        .instructions="Complete login in your browser. If remote, paste the final redirect URL when prompted."};
    ia->notify(ia, &eu); str_free(&url);

    /* wait for callback up to 5 min, else prompt manual */
    char *rcode = NULL, *rstate = NULL;
    int64_t dl = now_ms() + 300000;
    while (!s->done && now_ms() < dl) {
        if (auth_aborted(ia->abort)) { cb_free(s); free(verifier); free(challenge); return -1; }
        usleep(100000);
    }
    if (s->done) { rcode = strdup(s->code); rstate = s->state ? strdup(s->state) : NULL; }
    else {
        ia->notify(ia, &(AuthEvent){.type=EVENT_PROGRESS,.message="No callback yet. Paste the redirect URL manually."});
        AuthPrompt mp = {.type=PROMPT_MANUAL_CODE,.message="Paste redirect URL or code: "};
        char *man = ia->prompt(ia, &mp);
        if (man) { parse_auth_input(man, &rcode, &rstate); free(man); }
    }
    cb_free(s);
    if (!rcode) { free(verifier); free(challenge); free(rcode); free(rstate); return -1; }

    ia->notify(ia, &(AuthEvent){.type=EVENT_PROGRESS,.message="Exchanging authorization code..."});
    char *body;
    if (c->minimal) {
        if (c->form) { const char *p[]={"code",rcode,"code_verifier",verifier,"code_challenge_method","S256",NULL}; body=form_encode(p); }
        else { cJSON *o=cJSON_CreateObject(); cJSON_AddStringToObject(o,"code",rcode);
            cJSON_AddStringToObject(o,"code_verifier",verifier); cJSON_AddStringToObject(o,"code_challenge_method","S256");
            body=cJSON_PrintUnformatted(o); cJSON_Delete(o); }
    } else {
        if (c->form) { const char *p[]={"grant_type","authorization_code","client_id",c->client_id,
            "code",rcode,"state",rstate?rstate:verifier,"redirect_uri",redirect,"code_verifier",verifier,NULL}; body=form_encode(p); }
        else { cJSON *o=cJSON_CreateObject(); cJSON_AddStringToObject(o,"grant_type","authorization_code");
            cJSON_AddStringToObject(o,"client_id",c->client_id); cJSON_AddStringToObject(o,"code",rcode);
            cJSON_AddStringToObject(o,"state",rstate?rstate:verifier); cJSON_AddStringToObject(o,"redirect_uri",redirect);
            cJSON_AddStringToObject(o,"code_verifier",verifier); body=cJSON_PrintUnformatted(o); cJSON_Delete(o); }
    }
    char *resp = c->form ? post_form(c->token_url, body, ia->abort, 30000) : post_json(c->token_url, body, ia->abort, 30000);
    free(body); free(rcode); free(rstate); free(verifier); free(challenge);
    if (!resp) return -1;
    cJSON *j = cJSON_Parse(resp); free(resp); if (!j) return -1;
    const char *access = jstr(j, c->response_field ? c->response_field : "access_token");
    if (!access) { cJSON_Delete(j); return -1; }
    out->type = CRED_OAUTH; out->access = strdup(access);
    if (c->permanent) { out->refresh = strdup(""); out->expires_ms = INT64_MAX; }
    else { out->refresh = strdup(jstr(j,"refresh_token") ? jstr(j,"refresh_token") : "");
           out->expires_ms = expiry_from(jnum(j,"expires_in",3600)); }
    cJSON_Delete(j); return 0;
}

static int std_refresh(const PkceCfg *c, const Credential *in, Credential *out, AuthAbort *ab) {
    if ((!in->refresh || !*in->refresh) && c->permanent) {
        out->type=CRED_OAUTH; out->access=strdup(in->access?in->access:""); out->refresh=strdup(""); out->expires_ms=INT64_MAX; return 0;
    }
    if (!in->refresh || !*in->refresh) return -1;
    char *body;
    if (c->form) { const char *p[]={"grant_type","refresh_token","client_id",c->client_id,"refresh_token",in->refresh,NULL}; body=form_encode(p); }
    else { cJSON *o=cJSON_CreateObject(); cJSON_AddStringToObject(o,"grant_type","refresh_token");
        cJSON_AddStringToObject(o,"client_id",c->client_id); cJSON_AddStringToObject(o,"refresh_token",in->refresh);
        body=cJSON_PrintUnformatted(o); cJSON_Delete(o); }
    char *resp = c->form ? post_form(c->token_url, body, ab, REFRESH_TIMEOUT_MS) : post_json(c->token_url, body, ab, REFRESH_TIMEOUT_MS);
    free(body); if (!resp) return -1;
    cJSON *j = cJSON_Parse(resp); free(resp); if (!j) return -1;
    const char *access = jstr(j,"access_token"); if (!access) { cJSON_Delete(j); return -1; }
    out->type=CRED_OAUTH; out->access=strdup(access);
    out->refresh=strdup(jstr(j,"refresh_token") ? jstr(j,"refresh_token") : in->refresh ? in->refresh : "");
    out->expires_ms = expiry_from(jnum(j,"expires_in",3600));
    cJSON_Delete(j); return 0;
}

/* ============================================================ generic device-code */
typedef struct {
    const char *client_id, *device_url, *token_url, *scope;
    const char *const *extra; bool form;
} DevCfg;

typedef struct { const DevCfg *c; const char *device_code; AuthAbort *ab; } DevState;

static char *dev_request(const DevCfg *c, AuthAbort *ab, cJSON **outj) {
    char *body;
    if (c->form) {
        Str s = str_new(256); char *cid = url_encode(c->client_id);
        str_appendf(&s, "client_id=%s", cid); free(cid);
        if (c->scope) { char *sc = url_encode(c->scope); str_appendf(&s, "&scope=%s", sc); free(sc); }
        if (c->extra) for (int i = 0; c->extra[i] && c->extra[i+1]; i += 2) {
            char *k = url_encode(c->extra[i]), *v = url_encode(c->extra[i+1]);
            str_appendf(&s, "&%s=%s", k, v); free(k); free(v); }
        body = str_take(&s);
    } else {
        cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o,"client_id",c->client_id);
        if (c->scope) cJSON_AddStringToObject(o,"scope",c->scope);
        body = cJSON_PrintUnformatted(o); cJSON_Delete(o);
    }
    char *resp = c->form ? post_form(c->device_url, body, ab, 30000) : post_json(c->device_url, body, ab, 30000);
    free(body); if (!resp) return NULL;
    cJSON *j = cJSON_Parse(resp); free(resp); if (!j) return NULL;
    const char *dc = jstr(j,"device_code"); if (!dc) { cJSON_Delete(j); return NULL; }
    char *out = strdup(dc);
    if (outj) *outj = j; else cJSON_Delete(j);
    return out;
}

static DevSt dev_poll_fn(void *ud, Credential *out, const char **msg) {
    DevState *st = ud; const DevCfg *c = st->c;
    const char *grant = "urn:ietf:params:oauth:grant-type:device_code";
    char *body;
    if (c->form) { const char *p[]={"grant_type",grant,"client_id",c->client_id,"device_code",st->device_code,NULL}; body=form_encode(p); }
    else { cJSON *o=cJSON_CreateObject(); cJSON_AddStringToObject(o,"grant_type",grant);
        cJSON_AddStringToObject(o,"client_id",c->client_id); cJSON_AddStringToObject(o,"device_code",st->device_code);
        body=cJSON_PrintUnformatted(o); cJSON_Delete(o); }
    char *resp = c->form ? post_form(c->token_url, body, st->ab, 30000) : post_json(c->token_url, body, st->ab, 30000);
    free(body); if (!resp) { *msg="token request failed"; return DEV_FAILED; }
    cJSON *j = cJSON_Parse(resp); free(resp); if (!j) { *msg="invalid json"; return DEV_FAILED; }
    const char *access = jstr(j,"access_token");
    if (access) {
        out->type=CRED_OAUTH; out->access=strdup(access);
        out->refresh=strdup(jstr(j,"refresh_token")?jstr(j,"refresh_token"):"");
        out->expires_ms=expiry_from(jnum(j,"expires_in",3600));
        cJSON_Delete(j); return DEV_DONE;
    }
    const char *err = jstr(j,"error"); cJSON_Delete(j);
    if (!err) { *msg="invalid token response"; return DEV_FAILED; }
    if (!strcmp(err,"authorization_pending")) return DEV_PENDING;
    if (!strcmp(err,"slow_down")) return DEV_SLOW;
    *msg = err; return DEV_FAILED;
}

static int dev_login(const DevCfg *c, AuthInteraction *ia, Credential *out) {
    cJSON *dj = NULL; char *dc = dev_request(c, ia->abort, &dj); if (!dc) return -1;
    const char *uc = jstr(dj,"user_code");
    const char *veri = jstr(dj,"verification_uri_complete"); if (!veri) veri = jstr(dj,"verification_uri");
    int interval = (int)jnum(dj,"interval",5), expires = (int)jnum(dj,"expires_in",900);
    AuthEvent e = {.type=EVENT_DEVICE_CODE,.user_code=uc,.verification_uri=veri,
        .interval_seconds=interval,.expires_in_seconds=expires};
    ia->notify(ia, &e);
    DevState st = {c, dc, ia->abort};
    int rc = device_poll(dev_poll_fn, &st, interval, expires, ia->abort, out);
    free(dc); cJSON_Delete(dj); return rc;
}

static int dev_refresh(const DevCfg *c, const Credential *in, Credential *out, AuthAbort *ab) {
    char *body;
    if (c->form) { const char *p[]={"grant_type","refresh_token","client_id",c->client_id,"refresh_token",in->refresh,NULL}; body=form_encode(p); }
    else { cJSON *o=cJSON_CreateObject(); cJSON_AddStringToObject(o,"grant_type","refresh_token");
        cJSON_AddStringToObject(o,"client_id",c->client_id); cJSON_AddStringToObject(o,"refresh_token",in->refresh);
        body=cJSON_PrintUnformatted(o); cJSON_Delete(o); }
    char *resp = c->form ? post_form(c->token_url, body, ab, REFRESH_TIMEOUT_MS) : post_json(c->token_url, body, ab, REFRESH_TIMEOUT_MS);
    free(body); if (!resp) return -1;
    cJSON *j = cJSON_Parse(resp); free(resp); if (!j) return -1;
    const char *access = jstr(j,"access_token"); if (!access) { cJSON_Delete(j); return -1; }
    out->type=CRED_OAUTH; out->access=strdup(access);
    out->refresh=strdup(jstr(j,"refresh_token")?jstr(j,"refresh_token"):in->refresh?in->refresh:"");
    out->expires_ms=expiry_from(jnum(j,"expires_in",3600));
    cJSON_Delete(j); return 0;
}

/* ============================================================ flows */

/* Anthropic — PKCE callback, JSON, standard. */
static const PkceCfg ANTHROPIC_PKCE = {
    .client_id="9d1c250a-e61b-44d9-88ed-5944d1962f5e",
    .authorize_url="https://claude.ai/oauth/authorize",
    .token_url="https://platform.claude.com/v1/oauth/token",
    .scopes="org:create_api_key user:profile user:inference user:sessions:claude_code user:mcp_servers user:file_upload",
    .port=53692, .response_field="access_token", .permanent=false, .minimal=false, .form=false,
};
static int anth_login(AuthInteraction *ia, Credential *o){return pkce_login(&ANTHROPIC_PKCE,ia,o);}
static int anth_refresh(const Credential *in, Credential *o, AuthAbort *ab){return std_refresh(&ANTHROPIC_PKCE,in,o,ab);}
const OAuthFlow anthropic_oauth = {"Anthropic (Claude Pro/Max)",true,NULL,anth_login,anth_refresh,to_auth_apikey};

/* OpenRouter — PKCE callback, minimal, permanent key. */
static const PkceCfg OPENROUTER_PKCE = {
    .client_id=NULL,.authorize_url="https://openrouter.ai/auth",
    .token_url="https://openrouter.ai/api/v1/auth/keys",.scopes=NULL,
    .port=0,.response_field="key",.permanent=true,.minimal=true,.form=false,
};
static int or_login(AuthInteraction *ia, Credential *o){return pkce_login(&OPENROUTER_PKCE,ia,o);}
static int or_refresh(const Credential *in, Credential *o, AuthAbort *ab){
    (void)ab; o->type=CRED_OAUTH; o->access=strdup(in->access?in->access:""); o->refresh=strdup(""); o->expires_ms=INT64_MAX; return 0;
}
const OAuthFlow openrouter_oauth = {"OpenRouter",false,"Sign in with OpenRouter",or_login,or_refresh,to_auth_apikey};

/* xAI — device code, form. */
static const char *XAI_EXTRA[]={"referrer","pi",NULL};
static const DevCfg XAI_DEV={
    .client_id="b1a00492-073a-47ea-816f-4c329264a828",
    .device_url="https://auth.x.ai/oauth2/device/code",.token_url="https://auth.x.ai/oauth2/token",
    .scope="openid profile email offline_access grok-cli:access api:access",.extra=XAI_EXTRA,.form=true,
};
static int xai_login(AuthInteraction *ia, Credential *o){return dev_login(&XAI_DEV,ia,o);}
static int xai_refresh(const Credential *in, Credential *o, AuthAbort *ab){return dev_refresh(&XAI_DEV,in,o,ab);}
const OAuthFlow xai_oauth = {"xAI (Grok/X subscription)",true,"Sign in with SuperGrok or X Premium",xai_login,xai_refresh,to_auth_apikey};

/* Kimi Coding — device code, form. */
static const DevCfg KIMI_DEV={
    .client_id="17e5f671-d194-4dfb-9706-5516cb48c098",
    .device_url="https://auth.kimi.com/api/oauth/device_authorization",
    .token_url="https://auth.kimi.com/api/oauth/token",.scope=NULL,.extra=NULL,.form=true,
};
static int kimi_login(AuthInteraction *ia, Credential *o){return dev_login(&KIMI_DEV,ia,o);}
static int kimi_refresh(const Credential *in, Credential *o, AuthAbort *ab){return dev_refresh(&KIMI_DEV,in,o,ab);}
const OAuthFlow kimi_coding_oauth = {"Kimi Coding",true,NULL,kimi_login,kimi_refresh,to_auth_apikey};

/* Radius — device code, form. */
static const DevCfg RADIUS_DEV={
    .client_id="pi-gateway",.device_url="https://api.radius.ai/v1/oauth/device",
    .token_url="https://api.radius.ai/v1/oauth/token",.scope="gateway offline_access",.extra=NULL,.form=true,
};
static int rad_login(AuthInteraction *ia, Credential *o){return dev_login(&RADIUS_DEV,ia,o);}
static int rad_refresh(const Credential *in, Credential *o, AuthAbort *ab){return dev_refresh(&RADIUS_DEV,in,o,ab);}
const OAuthFlow radius_oauth = {"Radius",false,NULL,rad_login,rad_refresh,to_auth_apikey};

/* OpenAI Codex — 3-step device -> auth code -> PKCE exchange. */
#define CODEX_CID "app_EMoamEEZ73f0CkXaXp7hrann"
#define CODEX_BASE "https://auth.openai.com"
static int codex_login(AuthInteraction *ia, Credential *out) {
    cJSON *o=cJSON_CreateObject(); cJSON_AddStringToObject(o,"client_id",CODEX_CID);
    char *b=cJSON_PrintUnformatted(o); cJSON_Delete(o);
    char *r=post_json(CODEX_BASE "/api/accounts/deviceauth/usercode", b, ia->abort, 30000); free(b);
    if (!r) return -1;
    cJSON *j=cJSON_Parse(r); free(r); if (!j) return -1;
    const char *uc=jstr(j,"user_code"), *daid=jstr(j,"device_auth_id");
    int interval=(int)jnum(j,"interval",5);
    if (!uc||!daid) { cJSON_Delete(j); return -1; }
    char *ucs=strdup(uc), *ds=strdup(daid); cJSON_Delete(j);
    AuthEvent e={.type=EVENT_DEVICE_CODE,.user_code=ucs,.verification_uri=CODEX_BASE "/codex/device",
        .interval_seconds=interval,.expires_in_seconds=900};
    ia->notify(ia,&e);
    char *acode=NULL,*ver=NULL; int rc=-1;
    int64_t dl=now_ms()+900000;
    while (now_ms()<dl) {
        if (auth_aborted(ia->abort)) break;
        cJSON *po=cJSON_CreateObject(); cJSON_AddStringToObject(po,"device_auth_id",ds); cJSON_AddStringToObject(po,"user_code",ucs);
        char *pb=cJSON_PrintUnformatted(po); cJSON_Delete(po);
        char *pr=post_json(CODEX_BASE "/api/accounts/deviceauth/token", pb, ia->abort, 30000); free(pb);
        if (!pr) { usleep((useconds_t)(interval*1000000)); continue; }
        cJSON *pj=cJSON_Parse(pr); free(pr); if (!pj) { usleep((useconds_t)(interval*1000000)); continue; }
        const char *a=jstr(pj,"authorization_code"), *v=jstr(pj,"code_verifier");
        if (a&&v) { acode=strdup(a); ver=strdup(v); rc=0; cJSON_Delete(pj); break; }
        cJSON_Delete(pj); usleep((useconds_t)(interval*1000000));
    }
    free(ucs); free(ds);
    if (rc) return -1;
    const char *p[]={"grant_type","authorization_code","client_id",CODEX_CID,"code",acode,
        "code_verifier",ver,"redirect_uri","http://localhost:1455/auth/callback",NULL};
    char *eb=form_encode(p);
    char *er=post_form(CODEX_BASE "/oauth/token", eb, ia->abort, 30000); free(eb); free(acode); free(ver);
    if (!er) return -1;
    cJSON *ej=cJSON_Parse(er); free(er); if (!ej) return -1;
    const char *access=jstr(ej,"access_token"); if (!access) { cJSON_Delete(ej); return -1; }
    out->type=CRED_OAUTH; out->access=strdup(access);
    out->refresh=strdup(jstr(ej,"refresh_token")?jstr(ej,"refresh_token"):"");
    out->expires_ms=expiry_from(jnum(ej,"expires_in",3600));
    cJSON_Delete(ej); return 0;
}
static int codex_refresh(const Credential *in, Credential *out, AuthAbort *ab) {
    const char *p[]={"grant_type","refresh_token","client_id",CODEX_CID,"refresh_token",in->refresh,NULL};
    char *b=form_encode(p); char *r=post_form(CODEX_BASE "/oauth/token", b, ab, REFRESH_TIMEOUT_MS); free(b);
    if (!r) return -1;
    cJSON *j=cJSON_Parse(r); free(r); if (!j) return -1;
    const char *access=jstr(j,"access_token"); if (!access) { cJSON_Delete(j); return -1; }
    out->type=CRED_OAUTH; out->access=strdup(access);
    out->refresh=strdup(jstr(j,"refresh_token")?jstr(j,"refresh_token"):in->refresh?in->refresh:"");
    out->expires_ms=expiry_from(jnum(j,"expires_in",3600));
    cJSON_Delete(j); return 0;
}
const OAuthFlow openai_codex_oauth = {"OpenAI Codex (ChatGPT subscription)",true,NULL,codex_login,codex_refresh,to_auth_apikey};

/* GitHub Copilot — device code -> GH token -> copilot token (2-step). */
#define COPILOT_CID "Iv1.b507a08c87ecfe98"
#define COPILOT_APIVER "2026-06-01"
static const char *COPILOT_HDRS[]={
    "Accept: application/json","User-Agent: GitHubCopilotChat/0.35.0",
    "Editor-Version: vscode/1.107.0","Editor-Plugin-Version: copilot-chat/0.35.0",
    "Copilot-Integration-Id: vscode-chat",NULL,
};
static char *copilot_request(const char *url, const char *body, const char *ct, const char *bearer, AuthAbort *ab, int timeout_ms) {
    const char *h[10]; int n=0;
    for (int i=0; COPILOT_HDRS[i]; i++) h[n++]=COPILOT_HDRS[i];
    char cth[80]; if (body) { snprintf(cth,sizeof cth,"Content-Type: %s",ct); h[n++]=cth; }
    char ah[300]; if (bearer) { snprintf(ah,sizeof ah,"Authorization: Bearer %s",bearer); h[n++]=ah; }
    char xh[80]; snprintf(xh,sizeof xh,"X-GitHub-Api-Version: %s",COPILOT_APIVER); h[n++]=xh;
    h[n]=NULL;
    HttpRequest req={.url=url,.method=body?"POST":"GET",.headers=h,.body=body,.body_len=body?strlen(body):0,.timeout_ms=timeout_ms};
    HttpResponse resp={0};
    if (auth_aborted(ab)||http_request(&req,&resp)!=0) { http_response_free(&resp); return NULL; }
    if (resp.status_code<200||resp.status_code>=300) { http_response_free(&resp); return NULL; }
    char *d=malloc(resp.body_len+1); if (d) { memcpy(d,resp.body,resp.body_len); d[resp.body_len]=0; }
    http_response_free(&resp); return d;
}
static int copilot_exchange(const char *gh_token, Credential *out, AuthAbort *ab) {
    char *r=copilot_request("https://api.github.com/copilot_internal/v2/token", NULL, "", gh_token, ab, 30000);
    if (!r) return -1;
    cJSON *j=cJSON_Parse(r); free(r); if (!j) return -1;
    const char *token=jstr(j,"token"); double exp=jnum(j,"expires_at",0);
    if (!token||!exp) { cJSON_Delete(j); return -1; }
    out->type=CRED_OAUTH; out->access=strdup(token); out->refresh=strdup(gh_token);
    out->expires_ms=(int64_t)(exp*1000)-60000;
    cJSON_Delete(j); return 0;
}
static int copilot_login(AuthInteraction *ia, Credential *out) {
    const char *p[]={"client_id",COPILOT_CID,"scope","read:user",NULL};
    char *b=form_encode(p);
    char *r=copilot_request("https://github.com/login/device/code", b, "application/x-www-form-urlencoded", NULL, ia->abort, 30000);
    free(b); if (!r) return -1;
    cJSON *j=cJSON_Parse(r); free(r); if (!j) return -1;
    const char *dc=jstr(j,"device_code"),*uc=jstr(j,"user_code"),*veri=jstr(j,"verification_uri");
    int interval=(int)jnum(j,"interval",5), expires=(int)jnum(j,"expires_in",900);
    if (!dc||!uc||!veri) { cJSON_Delete(j); return -1; }
    char *dcs=strdup(dc),*ucs=strdup(uc); cJSON_Delete(j);
    AuthEvent e={.type=EVENT_DEVICE_CODE,.user_code=ucs,.verification_uri=veri,.interval_seconds=interval,.expires_in_seconds=expires};
    ia->notify(ia,&e);
    const char *pp[]={"client_id",COPILOT_CID,"device_code",dcs,"grant_type","urn:ietf:params:oauth:grant-type:device_code",NULL};
    char *pb=form_encode(pp);
    char *gh_token=NULL; int64_t dl=now_ms()+expires*1000;
    while (now_ms()<dl) {
        if (auth_aborted(ia->abort)) break;
        char *pr=copilot_request("https://github.com/login/oauth/access_token", pb, "application/x-www-form-urlencoded", NULL, ia->abort, 30000);
        if (!pr) { usleep((useconds_t)(interval*1000000)); continue; }
        cJSON *pj=cJSON_Parse(pr); free(pr); if (!pj) { usleep((useconds_t)(interval*1000000)); continue; }
        const char *at=jstr(pj,"access_token"),*err=jstr(pj,"error");
        if (at) { gh_token=strdup(at); cJSON_Delete(pj); break; }
        if (err&&!strcmp(err,"slow_down")) interval+=5;
        cJSON_Delete(pj);
        if (err&&strcmp(err,"authorization_pending")) break;
        usleep((useconds_t)(interval*1000000));
    }
    free(pb); free(dcs); free(ucs);
    if (!gh_token) return -1;
    int rc=copilot_exchange(gh_token, out, ia->abort); free(gh_token); return rc;
}
static int copilot_refresh(const Credential *in, Credential *out, AuthAbort *ab) {
    return copilot_exchange(in->refresh ? in->refresh : "", out, ab);
}
static int copilot_to_auth(const Credential *in, AuthResult *out) {
    if (!in->access) return -1;
    out->api_key=strdup(in->access); out->source=strdup("GitHub Copilot");
    return 0;
}
const OAuthFlow github_copilot_oauth = {"GitHub Copilot",true,"Sign in with GitHub Copilot",copilot_login,copilot_refresh,copilot_to_auth};
