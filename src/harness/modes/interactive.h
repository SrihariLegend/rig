#ifndef RIG_HARNESS_INTERACTIVE_MODE_H
#define RIG_HARNESS_INTERACTIVE_MODE_H

typedef struct RigInstance RigInstance;

int interactive_mode_start(RigInstance *rig, const char *session_id,
                           const char *model_pattern, const char *provider);

#endif
