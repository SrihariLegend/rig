/* test_signals.c — tests for signal handling */
#include "test.h"
#include "harness/signals.h"
#include <stdlib.h>
#include <signal.h>

/* ========== Init/Cleanup ========== */

TEST(signals_init_cleanup) {
    SignalState *ss = signals_init();
    ASSERT_NOT_NULL(ss);
    ASSERT_FALSE(signals_check_interrupt(ss));
    ASSERT_FALSE(signals_check_terminate(ss));
    ASSERT_FALSE(signals_check_resize(ss));
    ASSERT_EQ(ss->interrupted, 0);
    ASSERT_EQ(ss->terminated, 0);
    ASSERT_EQ(ss->resized, 0);
    ASSERT_EQ(ss->interrupt_count, 0);
    signals_cleanup(ss);
}

TEST(signals_install_uninstall) {
    SignalState *ss = signals_init();
    signals_install(ss);
    /* Handlers installed; cleanup restores defaults */
    signals_cleanup(ss);
}

/* ========== Flag check/clear ========== */

TEST(flag_check_clear_interrupt) {
    SignalState *ss = signals_init();
    ASSERT_FALSE(signals_check_interrupt(ss));

    ss->interrupted = 1;
    ss->interrupt_count = 1;
    ASSERT_TRUE(signals_check_interrupt(ss));

    signals_clear_interrupt(ss);
    ASSERT_FALSE(signals_check_interrupt(ss));
    ASSERT_EQ(ss->interrupt_count, 0);

    signals_cleanup(ss);
}

TEST(flag_check_clear_resize) {
    SignalState *ss = signals_init();
    ASSERT_FALSE(signals_check_resize(ss));

    ss->resized = 1;
    ASSERT_TRUE(signals_check_resize(ss));

    signals_clear_resize(ss);
    ASSERT_FALSE(signals_check_resize(ss));

    signals_cleanup(ss);
}

TEST(flag_check_terminate) {
    SignalState *ss = signals_init();
    ASSERT_FALSE(signals_check_terminate(ss));

    ss->terminated = 1;
    ASSERT_TRUE(signals_check_terminate(ss));

    signals_cleanup(ss);
}

/* ========== NULL safety ========== */

TEST(null_safety) {
    ASSERT_FALSE(signals_check_interrupt(NULL));
    ASSERT_FALSE(signals_check_terminate(NULL));
    ASSERT_FALSE(signals_check_resize(NULL));
    signals_clear_interrupt(NULL);
    signals_clear_resize(NULL);
    signals_install(NULL);
    signals_cleanup(NULL);
}

/* ========== Signal delivery (SIGINT via raise) ========== */

TEST(sigint_delivery) {
    SignalState *ss = signals_init();
    signals_install(ss);

    ASSERT_FALSE(signals_check_interrupt(ss));
    raise(SIGINT);
    ASSERT_TRUE(signals_check_interrupt(ss));
    ASSERT_EQ(ss->interrupt_count, 1);

    signals_clear_interrupt(ss);
    ASSERT_FALSE(signals_check_interrupt(ss));

    signals_cleanup(ss);
}

TEST(sigwinch_delivery) {
    SignalState *ss = signals_init();
    signals_install(ss);

    ASSERT_FALSE(signals_check_resize(ss));
    raise(SIGWINCH);
    ASSERT_TRUE(signals_check_resize(ss));

    signals_clear_resize(ss);
    ASSERT_FALSE(signals_check_resize(ss));

    signals_cleanup(ss);
}

/* Note: we do NOT test double-SIGINT delivery because it calls _exit(130) */

TEST(double_interrupt_count) {
    /* Test the counting mechanism without actually triggering _exit */
    SignalState *ss = signals_init();
    ss->interrupted = 1;
    ss->interrupt_count = 1;
    ASSERT_TRUE(signals_check_interrupt(ss));
    ASSERT_EQ(ss->interrupt_count, 1);

    /* Simulating a second interrupt (without going through handler) */
    ss->interrupt_count = 2;
    ASSERT_EQ(ss->interrupt_count, 2);

    /* Clear resets both */
    signals_clear_interrupt(ss);
    ASSERT_EQ(ss->interrupt_count, 0);
    ASSERT_FALSE(signals_check_interrupt(ss));

    signals_cleanup(ss);
}

int main(void) {
    TEST_SUITE("Signals: Init/Cleanup");
    RUN_TEST(signals_init_cleanup);
    RUN_TEST(signals_install_uninstall);

    TEST_SUITE("Signals: Flag Check/Clear");
    RUN_TEST(flag_check_clear_interrupt);
    RUN_TEST(flag_check_clear_resize);
    RUN_TEST(flag_check_terminate);

    TEST_SUITE("Signals: NULL Safety");
    RUN_TEST(null_safety);

    TEST_SUITE("Signals: Signal Delivery");
    RUN_TEST(sigint_delivery);
    RUN_TEST(sigwinch_delivery);
    RUN_TEST(double_interrupt_count);

    TEST_REPORT();
}
