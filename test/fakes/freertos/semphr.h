/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: host-test stand-in for FreeRTOS mutexes. single threaded, but it
             counts takes/gives and flags a take on a mutex that is already
             held, which would deadlock on the real (non-recursive) mutex
**************************************************************************/

#ifndef FAKE_SEMPHR_H
#define FAKE_SEMPHR_H

#include "FreeRTOS.h"

struct FakeMutex {
    int held;
    int takes;
    int gives;
    int deadlocks; //takes while already held
};

typedef FakeMutex* SemaphoreHandle_t;

SemaphoreHandle_t xSemaphoreCreateMutex();
BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t timeout);
BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex);

//test inspection, summed over every mutex
int fakeMutexDeadlocks();   //takes on a mutex that was already held
int fakeMutexesHeld();      //currently held (should be 0 between calls)
void fakeResetMutexStats();

#endif
