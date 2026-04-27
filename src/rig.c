#include "rig.h"
#include "util/log.h"
#include <stdlib.h>
#include <string.h>

const char *rig_version(void) {
    return RIG_VERSION;
}

RigInstance *rig_create(void) {
    RigInstance *r = calloc(1, sizeof(RigInstance));
    if (!r) return NULL;

    r->api = extension_api_create();
    if (!r->api) {
        free(r);
        return NULL;
    }

    return r;
}

void rig_free(RigInstance *r) {
    if (!r) return;

    if (r->rpc) rpc_server_free(r->rpc);
    if (r->session) session_free(r->session);
    if (r->api) extension_api_free(r->api);

    free(r);
}

int rig_init(RigInstance *r) {
    if (!r || !r->api) return -1;

    ai_registry_init();
    http_global_init();

    const char *global_dir = config_agent_dir();
    if (global_dir) {
        extension_discover_and_load(r->api, ".rig", global_dir);
    }

    hook_chain_fire(r->api->hooks, "init", NULL, NULL);

    r->initialized = true;
    return 0;
}

int rig_run_print(RigInstance *r, const char *prompt) {
    if (!r || !prompt) return -1;
    if (!r->initialized) rig_init(r);

    (void)prompt;
    return 0;
}

int rig_run_rpc(RigInstance *r) {
    if (!r) return -1;
    if (!r->initialized) rig_init(r);

    r->rpc = rpc_server_create(r->api);
    if (!r->rpc) return -1;

    return rpc_server_start(r->rpc);
}

int rig_run_interactive(RigInstance *r) {
    if (!r) return -1;
    if (!r->initialized) rig_init(r);
    return 0;
}
