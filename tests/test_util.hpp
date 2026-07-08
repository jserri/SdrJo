#pragma once
// Micro-framework di test: CHECK con report e conteggio fallimenti.
#include <cmath>
#include <cstdio>

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                              \
    do {                                                                   \
        double va = (a), vb = (b);                                         \
        if (std::fabs(va - vb) > (tol)) {                                  \
            std::printf("FAIL %s:%d: %s=%g atteso %g (tol %g)\n",          \
                        __FILE__, __LINE__, #a, va, vb, double(tol));      \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

static inline int testResult(const char* name)
{
    if (g_failures == 0) {
        std::printf("%s: OK\n", name);
        return 0;
    }
    std::printf("%s: %d test falliti\n", name, g_failures);
    return 1;
}
