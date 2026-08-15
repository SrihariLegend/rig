/* Rig auth — port of pi's auth architecture. Core: store, resolve, registry, CLI. */
#include "auth.h"
#include "config.h"
#include "util/fs.h"
#include "util/json.h"
#include "util/log.h"
#include "cjson/cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>
#include "ai/models.h"

/* ============================================================ credentials */

void credential_free(Credential *c) {
    if (!c) return;
    free(c->key);
    for (int i = 0; i < c->env_count; i++) { free(c->env[i].name); free(c->env[i].value); }
    free(c->env);
    free(c->refresh); free(c->access); free(c->extras);
    free(c);
}

static Credential *credential_new(CredentialType t) {
    Credential *c = calloc(1, sizeof(*c));
    if (c) c->type = t;
    return c;
}

Credential *credential_clone(const Credential *s) {
    if (!s) return NULL;
    Credential *c = credential_new(s->type);
    if (!c) return NULL;
    c->key = s->key ? strdup(s->key) : NULL;
    if (s->env_count > 0) {
        c->env = calloc(s->env_count, sizeof(EnvEntry));
        for (int i = 0; i < s->env_count; i++) {
            c->env[i].name = strdup(s->env[i].name);
            c->env[i].value = strdup(s->env[i].value);
            c->env_count++;
        }
    }
    c->refresh = s->refresh ? strdup(s->refresh) : NULL;
    c->access = s->access ? strdup(s->access) : NULL;
    c->expires_ms = s->expires_ms;
    c->extras = s->extras ? strdup(s->extras) : NULL;
    return c;
}

void credential_info_free(CredentialInfo *ci, int count) {
    if (!ci) return;
    for (int i = 0; i < count; i++) free(ci[i].provider_id);
    free(ci);
}

void auth_result_free(AuthResult *r) {
    if (!r) return;
    free(r->api_key); free(r->auth_header); free(r->base_url); free(r->source);
    free(r);
}

/* ============================================================ context/abort */

static const char *ctx_env_default(const char *name, void *ctx) {
    (void)ctx;
    const char *v = getenv(name);
    return (v && *v) ? v : NULL;
}
static bool ctx_file_exists_default(const char *path, void *ctx) {
    (void)ctx;
    char buf[4096];
    if (path[0] == '~') { snprintf(buf, sizeof buf, "%s%s", fs_homedir(), path+1); path = buf; }
    return fs_exists(path);
}
AuthContext auth_default_context(void) {
    AuthContext c = { ctx_env_default, ctx_file_exists_default, NULL };
    return c;
}
void auth_abort_init(AuthAbort *a) { a->flag = false; }
void auth_abort_cancel(AuthAbort *a) { a->flag = true; }
bool auth_aborted(const AuthAbort *a) { return a->flag; }

/* ============================================================ CLI interaction */

static char *read_line(const char *prompt, bool hidden) {
    fprintf(stderr, "%s", prompt); fflush(stderr);
    struct termios old, neu; bool tty = isatty(STDIN_FILENO);
    if (hidden && tty) {
        tcgetattr(STDIN_FILENO, &old); neu = old; neu.c_lflag &= ~(tcflag_t)ECHO;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &neu);
    }
    char buf[2048] = {0}; char *r = fgets(buf, sizeof buf, stdin);
    if (hidden && tty) { tcsetattr(STDIN_FILENO, TCSAFLUSH, &old); fprintf(stderr, "\n"); }
    if (!r) return NULL;
    size_t n = strlen(buf);
    if (n && buf[n-1] == '\n') buf[n-1] = '\0';
    if (!buf[0]) return NULL;
    return strdup(buf);
}

static char *cli_prompt(struct AuthInteraction *self, const AuthPrompt *p) {
    (void)self;
    if (auth_aborted(self->abort)) return NULL;
    if (p->type == PROMPT_SELECT) {
        fprintf(stderr, "%s\n", p->message);
        for (int i = 0; i < p->option_count; i++)
            fprintf(stderr, "  %d) %s\n", i+1, p->option_labels[i]);
        char buf[32]; fprintf(stderr, "Choice (1-%d): ", p->option_count); fflush(stderr);
        if (!fgets(buf, sizeof buf, stdin)) return NULL;
        int n = atoi(buf);
        if (n < 1 || n > p->option_count) return NULL;
        return strdup(p->option_ids[n-1]);
    }
    return read_line(p->message, p->type == PROMPT_SECRET || p->type == PROMPT_MANUAL_CODE);
}

static void cli_notify(struct AuthInteraction *self, const AuthEvent *e) {
    (void)self;
    switch (e->type) {
    case EVENT_AUTH_URL:
        fprintf(stderr, "\nOpen this URL in your browser:\n  %s\n", e->url);
        if (e->instructions) fprintf(stderr, "%s\n", e->instructions);
        break;
    case EVENT_DEVICE_CODE:
        fprintf(stderr, "\nDevice code: %s\n", e->user_code);
        fprintf(stderr, "Visit: %s\n", e->verification_uri);
        break;
    case EVENT_PROGRESS: fprintf(stderr, "%s\n", e->message); break;
    case EVENT_INFO: fprintf(stderr, "%s\n", e->message); break;
    }
}

AuthInteraction auth_cli_interaction(AuthAbort *abort) {
    AuthInteraction ia = { abort, cli_prompt, cli_notify, NULL };
    return ia;
}

/* ============================================================ registry */

extern const OAuthFlow anthropic_oauth, openrouter_oauth, xai_oauth,
    openai_codex_oauth, github_copilot_oauth, kimi_coding_oauth, radius_oauth;

/* anthropic: ANTHROPIC_AUTH_TOKEN participates in discovery but resolves as Bearer */
static int anthropic_resolve(const Credential *cred, AuthResult *out, AuthContext ctx, AuthAbort *ab) {
    if (cred && cred->key) { out->api_key = strdup(cred->key); out->source = strdup("stored credential"); return 0; }
    const char *t = ctx.env("ANTHROPIC_AUTH_TOKEN", ctx.ctx);
    if (auth_aborted(ab)) return -1;
    if (t) { out->auth_header = strdup(t); out->source = strdup("ANTHROPIC_AUTH_TOKEN"); return 0; }
    for (const char *const *v = (const char *const[]){"ANTHROPIC_OAUTH_TOKEN","ANTHROPIC_API_KEY",NULL}; *v; v++) {
        const char *k = ctx.env(*v, ctx.ctx);
        if (auth_aborted(ab)) return -1;
        if (k) { out->api_key = strdup(k); out->source = strdup(*v); return 0; }
    }
    return 1;
}

/* bedrock: stored aws keys → setenv so sigv4 provider picks them up; ambient env discovery */
static int bedrock_resolve(const Credential *cred, AuthResult *out, AuthContext ctx, AuthAbort *ab) {
    if (cred) {
        for (int i = 0; i < cred->env_count; i++)
            setenv(cred->env[i].name, cred->env[i].value, 0);
        if (cred->key) {  /* bearer token */
            setenv("AWS_BEARER_TOKEN_BEDROCK", cred->key, 0);
            out->api_key = strdup(cred->key); out->source = strdup("stored credential");
            return 0;
        }
        if (cred->env_count > 0) {  /* IAM credentials already setenv'd above */
            out->api_key = strdup("<authenticated>"); out->source = strdup("stored IAM");
            return 0;
        }
    }
    /* ambient: report configured if any AWS source is present */
    const char *sources[] = {"AWS_PROFILE","AWS_ACCESS_KEY_ID","AWS_BEARER_TOKEN_BEDROCK",
        "AWS_CONTAINER_CREDENTIALS_RELATIVE_URI","AWS_WEB_IDENTITY_TOKEN_FILE", NULL};
    for (const char **s = sources; *s; s++) {
        const char *v = ctx.env(*s, ctx.ctx);
        if (auth_aborted(ab)) return -1;
        if (v) {
            if (strcmp(*s, "AWS_ACCESS_KEY_ID") == 0 && !ctx.env("AWS_SECRET_ACCESS_KEY", ctx.ctx)) continue;
            out->api_key = strdup("<authenticated>"); out->source = strdup(*s);
            return 0;
        }
    }
    return 1;
}

static int bedrock_login(AuthInteraction *ia, Credential *out) {
    /* two sub-methods: bearer API key, or IAM access/secret + region */
    AuthPrompt m = { .type=PROMPT_SELECT, .message="Bedrock auth method",
        .option_ids=(const char*[]){"bearer","iam",NULL},
        .option_labels=(const char*[]){"API Key (bearer token)","IAM Credentials (Access Key + Secret)",NULL},
        .option_count=2 };
    char *choice = ia->prompt(ia, &m);
    if (!choice) return -1;
    int bearer = strcmp(choice, "bearer") == 0; free(choice);
    out->type = CRED_API_KEY;
    if (bearer) {
        AuthPrompt k = { .type=PROMPT_SECRET, .message="Bedrock API Key: " };
        char *key = ia->prompt(ia, &k); if (!key) return -1;
        AuthPrompt r = { .type=PROMPT_TEXT, .message="AWS Region [us-east-1]: " };
        char *reg = ia->prompt(ia, &r);
        out->key = key;
        out->env = calloc(1, sizeof(EnvEntry)); out->env_count = 1;
        out->env[0].name = strdup("AWS_REGION"); out->env[0].value = strdup(reg && *reg ? reg : "us-east-1");
        free(reg);
        return 0;
    }
    AuthPrompt ak = { .type=PROMPT_SECRET, .message="AWS Access Key ID: " };
    char *access = ia->prompt(ia, &ak); if (!access) return -1;
    AuthPrompt sk = { .type=PROMPT_SECRET, .message="AWS Secret Access Key: " };
    char *secret = ia->prompt(ia, &sk); if (!secret) { free(access); return -1; }
    AuthPrompt r = { .type=PROMPT_TEXT, .message="AWS Region [us-east-1]: " };
    char *reg = ia->prompt(ia, &r);
    const char *regv = (reg && *reg) ? reg : "us-east-1";
    out->key = NULL;
    out->env = calloc(4, sizeof(EnvEntry)); out->env_count = 4;
    out->env[0].name = strdup("AWS_ACCESS_KEY_ID"); out->env[0].value = access;
    out->env[1].name = strdup("AWS_SECRET_ACCESS_KEY"); out->env[1].value = secret;
    out->env[2].name = strdup("AWS_REGION"); out->env[2].value = strdup(regv);
    out->env[3].name = strdup("AWS_SESSION_TOKEN"); out->env[3].value = strdup("");
    free(reg);
    /* IAM: leave key NULL so resolve knows it's not a bearer token. */
    return 0;
}

#define ENV0 ((const char*[]) { NULL })
#define ENV1(a) ((const char*[]) { (a), NULL })
#define ENV2(a,b) ((const char*[]) { (a), (b), NULL })
#define ENV3(a,b,c) ((const char*[]) { (a), (b), (c), NULL })
#define K(id, nm, ev) { (id), (nm), nm " API key", "Enter " nm " API key: ", (ev), NULL, NULL, NULL }

static const ProviderAuth providers[] = {
    { "anthropic", "Anthropic", "Anthropic API key", "Enter Anthropic API key: ",
      ENV3("ANTHROPIC_AUTH_TOKEN","ANTHROPIC_OAUTH_TOKEN","ANTHROPIC_API_KEY"),
      anthropic_resolve, NULL, &anthropic_oauth },
    { "openai-codex", "OpenAI Codex", NULL, NULL, ENV0, NULL, NULL, &openai_codex_oauth },
    { "github-copilot", "GitHub Copilot", NULL, NULL, ENV1("COPILOT_GITHUB_TOKEN"),
      NULL, NULL, &github_copilot_oauth },
    { "radius", "Radius", "Radius API key", "Enter Radius API key: ",
      ENV1("RADIUS_API_KEY"), NULL, NULL, &radius_oauth },
    { "openrouter", "OpenRouter", "OpenRouter API key", "Enter OpenRouter API key: ",
      ENV1("OPENROUTER_API_KEY"), NULL, NULL, &openrouter_oauth },
    { "xai", "xAI", "xAI API key", "Enter xAI API key: ",
      ENV1("XAI_API_KEY"), NULL, NULL, &xai_oauth },
    { "kimi-coding", "Kimi Coding", "Kimi API key", "Enter Kimi API key: ",
      ENV1("KIMI_API_KEY"), NULL, NULL, &kimi_coding_oauth },
    { "bedrock", "AWS Bedrock", NULL, NULL, ENV0, bedrock_resolve, bedrock_login, NULL },
    K("openai","OpenAI",ENV1("OPENAI_API_KEY")),
    K("azure-openai-responses","Azure OpenAI",ENV1("AZURE_OPENAI_API_KEY")),
    K("google","Google",ENV1("GEMINI_API_KEY")),
    K("google-vertex","Google Vertex",ENV1("GOOGLE_CLOUD_API_KEY")),
    K("deepseek","DeepSeek",ENV1("DEEPSEEK_API_KEY")),
    K("groq","Groq",ENV1("GROQ_API_KEY")),
    K("cerebras","Cerebras",ENV1("CEREBRAS_API_KEY")),
    K("mistral","Mistral",ENV1("MISTRAL_API_KEY")),
    K("nvidia","NVIDIA",ENV1("NVIDIA_API_KEY")),
    K("fireworks","Fireworks",ENV1("FIREWORKS_API_KEY")),
    K("together","Together",ENV1("TOGETHER_API_KEY")),
    K("baseten","Baseten",ENV1("BASETEN_API_KEY")),
    K("huggingface","Hugging Face",ENV1("HF_TOKEN")),
    K("moonshotai","Moonshot",ENV1("MOONSHOT_API_KEY")),
    K("moonshotai-cn","Moonshot CN",ENV1("MOONSHOT_API_KEY")),
    K("minimax","MiniMax",ENV1("MINIMAX_API_KEY")),
    K("minimax-cn","MiniMax CN",ENV1("MINIMAX_CN_API_KEY")),
    K("opencode","OpenCode",ENV1("OPENCODE_API_KEY")),
    K("opencode-go","OpenCode Go",ENV1("OPENCODE_API_KEY")),
    K("cloudflare-workers-ai","Cloudflare Workers AI",ENV1("CLOUDFLARE_API_KEY")),
    K("cloudflare-ai-gateway","Cloudflare AI Gateway",ENV1("CLOUDFLARE_API_KEY")),
    K("vercel-ai-gateway","Vercel AI Gateway",ENV1("AI_GATEWAY_API_KEY")),
    K("zai","ZAI",ENV1("ZAI_API_KEY")),
    K("zai-coding-cn","ZAI Coding CN",ENV1("ZAI_CODING_CN_API_KEY")),
    K("ant-ling","AntLing",ENV1("ANT_LING_API_KEY")),
    K("qwen-token-plan","Qwen Token Plan",ENV1("QWEN_TOKEN_PLAN_API_KEY")),
    K("qwen-token-plan-cn","Qwen Token Plan CN",ENV1("QWEN_TOKEN_PLAN_CN_API_KEY")),
    K("qwen-token-plan-individual","Qwen Token Plan Individual",ENV1("QWEN_TOKEN_PLAN_API_KEY")),
    K("xiaomi","Xiaomi",ENV1("XIAOMI_API_KEY")),
    K("xiaomi-token-plan-cn","Xiaomi Token Plan CN",ENV1("XIAOMI_TOKEN_PLAN_CN_API_KEY")),
    K("xiaomi-token-plan-ams","Xiaomi Token Plan AMS",ENV1("XIAOMI_TOKEN_PLAN_AMS_API_KEY")),
    K("xiaomi-token-plan-sgp","Xiaomi Token Plan SGP",ENV1("XIAOMI_TOKEN_PLAN_SGP_API_KEY")),
};
static const int providers_count = (int)(sizeof(providers)/sizeof(providers[0]));

const ProviderAuth *auth_registry_get(const char *id) {
    for (int i = 0; i < providers_count; i++)
        if (strcmp(providers[i].id, id) == 0) return &providers[i];
    return NULL;
}
const ProviderAuth *const *auth_registry_all(void) {
    static const ProviderAuth *arr[64];
    for (int i = 0; i < providers_count; i++) arr[i] = &providers[i];
    arr[providers_count] = NULL;
    return arr;
}

int auth_env_api_key_resolve(const Credential *cred, AuthResult *out, AuthContext ctx,
                             AuthAbort *ab, const char *const *env_vars) {
    if (cred && cred->key) { out->api_key = strdup(cred->key); out->source = strdup("stored credential"); return 0; }
    if (!env_vars) return 1;
    for (const char *const *v = env_vars; *v; v++) {
        const char *k = ctx.env(*v, ctx.ctx);
        if (auth_aborted(ab)) return -1;
        if (k) { out->api_key = strdup(k); out->source = strdup(*v); return 0; }
    }
    return 1;
}

int auth_env_api_key_login(AuthInteraction *ia, Credential *out, const char *prompt_label) {
    AuthPrompt p = { .type = PROMPT_SECRET, .message = prompt_label };
    char *k = ia->prompt(ia, &p);
    if (!k) return -1;
    out->type = CRED_API_KEY; out->key = k;
    return 0;
}

/* ============================================================ store */

struct CredentialStore {
    bool readonly;
    bool inmemory;
    char *path;       /* auth.json path (NULL for inmemory) */
    cJSON *data;      /* in-memory cache: object keyed by provider id */
    int64_t mtime;
};

static int64_t file_mtime(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0) ? (int64_t)st.st_mtime : -1;
}

static cJSON *store_read_raw(CredentialStore *s) {
    if (!s->path) return cJSON_Duplicate(s->data, 1);
    int64_t mt = file_mtime(s->path);
    if (mt == s->mtime && s->data) return s->data;
    size_t len; char *content = fs_read_file(s->path, &len);
    if (!content) { s->mtime = -1; if (!s->data) s->data = cJSON_CreateObject(); return s->data; }
    cJSON *parsed = cJSON_Parse(content);
    free(content);
    if (!parsed || !cJSON_IsObject(parsed)) { if (parsed) cJSON_Delete(parsed); parsed = cJSON_CreateObject(); }
    if (s->data) cJSON_Delete(s->data);
    s->data = parsed; s->mtime = mt;
    return s->data;
}

/* parse one credential object from JSON */
static Credential *cred_from_json(cJSON *o) {
    if (!o || !cJSON_IsObject(o)) return NULL;
    cJSON *t = cJSON_GetObjectItem(o, "type");
    const char *ts = (t && cJSON_IsString(t)) ? t->valuestring : "api_key";
    Credential *c = credential_new(strcmp(ts, "oauth") == 0 ? CRED_OAUTH : CRED_API_KEY);
    if (!c) return NULL;
    cJSON *k = cJSON_GetObjectItem(o, "key");
    if (k && cJSON_IsString(k)) c->key = strdup(k->valuestring);
    cJSON *env = cJSON_GetObjectItem(o, "env");
    if (env && cJSON_IsObject(env)) {
        int n = 0; cJSON *e; cJSON_ArrayForEach(e, env) n++;
        if (n > 0) {
            c->env = calloc(n, sizeof(EnvEntry));
            cJSON_ArrayForEach(e, env) {
                if (cJSON_IsString(e)) {
                    c->env[c->env_count].name = strdup(e->string);
                    c->env[c->env_count].value = strdup(e->valuestring);
                    c->env_count++;
                }
            }
        }
    }
    cJSON *r = cJSON_GetObjectItem(o, "refresh"); if (r && cJSON_IsString(r)) c->refresh = strdup(r->valuestring);
    cJSON *a = cJSON_GetObjectItem(o, "access");  if (a && cJSON_IsString(a)) c->access = strdup(a->valuestring);
    cJSON *x = cJSON_GetObjectItem(o, "expires"); if (x && cJSON_IsNumber(x)) c->expires_ms = (int64_t)x->valuedouble;
    cJSON *ex = cJSON_GetObjectItem(o, "extras"); if (ex) { char *p = cJSON_PrintUnformatted(ex); c->extras = p; }
    return c;
}

static cJSON *cred_to_json(const Credential *c) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", c->type == CRED_OAUTH ? "oauth" : "api_key");
    if (c->key) cJSON_AddStringToObject(o, "key", c->key);
    if (c->env_count > 0) {
        cJSON *env = cJSON_CreateObject();
        for (int i = 0; i < c->env_count; i++)
            cJSON_AddStringToObject(env, c->env[i].name, c->env[i].value);
        cJSON_AddItemToObject(o, "env", env);
    }
    if (c->refresh) cJSON_AddStringToObject(o, "refresh", c->refresh);
    if (c->access) cJSON_AddStringToObject(o, "access", c->access);
    if (c->expires_ms) cJSON_AddNumberToObject(o, "expires", (double)c->expires_ms);
    if (c->extras) { cJSON *ex = cJSON_Parse(c->extras); if (ex) cJSON_AddItemToObject(o, "extras", ex); }
    return o;
}

static int store_write_locked(CredentialStore *s, cJSON *data) {
    if (!s->path) { return 0; }
    const char *dir = config_agent_dir();
    fs_mkdir_p(dir);
    char *json = cJSON_Print(data);
    if (!json) return -1;
    int rc = fs_write_file(s->path, json, strlen(json));
    free(json);
    if (rc == 0) chmod(s->path, 0600);
    s->mtime = file_mtime(s->path);
    /* data == s->data (modified in place); nothing else to do. */
    return rc;
}

/* hold a flock on path.lock for the scope of a mutation */
typedef struct { int fd; char *lpath; } Lock;
static bool lock_acquire(Lock *l, const char *path) {
    l->lpath = malloc(strlen(path) + 8);
    sprintf(l->lpath, "%s.lock", path);
    l->fd = open(l->lpath, O_CREAT | O_RDWR, 0600);
    if (l->fd < 0) { free(l->lpath); return false; }
    if (flock(l->fd, LOCK_EX) < 0) { close(l->fd); free(l->lpath); return false; }
    return true;
}
static void lock_release(Lock *l) {
    if (l->fd >= 0) { flock(l->fd, LOCK_UN); close(l->fd); }
    free(l->lpath);
}

CredentialStore *auth_store_create(void) {
    CredentialStore *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->path = strdup(config_auth_path());
    store_read_raw(s);
    return s;
}
CredentialStore *auth_store_create_readonly(void) {
    CredentialStore *s = auth_store_create();
    if (s) s->readonly = true;
    return s;
}
CredentialStore *auth_store_create_inmemory(void) {
    CredentialStore *s = calloc(1, sizeof(*s));
    if (s) { s->inmemory = true; s->data = cJSON_CreateObject(); }
    return s;
}
void auth_store_free(CredentialStore *s) {
    if (!s) return;
    free(s->path);
    if (s->data) cJSON_Delete(s->data);
    free(s);
}
void auth_store_reload(CredentialStore *s) {
    if (!s || !s->path) return;
    s->mtime = -1;
    store_read_raw(s);
}

Credential *auth_store_read(CredentialStore *s, const char *provider_id) {
    cJSON *data = store_read_raw(s);
    cJSON *o = cJSON_GetObjectItem(data, provider_id);
    if (!o) return NULL;
    return cred_from_json(o);
}

CredentialInfo *auth_store_list(CredentialStore *s, int *count) {
    cJSON *data = store_read_raw(s);
    int n = 0; cJSON *e; cJSON_ArrayForEach(e, data) n++;
    CredentialInfo *arr = calloc(n ? n : 1, sizeof(CredentialInfo));
    int i = 0;
    cJSON_ArrayForEach(e, data) {
        if (!cJSON_IsObject(e)) continue;
        arr[i].provider_id = strdup(e->string);
        cJSON *t = cJSON_GetObjectItem(e, "type");
        arr[i].type = (t && cJSON_IsString(t) && strcmp(t->valuestring,"oauth")==0) ? CRED_OAUTH : CRED_API_KEY;
        i++;
    }
    *count = i;
    return arr;
}

Credential *auth_store_modify(CredentialStore *s, const char *provider_id,
                              Credential *(*fn)(const Credential *cur, void *ctx), void *ctx) {
    if (s->readonly) return NULL;
    if (s->inmemory) {
        cJSON *data = s->data;
        cJSON *o = cJSON_GetObjectItem(data, provider_id);
        Credential *cur = o ? cred_from_json(o) : NULL;
        Credential *next = fn(cur, ctx);
        credential_free(cur);
        if (!next) return NULL;
        cJSON *nj = cred_to_json(next);
        if (o) cJSON_ReplaceItemInObject(data, provider_id, nj);
        else cJSON_AddItemToObject(data, provider_id, nj);
        Credential *ret = credential_clone(next);
        credential_free(next);
        return ret;
    }
    Lock lk = { -1, NULL };
    if (!lock_acquire(&lk, s->path)) return NULL;
    s->mtime = -1; cJSON *data = store_read_raw(s);
    cJSON *o = cJSON_GetObjectItem(data, provider_id);
    Credential *cur = o ? cred_from_json(o) : NULL;
    Credential *next = fn(cur, ctx);
    credential_free(cur);
    Credential *ret = NULL;
    if (next) {
        cJSON *nj = cred_to_json(next);
        if (o) cJSON_ReplaceItemInObject(data, provider_id, nj);
        else cJSON_AddItemToObject(data, provider_id, nj);
        store_write_locked(s, data);
        ret = credential_clone(next);
        credential_free(next);
    }
    lock_release(&lk);
    return ret;
}

int auth_store_delete(CredentialStore *s, const char *provider_id) {
    if (s->readonly) return -1;
    if (s->inmemory) { cJSON_DeleteItemFromObject(s->data, provider_id); return 0; }
    Lock lk = { -1, NULL };
    if (!lock_acquire(&lk, s->path)) return -1;
    s->mtime = -1; cJSON *data = store_read_raw(s);
    cJSON_DeleteItemFromObject(data, provider_id);
    store_write_locked(s, data);
    lock_release(&lk);
    return 0;
}

/* ============================================================ resolve */

#define OAUTH_MIN_VALIDITY_MS (5 * 60 * 1000)

static int64_t now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

typedef struct { const OAuthFlow *flow; AuthAbort *abort; int refreshed; } RefreshCtx;

static Credential *refresh_fn(const Credential *cur, void *ctx) {
    RefreshCtx *rc = ctx;
    if (!cur || cur->type != CRED_OAUTH) return NULL;
    if (now_ms() + OAUTH_MIN_VALIDITY_MS < cur->expires_ms) return NULL; /* not yet */
    Credential *next = credential_new(CRED_OAUTH);
    if (!next) return NULL;
    if (rc->flow->refresh(cur, next, rc->abort) != 0) { credential_free(next); return NULL; }
    rc->refreshed = 1;
    return next;
}

static AuthResult *resolve_oauth(CredentialStore *s, const char *id, const OAuthFlow *f,
                                 Credential *stored, AuthAbort *ab) {
    if (stored && now_ms() + OAUTH_MIN_VALIDITY_MS >= stored->expires_ms) {
        RefreshCtx rc = { f, ab, 0 };
        Credential *rotated = auth_store_modify(s, id, refresh_fn, &rc);
        if (rotated) { credential_free(stored); stored = rotated; }
    }
    AuthResult *r = calloc(1, sizeof(*r));
    if (f->to_auth(stored, r) != 0) { auth_result_free(r); credential_free(stored); return NULL; }
    r->source = strdup("OAuth");
    credential_free(stored);
    return r;
}

AuthResult *auth_resolve_with_ctx(CredentialStore *s, const char *id, AuthContext ctx, AuthAbort *ab) {
    const ProviderAuth *p = auth_registry_get(id);
    if (!p) return NULL;
    Credential *stored = auth_store_read(s, id);
    if (stored) {
        if (stored->type == CRED_OAUTH && p->oauth)
            return resolve_oauth(s, id, p->oauth, stored, ab);
        if (stored->type == CRED_API_KEY && p->env_vars) {
            AuthResult *r = calloc(1, sizeof(*r));
            ApiKeyResolveFn res = p->resolve ? p->resolve : NULL;
            int rc;
            if (res) rc = res(stored, r, ctx, ab);
            else rc = auth_env_api_key_resolve(stored, r, ctx, ab, p->env_vars);
            credential_free(stored);
            if (rc == 0) return r;
            auth_result_free(r); return NULL;
        }
        credential_free(stored);
        return NULL;
    }
    /* ambient */
    if (p->resolve) {
        AuthResult *r = calloc(1, sizeof(*r));
        int rc = p->resolve(NULL, r, ctx, ab);
        if (rc == 0) return r;
        auth_result_free(r); return NULL;
    }
    if (p->env_vars) {
        AuthResult *r = calloc(1, sizeof(*r));
        if (auth_env_api_key_resolve(NULL, r, ctx, ab, p->env_vars) == 0) return r;
        auth_result_free(r);
    }
    return NULL;
}

AuthResult *auth_resolve(CredentialStore *s, const char *id) {
    AuthAbort ab; auth_abort_init(&ab);
    return auth_resolve_with_ctx(s, id, auth_default_context(), &ab);
}

bool auth_check(CredentialStore *s, const char *id) {
    AuthResult *r = auth_resolve(s, id);
    bool ok = r != NULL;
    auth_result_free(r);
    return ok;
}

/* ============================================================ login/logout */

typedef struct { const Credential *c; } SetCtx;
static Credential *set_fn(const Credential *cur, void *ctx) {
    (void)cur;
    return credential_clone(((SetCtx *)ctx)->c);
}

int auth_login(CredentialStore *s, const char *id, CredentialType type, AuthInteraction *ia) {
    const ProviderAuth *p = auth_registry_get(id);
    if (!p) return -1;
    Credential *out = credential_new(type);
    if (!out) return -1;
    if (type == CRED_OAUTH) {
        if (!p->oauth || p->oauth->login(ia, out) != 0) { credential_free(out); return -1; }
    } else {
        if (p->login) { if (p->login(ia, out) != 0) { credential_free(out); return -1; } }
        else if (p->api_key_prompt) { if (auth_env_api_key_login(ia, out, p->api_key_prompt) != 0) { credential_free(out); return -1; } }
        else { credential_free(out); return -1; }
    }
    SetCtx sc = { out };
    credential_free(auth_store_modify(s, id, set_fn, &sc));
    credential_free(out);
    return 0;
}

int auth_logout(CredentialStore *s, const char *id) {
    return auth_store_delete(s, id);
}

/* ============================================================ back-compat */

char *auth_get_api_key(const char *provider_id) {
    if (!provider_id) return NULL;
    CredentialStore *s = auth_store_create();
    AuthResult *r = auth_resolve(s, provider_id);
    auth_store_free(s);
    if (!r) return NULL;
    char *key = NULL;
    if (r->api_key) key = strdup(r->api_key);
    else if (r->auth_header) {
        /* "Bearer X" or raw token */
        const char *h = r->auth_header;
        const char *sp = strchr(h, ' ');
        key = strdup(sp ? sp + 1 : h);
    }
    auth_result_free(r);
    return key;
}

const char *auth_get_active_provider(void) {
    /* prefer a stored credential; else first provider with ambient env configured */
    static char buf[64];
    CredentialStore *s = auth_store_create();
    int n = 0; CredentialInfo *list = auth_store_list(s, &n);
    if (n > 0) {
        strncpy(buf, list[0].provider_id, sizeof(buf)-1);
        buf[sizeof(buf)-1] = '\0';
        credential_info_free(list, n);
        auth_store_free(s);
        return buf;
    }
    auth_store_free(s);
    AuthAbort ab; auth_abort_init(&ab);
    AuthContext ctx = auth_default_context();
    for (int i = 0; i < providers_count; i++) {
        const ProviderAuth *p = &providers[i];
        AuthResult *r = calloc(1, sizeof(*r));
        int rc = p->resolve ? p->resolve(NULL, r, ctx, &ab)
                            : auth_env_api_key_resolve(NULL, r, ctx, &ab, p->env_vars);
        if (rc == 0) {
            auth_result_free(r);
            strncpy(buf, p->id, sizeof(buf)-1); buf[sizeof(buf)-1] = '\0';
            return buf;
        }
        auth_result_free(r);
    }
    return NULL;
}

bool auth_is_configured(void) {
    CredentialStore *s = auth_store_create();
    int n = 0; CredentialInfo *list = auth_store_list(s, &n);
    bool stored = n > 0;
    credential_info_free(list, n);
    auth_store_free(s);
    if (stored) return true;
    return auth_get_active_provider() != NULL;
}

/* ============================================================ CLI */

static void print_help(void) {
    fprintf(stderr,
        "Usage:\n"
        "  rig auth                              Interactive login (pick provider + method)\n"
        "  rig auth check --provider <p> [--model <m>] [--json] [--credentials] [--no-refresh]\n"
        "  rig auth print-api-key --provider <p> [--model <m>]\n"
        "  rig auth print-bearer-token --provider <p> [--model <m>] [--min-expiry <duration>]\n"
        "  rig auth logout [--provider <p>]\n"
        "  rig auth status\n"
        "\n");
}

static int cmd_status(void) {
    CredentialStore *s = auth_store_create();
    int n = 0; CredentialInfo *list = auth_store_list(s, &n);
    if (n > 0) {
        for (int i = 0; i < n; i++) {
            fprintf(stderr, "  %s (%s)\n", list[i].provider_id, list[i].type == CRED_OAUTH ? "oauth" : "api_key");
        }
        credential_info_free(list, n);
    } else {
        fprintf(stderr, "No stored credentials.\n");
    }
    auth_store_free(s);
    const char *active = auth_get_active_provider();
    if (active) fprintf(stderr, "Active (ambient): %s\n", active);
    fprintf(stderr, "Config: %s\n", config_auth_path());
    return 0;
}

static int cmd_logout(const char *provider) {
    if (!provider) {
        /* logout all */
        CredentialStore *s = auth_store_create();
        int n = 0; CredentialInfo *list = auth_store_list(s, &n);
        for (int i = 0; i < n; i++) auth_store_delete(s, list[i].provider_id);
        credential_info_free(list, n);
        auth_store_free(s);
        fprintf(stderr, "Logged out all providers.\n");
        return 0;
    }
    CredentialStore *s = auth_store_create();
    int rc = auth_store_delete(s, provider);
    auth_store_free(s);
    if (rc == 0) fprintf(stderr, "Logged out %s.\n", provider);
    else fprintf(stderr, "No credential for %s.\n", provider);
    return rc == 0 ? 0 : 1;
}

/* parse --min-expiry like 30m, 1h, 600s, 600000ms */
static int64_t parse_duration(const char *s) {
    size_t n = strlen(s);
    if (n < 2) return -1;
    char unit = s[n-1];
    char unit2 = n >= 2 ? s[n-2] : 0;
    long val;
    if (unit == 's' && unit2 == 'm') { val = atol(s); return val; }
    if (unit == 's') { val = atol(s); return val * 1000; }
    if (unit == 'm') { val = atol(s); return val * 60000; }
    if (unit == 'h') { val = atol(s); return val * 3600000; }
    return -1;
}

/* resolve --provider or --model from remaining args */
static const char *find_flag(int argc, char **argv, const char *name) {
    for (int i = 0; i < argc - 1; i++)
        if (strcmp(argv[i], name) == 0) return argv[i+1];
    return NULL;
}

/* resolve a provider from a model pattern (first builtin model matching) */
static const char *provider_from_model(const char *pattern) {
    int n = 0; const Model **all = models_get_all(NULL, &n);
    for (int i = 0; i < n; i++) {
        if (strstr(all[i]->id, pattern) || strstr(all[i]->name, pattern)) return all[i]->provider;
    }
    return NULL;
}

static int cmd_check(int argc, char **argv) {
    bool json = false, creds = false, no_refresh = false;
    const char *provider = NULL, *model = NULL;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i],"--json")==0) json = true;
        else if (strcmp(argv[i],"--credentials")==0) creds = true;
        else if (strcmp(argv[i],"--no-refresh")==0) no_refresh = true;
        else if (strcmp(argv[i],"--provider")==0 && i+1<argc) provider = argv[++i];
        else if (strcmp(argv[i],"--model")==0 && i+1<argc) model = argv[++i];
    }
    if (!provider && !model) { fprintf(stderr, "Error: --provider or --model required\n"); return 2; }
    if (!provider) { provider = provider_from_model(model); if (!provider) { fprintf(stderr, "Error: cannot resolve model %s\n", model); return 2; } }
    CredentialStore *s = no_refresh ? auth_store_create_readonly() : auth_store_create();
    bool ok = auth_check(s, provider);
    const char *status = ok ? "ready" : "not_ready";
    if (json) {
        fprintf(stdout, "{\"status\":\"%s\",\"provider\":\"%s\"}\n", status, provider);
    } else if (creds && ok) {
        char *k = auth_get_api_key(provider);
        fprintf(stdout, "%s\n", k ? k : "not_ready");
        free(k);
    } else {
        fprintf(stdout, "%s\n", status);
    }
    auth_store_free(s);
    return ok ? 0 : 1;
}

static int cmd_print_key(int argc, char **argv) {
    const char *provider = find_flag(argc, argv, "--provider");
    const char *model = find_flag(argc, argv, "--model");
    if (!provider && !model) { fprintf(stderr, "Error: --provider or --model required\n"); return 1; }
    if (!provider) { provider = provider_from_model(model); if (!provider) return 1; }
    char *k = auth_get_api_key(provider);
    if (!k) { fprintf(stderr, "not_ready\n"); return 1; }
    fprintf(stdout, "%s\n", k);
    free(k);
    return 0;
}

static int cmd_print_bearer(int argc, char **argv) {
    const char *provider = find_flag(argc, argv, "--provider");
    const char *model = find_flag(argc, argv, "--model");
    const char *minexp = find_flag(argc, argv, "--min-expiry");
    if (!provider && !model) { fprintf(stderr, "Error: --provider or --model required\n"); return 1; }
    if (!provider) { provider = provider_from_model(model); if (!provider) return 1; }
    /* resolve OAuth access token; refresh if min-expiry demands */
    CredentialStore *s = auth_store_create();
    Credential *c = auth_store_read(s, provider);
    if (!c || c->type != CRED_OAUTH) { fprintf(stderr, "not_ready\n"); auth_store_free(s); credential_free(c); return 1; }
    int64_t min = minexp ? parse_duration(minexp) : 0;
    if (min > 0 && now_ms() + min >= c->expires_ms) {
        const ProviderAuth *p = auth_registry_get(provider);
        if (p && p->oauth) {
            AuthAbort ab; auth_abort_init(&ab);
            Credential *next = credential_new(CRED_OAUTH);
            if (p->oauth->refresh(c, next, &ab) == 0) {
                SetCtx sc = { next };
                credential_free(auth_store_modify(s, provider, set_fn, &sc));
                credential_free(c);
                c = credential_clone(next);
            }
            credential_free(next);
        }
    }
    if (!c || c->type != CRED_OAUTH || !c->access) { fprintf(stderr, "not_ready\n"); credential_free(c); auth_store_free(s); return 1; }
    fprintf(stdout, "%s\n", c->access ? c->access : "");
    credential_free(c);
    auth_store_free(s);
    return 0;
}

int auth_cli_main(int argc, char **argv) {
    if (argc < 2) return auth_interactive_setup();
    const char *sub = argv[1];
    if (strcmp(sub, "help")==0 || strcmp(sub,"--help")==0 || strcmp(sub,"-h")==0) { print_help(); return 0; }
    if (strcmp(sub, "status")==0) return cmd_status();
    if (strcmp(sub, "logout")==0) {
        const char *p = find_flag(argc - 1, argv + 1, "--provider");
        return cmd_logout(p);
    }
    if (strcmp(sub, "check")==0) return cmd_check(argc - 1, argv + 1);
    if (strcmp(sub, "print-api-key")==0) return cmd_print_key(argc - 1, argv + 1);
    if (strcmp(sub, "print-bearer-token")==0) return cmd_print_bearer(argc - 1, argv + 1);
    fprintf(stderr, "Unknown auth command '%s'. See `rig auth help`.\n", sub);
    return 2;
}

/* interactive: pick provider, pick method, login, show models */
int auth_interactive_setup(void) {
    fprintf(stderr, "\n  Rig Authentication\n  ==================\n\n  Providers:\n");
    const ProviderAuth *const *all = auth_registry_all();
    /* count */
    int n = 0; while (all[n]) n++;
    for (int i = 0; i < n; i++)
        fprintf(stderr, "  %2d) %-22s %s%s\n", i+1, all[i]->name,
                all[i]->oauth ? "(OAuth" : "",
                all[i]->oauth ? (all[i]->oauth->is_subscription ? " sub)" : ")") : "");
    fprintf(stderr, "\n");
    char *choice = read_line("  Select provider (number, or name): ", false);
    if (!choice) { fprintf(stderr, "  Cancelled.\n"); return 1; }
    const ProviderAuth *p = NULL;
    if (isdigit((unsigned char)choice[0])) {
        int idx = atoi(choice) - 1;
        if (idx >= 0 && idx < n) p = all[idx];
    } else {
        p = auth_registry_get(choice);
    }
    free(choice);
    if (!p) { fprintf(stderr, "  Invalid choice.\n"); return 1; }

    AuthAbort ab; auth_abort_init(&ab);
    AuthInteraction ia = auth_cli_interaction(&ab);
    CredentialStore *store = auth_store_create();

    int rc;
    if (p->oauth && p->env_vars) {
        AuthPrompt m = { .type=PROMPT_SELECT, .message="  Auth method",
            .option_ids=(const char*[]){"oauth","api_key",NULL},
            .option_labels=(const char*[]){"OAuth (sign in via browser/device)","API key",NULL},
            .option_count=2 };
        char *m_ = ia.prompt(&ia, &m);
        if (!m_) { fprintf(stderr, "  Cancelled.\n"); auth_store_free(store); return 1; }
        bool oauth = strcmp(m_, "oauth") == 0; free(m_);
        rc = auth_login(store, p->id, oauth ? CRED_OAUTH : CRED_API_KEY, &ia);
    } else if (p->oauth) {
        rc = auth_login(store, p->id, CRED_OAUTH, &ia);
    } else if (p->login || p->api_key_prompt) {
        rc = auth_login(store, p->id, CRED_API_KEY, &ia);
    } else {
        fprintf(stderr, "  Provider %s is ambient-only (no interactive login). Set %s.\n",
                p->id, p->env_vars && p->env_vars[0] ? p->env_vars[0] : "credentials manually");
        auth_store_free(store);
        return 1;
    }
    auth_store_free(store);

    if (rc != 0) { fprintf(stderr, "  Login failed.\n"); return 1; }
    fprintf(stderr, "\n  Saved to %s\n  Provider: %s\n\n", config_auth_path(), p->id);

    /* show models discovered for this provider */
    int mc = 0; const Model **mods = models_get_all(p->id, &mc);
    if (mc > 0) {
        fprintf(stderr, "  Models for %s:\n", p->id);
        for (int i = 0; i < mc; i++) fprintf(stderr, "    %s (%s)\n", mods[i]->name, mods[i]->id);
    } else {
        fprintf(stderr, "  No builtin models for %s. Add custom models via %s.\n", p->id, config_models_path());
    }
    fprintf(stderr, "\n");
    return 0;
}
