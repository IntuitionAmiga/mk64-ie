#ifndef TEST_SUPPORT_H
#define TEST_SUPPORT_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_failures = 0;

#define EXPECT_TRUE(cond)                                                                 \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                        \
            test_failures++;                                                              \
        }                                                                                 \
    } while (0)

#define EXPECT_EQ_INT(actual, expected)                                                   \
    do {                                                                                  \
        long long a_ = (long long) (actual);                                              \
        long long e_ = (long long) (expected);                                            \
        if (a_ != e_) {                                                                   \
            printf("FAIL %s:%d: %s == %lld, expected %lld\n", __FILE__, __LINE__,         \
                   #actual, a_, e_);                                                      \
            test_failures++;                                                              \
        }                                                                                 \
    } while (0)

#define EXPECT_NEAR(actual, expected, tol)                                                \
    do {                                                                                  \
        double a_ = (actual);                                                             \
        double e_ = (expected);                                                           \
        if (!(fabs(a_ - e_) <= (tol))) {                                                  \
            printf("FAIL %s:%d: %s == %g, expected %g (tol %g)\n", __FILE__, __LINE__,    \
                   #actual, a_, e_, (double) (tol));                                      \
            test_failures++;                                                              \
        }                                                                                 \
    } while (0)

#define EXPECT_MAT4_NEAR(actual, expected, tol)                                           \
    do {                                                                                  \
        for (int r_ = 0; r_ < 4; r_++)                                                    \
            for (int c_ = 0; c_ < 4; c_++)                                                \
                EXPECT_NEAR((actual)[r_][c_], (expected)[r_][c_], (tol));                 \
    } while (0)

static inline int test_finish(const char* name) {
    if (test_failures == 0) {
        printf("%s: OK\n", name);
        return 0;
    }
    printf("%s: %d failure(s)\n", name, test_failures);
    return 1;
}

#endif /* TEST_SUPPORT_H */
