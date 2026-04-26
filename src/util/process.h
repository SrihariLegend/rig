#ifndef PI_PROCESS_H
#define PI_PROCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

typedef struct {
    const char *command;
    const char *cwd;
    const char **env;
    int timeout_ms;
    void (*on_stdout)(const char *data, size_t len, void *ctx);
    void (*on_stderr)(const char *data, size_t len, void *ctx);
    void *ctx;
    volatile bool *abort_flag;
} ProcessOptions;

typedef struct {
    pid_t pid;
    int exit_code;
    bool timed_out;
    bool aborted;
} ProcessResult;

int process_run(const ProcessOptions *opts, ProcessResult *result);
int process_kill(pid_t pid);

#endif
