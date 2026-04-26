#include "pi.h"
#include "util/log.h"
#include <stdlib.h>
#include <string.h>

const char *pi_version(void) {
    return PI_VERSION;
}

PiInstance *pi_create(void) {
    PiInstance *pi = calloc(1, sizeof(PiInstance));
    if (!pi) return NULL;

    pi->api = extension_api_create();
    if (!pi->api) {
        free(pi);
        return NULL;
    }

    return pi;
}

void pi_free(PiInstance *pi) {
    if (!pi) return;

    if (pi->rpc) rpc_server_free(pi->rpc);
    if (pi->tui) tui_free(pi->tui);
    if (pi->session) session_free(pi->session);
    if (pi->api) extension_api_free(pi->api);

    free(pi);
}

int pi_init(PiInstance *pi) {
    if (!pi || !pi->api) return -1;

    ai_registry_init();
    http_global_init();

    const char *global_dir = config_agent_dir();
    if (global_dir) {
        extension_discover_and_load(pi->api, ".pi", global_dir);
    }

    hook_chain_fire(pi->api->hooks, "init", NULL, NULL);

    pi->initialized = true;
    return 0;
}

int pi_run_print(PiInstance *pi, const char *prompt) {
    if (!pi || !prompt) return -1;
    if (!pi->initialized) pi_init(pi);

    (void)prompt;
    return 0;
}

int pi_run_rpc(PiInstance *pi) {
    if (!pi) return -1;
    if (!pi->initialized) pi_init(pi);

    pi->rpc = rpc_server_create(pi->api);
    if (!pi->rpc) return -1;

    return rpc_server_start(pi->rpc);
}

int pi_run_interactive(PiInstance *pi) {
    if (!pi) return -1;
    if (!pi->initialized) pi_init(pi);

    pi->tui = tui_create();
    if (!pi->tui) return -1;

    return 0;
}
