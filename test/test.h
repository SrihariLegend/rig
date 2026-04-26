#ifndef PI_TEST_H
#define PI_TEST_H

#include <stdio.h>
#include <string.h>
#include <math.h>

static int _tests_run = 0;
static int _tests_passed = 0;
static int _tests_failed = 0;

#define TEST(name) static void test_##name(void)

#define RUN_TEST(name) do { \
    _tests_run++; \
    printf("  %-50s ", #name); \
    test_##name(); \
    _tests_passed++; \
    printf("PASS\n"); \
} while(0)

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        _tests_passed--; _tests_failed++; \
        printf("FAIL\n    %s:%d: ASSERT_TRUE(%s)\n", __FILE__, __LINE__, #expr); \
        return; \
    } \
} while(0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_EQ(a, b) do { \
    long long _a = (long long)(a); long long _b = (long long)(b); \
    if (_a != _b) { \
        _tests_passed--; _tests_failed++; \
        printf("FAIL\n    %s:%d: ASSERT_EQ(%s, %s) => %lld != %lld\n", \
               __FILE__, __LINE__, #a, #b, _a, _b); \
        return; \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    const char *_a = (a); const char *_b = (b); \
    if (_a == NULL || _b == NULL || strcmp(_a, _b) != 0) { \
        _tests_passed--; _tests_failed++; \
        printf("FAIL\n    %s:%d: ASSERT_STR_EQ(%s, %s) => \"%s\" != \"%s\"\n", \
               __FILE__, __LINE__, #a, #b, _a ? _a : "NULL", _b ? _b : "NULL"); \
        return; \
    } \
} while(0)

#define ASSERT_NULL(p) ASSERT_TRUE((p) == NULL)
#define ASSERT_NOT_NULL(p) ASSERT_TRUE((p) != NULL)

#define ASSERT_FLOAT_EQ(a, b, eps) do { \
    if (fabs((double)(a) - (double)(b)) > (eps)) { \
        _tests_passed--; _tests_failed++; \
        printf("FAIL\n    %s:%d: ASSERT_FLOAT_EQ => %f != %f\n", \
               __FILE__, __LINE__, (double)(a), (double)(b)); \
        return; \
    } \
} while(0)

#define TEST_SUITE(name) printf("\n=== %s ===\n", name)

#define TEST_REPORT() do { \
    printf("\n--- Results: %d passed, %d failed, %d total ---\n", \
           _tests_passed, _tests_failed, _tests_run); \
    return _tests_failed > 0 ? 1 : 0; \
} while(0)

#endif
