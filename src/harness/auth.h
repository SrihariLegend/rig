#ifndef RIG_HARNESS_AUTH_H
#define RIG_HARNESS_AUTH_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/*
 * Rig auth — port of pi's auth architecture, collapsed for density.
 *
 *   CredentialStore  multi-provider auth.json, file-locked, 0600.
 *   ProviderAuth     per-provider registry: api-key and/or oauth.
 *   AuthInteraction  UI-agnostic prompt/notify (TUI/CLI/web).
 *   resolve          stored credential owns the provider; ambient env is
 *                    fallback; OAuth tokens refresh under the store lock.
 *
 * Implementation: auth.c (core) + auth_oauth.c (flows).
 */

/* ---- credentials ---- */
typedef enum { CRED_API_KEY = 0, CRED_OAUTH = 1 } CredentialType;

typedef struct { char *name; char *value; } EnvEntry;

typedef struct {
    CredentialType type;
    char *key;          /* api_key; may be NULL (ambient-only); may be a shell cmd */
    EnvEntry *env; int env_count;
    char *refresh, *access; int64_t expires_ms; char *extras; /* oauth */
} Credential;

void credential_free(Credential *c);
Credential *credential_clone(const Credential *c);

typedef struct { char *provider_id; CredentialType type; } CredentialInfo;
void credential_info_free(CredentialInfo *ci, int count);

typedef struct { char *api_key; char *auth_header; char *base_url; char *source; } AuthResult;
void auth_result_free(AuthResult *r);

/* ---- ambient context ---- */
typedef struct {
    const char *(*env)(const char *name, void *ctx);
    bool (*file_exists)(const char *path, void *ctx);
    void *ctx;
} AuthContext;
AuthContext auth_default_context(void);

/* ---- cancellation ---- */
typedef struct { volatile bool flag; } AuthAbort;
void auth_abort_init(AuthAbort *a);
void auth_abort_cancel(AuthAbort *a);
bool auth_aborted(const AuthAbort *a);

/* ---- interaction ---- */
typedef enum { PROMPT_TEXT, PROMPT_SECRET, PROMPT_SELECT, PROMPT_MANUAL_CODE } PromptType;
typedef struct {
    PromptType type; const char *message, *placeholder;
    const char *const *option_ids, *const *option_labels; int option_count;
} AuthPrompt;

typedef enum { EVENT_INFO, EVENT_AUTH_URL, EVENT_DEVICE_CODE, EVENT_PROGRESS } EventType;
typedef struct {
    EventType type; const char *message, *url, *instructions;
    const char *user_code, *verification_uri; int interval_seconds, expires_in_seconds;
} AuthEvent;

typedef struct AuthInteraction {
    AuthAbort *abort;
    char *(*prompt)(struct AuthInteraction *self, const AuthPrompt *p);
    void (*notify)(struct AuthInteraction *self, const AuthEvent *e);
    void *userdata;
} AuthInteraction;

AuthInteraction auth_cli_interaction(AuthAbort *abort);

/* ---- store ---- */
typedef struct CredentialStore CredentialStore;
CredentialStore *auth_store_create(void);
CredentialStore *auth_store_create_readonly(void);
CredentialStore *auth_store_create_inmemory(void);
void auth_store_free(CredentialStore *s);
Credential *auth_store_read(CredentialStore *s, const char *provider_id);
CredentialInfo *auth_store_list(CredentialStore *s, int *count);
Credential *auth_store_modify(CredentialStore *s, const char *provider_id,
                              Credential *(*fn)(const Credential *cur, void *ctx), void *ctx);
int auth_store_delete(CredentialStore *s, const char *provider_id);
void auth_store_reload(CredentialStore *s);

/* ---- registry ---- */
typedef struct OAuthFlow OAuthFlow;
typedef int (*ApiKeyResolveFn)(const Credential *cred, AuthResult *out, AuthContext ctx, AuthAbort *abort);
typedef int (*ApiKeyLoginFn)(AuthInteraction *ia, Credential *out);

struct OAuthFlow {
    const char *name; bool is_subscription; const char *login_label;
    int (*login)(AuthInteraction *ia, Credential *out);
    int (*refresh)(const Credential *in, Credential *out, AuthAbort *abort);
    int (*to_auth)(const Credential *in, AuthResult *out);
};

typedef struct {
    const char *id, *name;
    const char *api_key_name, *api_key_prompt;
    const char *const *env_vars;   /* NULL-terminated ambient env vars */
    ApiKeyResolveFn resolve;       /* NULL => default env resolve */
    ApiKeyLoginFn login;           /* NULL => default secret prompt */
    const OAuthFlow *oauth;        /* NULL => no oauth */
} ProviderAuth;

const ProviderAuth *auth_registry_get(const char *id);
const ProviderAuth *const *auth_registry_all(void);

int auth_env_api_key_resolve(const Credential *cred, AuthResult *out, AuthContext ctx,
                             AuthAbort *abort, const char *const *env_vars);
int auth_env_api_key_login(AuthInteraction *ia, Credential *out, const char *prompt_label);

/* ---- resolve / check / login / logout ---- */
AuthResult *auth_resolve(CredentialStore *s, const char *provider_id);
AuthResult *auth_resolve_with_ctx(CredentialStore *s, const char *provider_id, AuthContext ctx, AuthAbort *abort);
bool auth_check(CredentialStore *s, const char *provider_id);
int auth_login(CredentialStore *s, const char *provider_id, CredentialType type, AuthInteraction *ia);
int auth_logout(CredentialStore *s, const char *provider_id);

/* ---- back-compat ---- */
char *auth_get_api_key(const char *provider_id);
const char *auth_get_active_provider(void);
bool auth_is_configured(void);

/* ---- cli ---- */
int auth_cli_main(int argc, char **argv);
int auth_interactive_setup(void);

#endif
