/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: single entry point for changing the function generator.
             the LCD UI, BLE and MCP server all go through this class and
             never touch FunctionGenerator directly. it serializes access
             and notifies listeners when the state changes.
**************************************************************************/

/*
    Channels
        - one controller drives both channels (CHANNEL_1, CHANNEL_2 from
          funcgen_state.h). every setter takes the channel first
        - one lock for both: the channels share the generator SPI bus, and
          the AD9833 driver pulls its FSYNC low before it takes the bus, so
          two tasks talking to different channels at once could hand one
          chip the other's word

    Mirror (LINK)
        - setMirror(true) copies CH1's waveform, frequency, phase, amplitude
          and offset to CH2, and from then on every change to one of them on
          CH1 is copied to CH2 under the same lock. outputEnabled stays per
          channel
        - while it is on, those settings are locked on CH2: their setters
          return false and change nothing (whoever asked, the LCD greys them
          out, MCP and the web API report an error). setMirror(false) never
          moves anything, CH2 keeps the values it had
        - the copy keeps the peak rule on every step: CH2's amplitude only
          drops before the offset moves, and rises after it
        - CH2's state carries the flag (FuncGenState::mirrored), so listeners
          see it change through the normal CH2 notification

    Listeners get the channel that changed. A change on CH1 while mirrored
    notifies CH1, then CH2, both with the same source.
*/

#ifndef FUNCGEN_CONTROLLER_H
#define FUNCGEN_CONTROLLER_H

#include "func_gen.h"
#include "funcgen_state.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static_assert(CHANNEL_COUNT == 2, "the mirror (CH2 follows CH1) is written for two channels");

class FuncGenController {
    public:
        using Listener = void (*)(uint8_t channel, const FuncGenState& state, CHANGE_SOURCE source, void* ctx);

        static const uint8_t MAX_LISTENERS = 6;

        FuncGenController(FunctionGenerator& ch1, FunctionGenerator& ch2);

        //false (and nothing changes) if channel is out of range, or it is CH2
        //and the mirror locks the setting
        bool setWaveform(uint8_t channel, WAVEFORM waveform, CHANGE_SOURCE source = CHANGE_SOURCE::LOCAL);
        bool setFrequency(uint8_t channel, float frequency, CHANGE_SOURCE source = CHANGE_SOURCE::LOCAL);
        bool setPhase(uint8_t channel, float phase, CHANGE_SOURCE source = CHANGE_SOURCE::LOCAL);
        bool setAmplitude(uint8_t channel, float amplitude, CHANGE_SOURCE source = CHANGE_SOURCE::LOCAL);
        bool setDCOffset(uint8_t channel, float dcOffset, CHANGE_SOURCE source = CHANGE_SOURCE::LOCAL);
        //never locked by the mirror. false only for a bad channel
        bool setOutputEnabled(uint8_t channel, bool enabled, CHANGE_SOURCE source = CHANGE_SOURCE::LOCAL);

        //on: CH2 copies CH1 right away and follows it (see above)
        void setMirror(bool on, CHANGE_SOURCE source = CHANGE_SOURCE::LOCAL);
        bool mirror();

        //channel out of range reads CH1
        FuncGenState state(uint8_t channel);

        //constant and the same for both channels, so no lock is needed
        FuncGenLimits limits() const;

        //returns false if the listener table is full
        bool addListener(Listener listener, void* ctx = nullptr);

    private:
        //runs apply on the channel under the lock, copies it to CH2 if the
        //mirror is on, then notifies. followed: a setting the mirror copies
        template <typename Apply>
        bool change(uint8_t channel, bool followed, CHANGE_SOURCE source, Apply apply);

        //copies CH1's mirrored settings to CH2. lock held
        void syncMirror();

        //calls listeners outside the lock so they can read state() safely
        void notify(uint8_t channel, CHANGE_SOURCE source);
        FuncGenState snapshot(uint8_t channel) const;

        FunctionGenerator* _gen[CHANNEL_COUNT];
        bool _mirror;
        SemaphoreHandle_t _lock;

        Listener _listeners[MAX_LISTENERS];
        void* _listenerCtx[MAX_LISTENERS];
        uint8_t _listenerCount;
};

#endif
