#ifndef PI_H
#define PI_H

#include "ai/types.h"
#include "ai/registry.h"
#include "ai/models.h"
#include "agent/agent.h"
#include "harness/config.h"
#include "harness/auth.h"
#include "harness/session.h"
#include "harness/system_prompt.h"
#include "harness/extensions/extension.h"
#include "harness/extensions/hooks.h"
#include "harness/extensions/event_bus.h"
#include "harness/extensions/lua_ext.h"
#include "harness/workflow/workflow.h"
#include "harness/workflow/expr.h"
#include "harness/modes/rpc.h"
#include "tui/tui.h"
#include "tui/terminal.h"
#include "tui/keys.h"
#include "tui/ansi.h"
#include "tui/widgets/text.h"
#include "tui/widgets/input.h"
#include "tui/widgets/box.h"
#include "tui/widgets/loader.h"
#include "tui/widgets/select_list.h"
#include "util/arena.h"
#include "util/str.h"
#include "util/vec.h"
#include "util/hashmap.h"
#include "util/json.h"
#include "util/http.h"
#include "util/fs.h"
#include "util/process.h"

#define PI_VERSION "0.1.0"
#define PI_VERSION_MAJOR 0
#define PI_VERSION_MINOR 1
#define PI_VERSION_PATCH 0

typedef struct PiInstance {
    PiExtensionAPI *api;
    AgentState *agent;
    Session *session;
    TUI *tui;
    RPCServer *rpc;
    bool initialized;
} PiInstance;

PiInstance *pi_create(void);
void pi_free(PiInstance *pi);

int pi_init(PiInstance *pi);

int pi_run_print(PiInstance *pi, const char *prompt);
int pi_run_rpc(PiInstance *pi);
int pi_run_interactive(PiInstance *pi);

const char *pi_version(void);

#endif
