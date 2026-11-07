/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: runs every registered host test and returns non-zero on failure
**************************************************************************/

#include "test.h"
#include "fakes/SPI.h"
#include "fakes/freertos/semphr.h"

static const int MAX_TESTS = 256;

static const char* testNames[MAX_TESTS];
static TestFn testFns[MAX_TESTS];
static int testCount = 0;
static int currentFailures = 0;

void registerTest(const char* name, TestFn fn) {
    if (testCount < MAX_TESTS) {
        testNames[testCount] = name;
        testFns[testCount] = fn;
        testCount++;
    }
}

void testFailed(const char* file, int line, const char* message) {
    printf("    %s:%d: %s\n", file, line, message);
    currentFailures++;
}

int main() {
    int failedTests = 0;
    for (int i = 0; i < testCount; i++) {
        fakeResetPins();
        SPI.reset();
        fakeResetMutexStats();
        currentFailures = 0;
        testFns[i]();
        printf("[%s] %s\n", currentFailures ? "FAIL" : " ok ", testNames[i]);
        if (currentFailures) failedTests++;
    }
    printf("\n%d/%d tests passed\n", testCount - failedTests, testCount);
    return failedTests ? 1 : 0;
}
