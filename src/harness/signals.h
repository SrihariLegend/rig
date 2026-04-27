#ifndef RIG_HARNESS_SIGNALS_H
#define RIG_HARNESS_SIGNALS_H

#include <signal.h>
#include <stdbool.h>

typedef void (*SignalCallback)(int signum, void *ctx);

typedef struct {
    SignalCallback on_interrupt;   /* SIGINT -- abort current gen */
    SignalCallback on_terminate;   /* SIGTERM -- graceful shutdown */
    SignalCallback on_resize;      /* SIGWINCH -- terminal resize */
    void *ctx;
    volatile sig_atomic_t interrupted;
    volatile sig_atomic_t terminated;
    volatile sig_atomic_t resized;
    volatile sig_atomic_t interrupt_count;
} SignalState;

SignalState *signals_init(void);
void signals_cleanup(SignalState *ss);

/* Install handlers */
void signals_install(SignalState *ss);

/* Check flags (call from main loop) */
bool signals_check_interrupt(SignalState *ss);
bool signals_check_terminate(SignalState *ss);
bool signals_check_resize(SignalState *ss);

/* Reset flag after handling */
void signals_clear_interrupt(SignalState *ss);
void signals_clear_resize(SignalState *ss);

#endif
