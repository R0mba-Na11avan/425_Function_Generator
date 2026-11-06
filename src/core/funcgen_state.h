/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: snapshot of the function generator settings, shared by the
             controller and every interface (LCD, BLE, MCP, ...)
**************************************************************************/

#ifndef FUNCGEN_STATE_H
#define FUNCGEN_STATE_H

#include "func_gen.h"

struct FuncGenState {
    WAVEFORM waveform;
    float frequency;
    float phase;
    float amplitude;
    float dcOffset;
    bool outputEnabled;
};

#endif
