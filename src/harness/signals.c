/* signals.c — signal handling with sigaction, double-SIGINT force exit */
#include "harness/signals.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Global pointer for signal handlers (async-signal-safe access) */
static volatile SignalState *g_signal_state = NULL;

static void handle_sigint(int signum) {
    (void)signum;
    volatile SignalState *ss = g_signal_state;
    if (!ss) return;

    ss->interrupt_count++;
    ss->interrupted = 1;

    /* Double SIGINT: force exit */
    if (ss->interrupt_count >= 2) {
        _exit(130);
    }
}

static void handle_sigterm(int signum) {
    (void)signum;
    volatile SignalState *ss = g_signal_state;
    if (!ss) return;
    ss->terminated = 1;
}

static void handle_sigwinch(int signum) {
    (void)signum;
    volatile SignalState *ss = g_signal_state;
    if (!ss) return;
    ss->resized = 1;
}

SignalState *signals_init(void) {
    SignalState *ss = calloc(1, sizeof(SignalState));
    if (!ss) return NULL;
    ss->interrupted = 0;
    ss->terminated = 0;
    ss->resized = 0;
    ss->interrupt_count = 0;
    ss->on_interrupt = NULL;
    ss->on_terminate = NULL;
    ss->on_resize = NULL;
    ss->ctx = NULL;
    return ss;
}

void signals_cleanup(SignalState *ss) {
    if (!ss) return;

    /* Restore default handlers if this was the active state */
    if (g_signal_state == ss) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);

        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);
        sigaction(SIGWINCH, &sa, NULL);

        g_signal_state = NULL;
    }

    free(ss);
}

void signals_install(SignalState *ss) {
    if (!ss) return;

    g_signal_state = ss;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sa.sa_handler = handle_sigint;
    sigaction(SIGINT, &sa, NULL);

    sa.sa_handler = handle_sigterm;
    sigaction(SIGTERM, &sa, NULL);

    sa.sa_handler = handle_sigwinch;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sa, NULL);
}

bool signals_check_interrupt(SignalState *ss) {
    if (!ss) return false;
    return ss->interrupted != 0;
}

bool signals_check_terminate(SignalState *ss) {
    if (!ss) return false;
    return ss->terminated != 0;
}

bool signals_check_resize(SignalState *ss) {
    if (!ss) return false;
    return ss->resized != 0;
}

void signals_clear_interrupt(SignalState *ss) {
    if (!ss) return;
    ss->interrupted = 0;
    ss->interrupt_count = 0;
}

void signals_clear_resize(SignalState *ss) {
    if (!ss) return;
    ss->resized = 0;
}
