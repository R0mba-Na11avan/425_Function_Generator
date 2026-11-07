/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: tiny test framework for the host tests. TEST(name) registers a
             test, CHECK* macros record failures and keep going
**************************************************************************/

#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <math.h>

typedef void (*TestFn)();

void registerTest(const char* name, TestFn fn);
void testFailed(const char* file, int line, const char* message);

struct TestRegistrar {
    TestRegistrar(const char* name, TestFn fn) { registerTest(name, fn); }
};

#define TEST(name) \
    static void name(); \
    static TestRegistrar name##_registrar(#name, name); \
    static void name()

#define CHECK(cond) \
    do { if (!(cond)) testFailed(__FILE__, __LINE__, "CHECK(" #cond ")"); } while (0)

#define CHECK_EQ(a, b) \
    do { \
        long long _a = (long long)(a), _b = (long long)(b); \
        if (_a != _b) { \
            char _msg[256]; \
            snprintf(_msg, sizeof(_msg), "CHECK_EQ(%s, %s): %lld != %lld", #a, #b, _a, _b); \
            testFailed(__FILE__, __LINE__, _msg); \
        } \
    } while (0)

#define CHECK_NEAR(a, b, eps) \
    do { \
        double _a = (double)(a), _b = (double)(b); \
        if (fabs(_a - _b) > (eps)) { \
            char _msg[256]; \
            snprintf(_msg, sizeof(_msg), "CHECK_NEAR(%s, %s): %g != %g", #a, #b, _a, _b); \
            testFailed(__FILE__, __LINE__, _msg); \
        } \
    } while (0)

#endif
