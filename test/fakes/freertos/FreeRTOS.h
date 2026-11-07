/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: host-test stand-in for the FreeRTOS types src/core uses
**************************************************************************/

#ifndef FAKE_FREERTOS_H
#define FAKE_FREERTOS_H

#include <stdint.h>

typedef uint32_t TickType_t;
typedef int BaseType_t;

#define portMAX_DELAY ((TickType_t)0xffffffffUL)
#define pdTRUE 1
#define pdFALSE 0

#endif
