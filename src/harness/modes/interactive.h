#ifndef PI_HARNESS_INTERACTIVE_MODE_H
#define PI_HARNESS_INTERACTIVE_MODE_H

typedef struct PiInstance PiInstance;

int interactive_mode_start(PiInstance *pi, const char *session_id,
                           const char *model_pattern, const char *provider);

#endif
