/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: single entry point for changing the function generator.
             the LCD UI, BLE and MCP server all go through this class and
             never touch FunctionGenerator directly. it serializes access
             and notifies listeners when the state changes.
**************************************************************************/

#ifndef FUNCGEN_CONTROLLER_H
#define FUNCGEN_CONTROLLER_H

#include "func_gen.h"
#include "funcgen_state.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class FuncGenController {
    public:
        using Listener = void (*)(const FuncGenState& state, CHANGE_SOURCE source, void* ctx);

        static const uint8_t MAX_LISTENERS = 6;

        FuncGenController(FunctionGenerator& funcGen);

        void setWaveform(WAVEFORM waveform, CHANGE_SOURCE source = CHANGE_SOURCE::LOCAL);
        void setFrequency(float frequency, CHANGE_SOURCE source = CHANGE_SOURCE::LOCAL);
        void setPhase(float phase, CHANGE_SOURCE source = CHANGE_SOURCE::LOCAL);
        void setAmplitude(float amplitude, CHANGE_SOURCE source = CHANGE_SOURCE::LOCAL);
        void setDCOffset(float dcOffset, CHANGE_SOURCE source = CHANGE_SOURCE::LOCAL);
        void setOutputEnabled(bool enabled, CHANGE_SOURCE source = CHANGE_SOURCE::LOCAL);

        FuncGenState state();

        //constant, so no lock is needed
        FuncGenLimits limits() const;

        //returns false if the listener table is full
        bool addListener(Listener listener, void* ctx = nullptr);

    private:

        //calls listeners outside the lock so they can read state() safely
        void notify(CHANGE_SOURCE source);
        FuncGenState snapshot() const;

        FunctionGenerator& _funcGen;
        SemaphoreHandle_t _lock;

        Listener _listeners[MAX_LISTENERS];
        void* _listenerCtx[MAX_LISTENERS];
        uint8_t _listenerCount;
};

#endif
