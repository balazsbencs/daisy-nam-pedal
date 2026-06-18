// test_harness.h — minimal CHECK macro for host-side tests (no framework deps).
#pragma once
#include <cstdio>
#include <cstdlib>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond) \
    do { \
        if (cond) { \
            g_pass++; \
        } else { \
            fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            g_fail++; \
        } \
    } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))
#define CHECK_NE(a, b) CHECK((a) != (b))
#define CHECK_STR(a, b) CHECK(std::string(a) == std::string(b))

static int test_summary(const char* suite)
{
    printf("%s: %d passed, %d failed\n", suite, g_pass, g_fail);
    return (g_fail > 0) ? 1 : 0;
}
