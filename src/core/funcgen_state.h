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

//who asked for a change. listeners use it to show remote changes (e.g. the LCD
//flashes a field an agent changed)
enum class CHANGE_SOURCE : uint8_t {
    LOCAL,  //LCD, encoder, touch
    MCP,
    BLE
};

//every valid range, in one struct so the LCD, MCP schemas and get_limits
//all read the same numbers
struct FuncGenLimits {
    ValueRange frequency;                   //Hz
    ValueRange phase;                       //degrees, max is exclusive (wraps to 0)
    ValueRange amplitude[WAVEFORM_COUNT];   //Vpp, indexed by (uint8_t)WAVEFORM. widest range
    ValueRange dcOffset;                    //V. widest range
    float outputPeak;                       //V. amplitude / 2 + |offset| must stay at or below it

    //what a change may set right now, with the other setting held where it is
    //(the peak rule, see func_gen.h). use these to check or clamp a new value
    ValueRange amplitudeFor(const FuncGenState& state) const {
        return peakLimitedAmplitude(amplitude[(uint8_t)state.waveform], state.dcOffset, outputPeak);
    }
    ValueRange dcOffsetFor(const FuncGenState& state) const {
        return peakLimitedOffset(dcOffset, state.amplitude, outputPeak);
    }
};

#endif
