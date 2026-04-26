#ifndef PI_HARNESS_AUTH_H
#define PI_HARNESS_AUTH_H

#include <stdbool.h>

typedef struct {
    char *provider;
    char *api_key;
    char *aws_access_key;
    char *aws_secret_key;
    char *aws_region;
    char *aws_session_token;
} AuthCredentials;

char *auth_get_api_key(const char *provider);

AuthCredentials *auth_load(void);
void auth_credentials_free(AuthCredentials *creds);

int auth_save(const char *provider, const char *api_key,
              const char *aws_access_key, const char *aws_secret_key,
              const char *aws_region, const char *aws_session_token);

int auth_logout(void);

bool auth_is_configured(void);

const char *auth_get_active_provider(void);

int auth_interactive_setup(void);

#endif
