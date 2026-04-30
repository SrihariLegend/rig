#ifndef RIG_PROCESS_H
#define RIG_PROCESS_H

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

/* Persistent subprocess with bidirectional pipes */
typedef struct {
    pid_t pid;
    int stdin_fd;
    int stdout_fd;
    bool alive;
} SpawnedProcess;

int process_spawn(SpawnedProcess *sp, const char *command, const char *cwd);
int process_spawn_write(SpawnedProcess *sp, const char *data, size_t len);
int process_spawn_read_line(SpawnedProcess *sp, char **out, int timeout_ms);
void process_spawn_close(SpawnedProcess *sp);

#endif
