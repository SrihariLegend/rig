#include "process.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>
#include <time.h>

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int process_run(const ProcessOptions *opts, ProcessResult *result) {
    memset(result, 0, sizeof(*result));

    int stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdout_pipe) != 0) return -1;
    if (pipe(stderr_pipe) != 0) { close(stdout_pipe[0]); close(stdout_pipe[1]); return -1; }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        if (opts->cwd && chdir(opts->cwd) != 0) _exit(127);
        if (opts->env) {
            for (const char **e = opts->env; *e; e++) {
                const char *eq = strchr(*e, '=');
                if (eq) {
                    char key[128];
                    int klen = (int)(eq - *e);
                    if (klen > 127) klen = 127;
                    memcpy(key, *e, klen);
                    key[klen] = '\0';
                    setenv(key, eq + 1, 1);
                }
            }
        }

        execl("/bin/sh", "sh", "-c", opts->command, (char *)NULL);
        _exit(127);
    }

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    result->pid = pid;

    fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);

    char buf[4096];
    int64_t deadline = opts->timeout_ms > 0 ? now_ms() + opts->timeout_ms : 0;
    bool done = false;

    while (!done) {
        if (opts->abort_flag && *opts->abort_flag) {
            process_kill(pid);
            result->aborted = true;
            break;
        }

        if (deadline > 0 && now_ms() >= deadline) {
            process_kill(pid);
            result->timed_out = true;
            break;
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = -1;

        if (stdout_pipe[0] >= 0) { FD_SET(stdout_pipe[0], &rfds); if (stdout_pipe[0] > maxfd) maxfd = stdout_pipe[0]; }
        if (stderr_pipe[0] >= 0) { FD_SET(stderr_pipe[0], &rfds); if (stderr_pipe[0] > maxfd) maxfd = stderr_pipe[0]; }

        if (maxfd < 0) break;

        struct timeval tv = { .tv_sec = 0, .tv_usec = 50000 };
        int ready = select(maxfd + 1, &rfds, NULL, NULL, &tv);

        if (ready > 0) {
            if (stdout_pipe[0] >= 0 && FD_ISSET(stdout_pipe[0], &rfds)) {
                ssize_t n = read(stdout_pipe[0], buf, sizeof(buf));
                if (n > 0) {
                    if (opts->on_stdout) opts->on_stdout(buf, (size_t)n, opts->ctx);
                } else if (n == 0) {
                    close(stdout_pipe[0]);
                    stdout_pipe[0] = -1;
                }
            }
            if (stderr_pipe[0] >= 0 && FD_ISSET(stderr_pipe[0], &rfds)) {
                ssize_t n = read(stderr_pipe[0], buf, sizeof(buf));
                if (n > 0) {
                    if (opts->on_stderr) opts->on_stderr(buf, (size_t)n, opts->ctx);
                } else if (n == 0) {
                    close(stderr_pipe[0]);
                    stderr_pipe[0] = -1;
                }
            }
        }

        if (stdout_pipe[0] < 0 && stderr_pipe[0] < 0) done = true;
    }

    if (stdout_pipe[0] >= 0) close(stdout_pipe[0]);
    if (stderr_pipe[0] >= 0) close(stderr_pipe[0]);

    int status;
    waitpid(pid, &status, 0);
    result->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return 0;
}

int process_kill(pid_t pid) {
    if (kill(pid, SIGTERM) != 0) return -1;

    int64_t deadline = now_ms() + 2000;
    while (now_ms() < deadline) {
        int status;
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r > 0) return 0;
        usleep(50000);
    }

    kill(pid, SIGKILL);
    int status;
    waitpid(pid, &status, 0);
    return 0;
}

/* ---- Persistent subprocess ---- */

int process_spawn(SpawnedProcess *sp, const char *command, const char *cwd) {
    if (!sp || !command) return -1;
    memset(sp, 0, sizeof(*sp));
    sp->stdin_fd = -1;
    sp->stdout_fd = -1;

    int in_pipe[2], out_pipe[2];
    if (pipe(in_pipe) != 0) return -1;
    if (pipe(out_pipe) != 0) { close(in_pipe[0]); close(in_pipe[1]); return -1; }

    pid_t pid = fork();
    if (pid < 0) {
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        close(in_pipe[1]);
        close(out_pipe[0]);
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(out_pipe[1], STDERR_FILENO);
        close(in_pipe[0]);
        close(out_pipe[1]);
        if (cwd && chdir(cwd) != 0) _exit(127);
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(127);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);
    fcntl(out_pipe[0], F_SETFL, O_NONBLOCK);

    sp->pid = pid;
    sp->stdin_fd = in_pipe[1];
    sp->stdout_fd = out_pipe[0];
    sp->alive = true;
    return 0;
}

int process_spawn_write(SpawnedProcess *sp, const char *data, size_t len) {
    if (!sp || !sp->alive || sp->stdin_fd < 0) return -1;
    signal(SIGPIPE, SIG_IGN);
    ssize_t n = write(sp->stdin_fd, data, len);
    signal(SIGPIPE, SIG_DFL);
    if (n < 0) { sp->alive = false; return -1; }
    return 0;
}

int process_spawn_read_line(SpawnedProcess *sp, char **out, int timeout_ms) {
    if (!sp || !sp->alive || sp->stdout_fd < 0) return -1;
    *out = NULL;

    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) return -1;

    int64_t deadline = now_ms() + timeout_ms;
    while (now_ms() < deadline) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sp->stdout_fd, &rfds);
        struct timeval tv = {0, 50000};
        int ready = select(sp->stdout_fd + 1, &rfds, NULL, NULL, &tv);
        if (ready > 0) {
            char ch;
            ssize_t n = read(sp->stdout_fd, &ch, 1);
            if (n <= 0) { sp->alive = false; break; }
            if (ch == '\n') { buf[len] = '\0'; *out = buf; return 0; }
            buf[len++] = ch;
            if (len >= cap - 1) {
                if (cap >= 1024 * 1024) { buf[len] = '\0'; *out = buf; return 0; }
                cap *= 2;
                char *nb = realloc(buf, cap);
                if (!nb) { buf[len] = '\0'; *out = buf; return 0; }
                buf = nb;
            }
        }
    }

    if (len > 0) { buf[len] = '\0'; *out = buf; return 0; }
    free(buf);
    return -1;
}

void process_spawn_close(SpawnedProcess *sp) {
    if (!sp) return;
    if (sp->stdin_fd >= 0) { close(sp->stdin_fd); sp->stdin_fd = -1; }
    if (sp->stdout_fd >= 0) { close(sp->stdout_fd); sp->stdout_fd = -1; }
    if (sp->pid > 0 && sp->alive) {
        kill(sp->pid, SIGTERM);
        int64_t deadline = now_ms() + 2000;
        while (now_ms() < deadline) {
            int status;
            pid_t r = waitpid(sp->pid, &status, WNOHANG);
            if (r > 0) { sp->alive = false; return; }
            usleep(50000);
        }
        kill(sp->pid, SIGKILL);
        int status;
        waitpid(sp->pid, &status, 0);
    }
    sp->alive = false;
}
