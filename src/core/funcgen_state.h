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

//every valid range, in one struct so the LCD, MCP schemas and get_limits
//all read the same numbers
struct FuncGenLimits {
    ValueRange frequency;                   //Hz
    ValueRange phase;                       //degrees, max is exclusive (wraps to 0)
    ValueRange amplitude[WAVEFORM_COUNT];   //Vpp, indexed by (uint8_t)WAVEFORM
    ValueRange dcOffset;                    //V
};

#endif
