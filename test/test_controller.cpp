/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: host tests for FuncGenController: locking, listeners, limits,
             both channels and the mirror (CH2 follows CH1)
**************************************************************************/

#include "test.h"
#include "fakes/SPI.h"
#include "fakes/freertos/semphr.h"
#include "../src/core/funcgen_controller.h"

struct ListenerLog {
    int calls = 0;
    int callsPerChannel[CHANNEL_COUNT] = {};
    FuncGenState last;
    FuncGenState lastPerChannel[CHANNEL_COUNT];
    uint8_t lastChannel = 0xFF;
    FuncGenController* controller = nullptr;
    int heldDuringCall = 0; //calls that ran with a mutex still held
    CHANGE_SOURCE lastSource = CHANGE_SOURCE::BLE;
};

static void recordListener(uint8_t channel, const FuncGenState& state, CHANGE_SOURCE source, void* ctx) {
    ListenerLog* log = (ListenerLog*)ctx;
    log->calls++;
    if (channel < CHANNEL_COUNT) {
        log->callsPerChannel[channel]++;
        log->lastPerChannel[channel] = state;
    }
    log->last = state;
    log->lastChannel = channel;
    log->lastSource = source;
    if (fakeMutexesHeld()) log->heldDuringCall++;
    //listeners may read state() (README: "calls listeners outside the lock")
    if (log->controller) log->controller->state(channel);
}

static void noopListener(uint8_t, const FuncGenState&, CHANGE_SOURCE, void*) {}

//both generators + controller, already begun
struct Rig {
    FunctionGenerator gen;
    FunctionGenerator gen2;
    FuncGenController ctl;
    Rig() : gen(PIN_AD9833_CS, PIN_AMP_DAC_CS, PIN_OFFSET_DAC_CS, OFFSET_DAC_CHANNEL, PIN_WAVE_SWITCH, PIN_OUTPUT_RELAY),
            gen2(PIN_AD9833_CS_2, PIN_AMP_DAC_CS_2, PIN_OFFSET_DAC_CS, OFFSET_DAC_CHANNEL_2, PIN_WAVE_SWITCH_2, PIN_OUTPUT_RELAY_2),
            ctl(gen, gen2) {
        gen.begin();
        gen2.begin();
    }
};

TEST(listener_gets_applied_state) {
    Rig rig;
    ListenerLog log;
    CHECK(rig.ctl.addListener(recordListener, &log));

    CHECK(rig.ctl.setFrequency(CHANNEL_1, 99e6));
    CHECK_EQ(log.calls, 1);
    CHECK_EQ(log.lastChannel, CHANNEL_1);
    CHECK_NEAR(log.last.frequency, FREQ_MAX_HZ, 1e-3); //clamped value, not the request

    rig.ctl.setWaveform(CHANNEL_1, WAVEFORM::TRIANGLE);
    CHECK(log.last.waveform == WAVEFORM::TRIANGLE);

    rig.ctl.setAmplitude(CHANNEL_1, -3);
    CHECK_NEAR(log.last.amplitude, FunctionGenerator::amplitudeRange(WAVEFORM::TRIANGLE).min, 1e-6);

    rig.ctl.setPhase(CHANNEL_1, 450);
    CHECK_NEAR(log.last.phase, 90, 1e-4);

    rig.ctl.setDCOffset(CHANNEL_1, 1.25);
    CHECK_NEAR(log.last.dcOffset, 1.25, 1e-6);

    rig.ctl.setOutputEnabled(CHANNEL_1, true);
    CHECK(log.last.outputEnabled);
    CHECK_EQ(log.calls, 6);
}

TEST(every_setter_notifies_all_listeners) {
    Rig rig;
    ListenerLog a, b;
    rig.ctl.addListener(recordListener, &a);
    rig.ctl.addListener(recordListener, &b);
    rig.ctl.setWaveform(CHANNEL_1, WAVEFORM::SQUARE);
    rig.ctl.setFrequency(CHANNEL_1, 10);
    rig.ctl.setPhase(CHANNEL_1, 10);
    rig.ctl.setAmplitude(CHANNEL_1, 1);
    rig.ctl.setDCOffset(CHANNEL_1, 0);
    rig.ctl.setOutputEnabled(CHANNEL_1, false);
    CHECK_EQ(a.calls, 6);
    CHECK_EQ(b.calls, 6);
}

TEST(listener_can_read_state_without_deadlock) {
    Rig rig;
    ListenerLog log;
    log.controller = &rig.ctl;
    rig.ctl.addListener(recordListener, &log);

    rig.ctl.setFrequency(CHANNEL_1, 5000);
    rig.ctl.setOutputEnabled(CHANNEL_1, true);
    rig.ctl.setMirror(true);
    rig.ctl.setFrequency(CHANNEL_1, 6000);
    CHECK_EQ(log.calls, 5); //the last change notifies both channels
    CHECK_EQ(log.heldDuringCall, 0);   //listeners run outside the lock
    CHECK_EQ(fakeMutexDeadlocks(), 0); //the fake counts a take while held as a deadlock
    CHECK_EQ(fakeMutexesHeld(), 0);

    //state() after the listeners ran must still work, i.e. the lock was given back
    FuncGenState s = rig.ctl.state(CHANNEL_1);
    CHECK_NEAR(s.frequency, 6000, 1e-3);
    CHECK(s.outputEnabled);
    CHECK(rig.ctl.mirror());
}

TEST(listener_table_fills_up) {
    Rig rig;
    for (uint8_t i = 0; i < FuncGenController::MAX_LISTENERS; i++) {
        CHECK(rig.ctl.addListener(noopListener));
    }
    CHECK(!rig.ctl.addListener(noopListener));
}

TEST(limits_match_function_generator) {
    Rig rig;
    FuncGenLimits l = rig.ctl.limits();
    CHECK_NEAR(l.frequency.min, FunctionGenerator::frequencyRange().min, 0);
    CHECK_NEAR(l.frequency.max, FunctionGenerator::frequencyRange().max, 0);
    CHECK_NEAR(l.phase.max, FunctionGenerator::phaseRange().max, 0);
    CHECK_NEAR(l.dcOffset.min, FunctionGenerator::dcOffsetRange().min, 0);
    CHECK_NEAR(l.dcOffset.max, FunctionGenerator::dcOffsetRange().max, 0);
    CHECK_NEAR(l.outputPeak, FunctionGenerator::outputPeakMax(), 0);
    for (uint8_t i = 0; i < WAVEFORM_COUNT; i++) {
        ValueRange r = FunctionGenerator::amplitudeRange((WAVEFORM)i);
        CHECK_NEAR(l.amplitude[i].min, r.min, 0);
        CHECK_NEAR(l.amplitude[i].max, r.max, 0);
    }
}

TEST(state_reflects_generator_after_setters) {
    Rig rig;
    rig.ctl.setWaveform(CHANNEL_1, WAVEFORM::SQUARE);
    rig.ctl.setFrequency(CHANNEL_1, 2500);
    rig.ctl.setOutputEnabled(CHANNEL_1, true);
    FuncGenState s = rig.ctl.state(CHANNEL_1);
    CHECK(s.waveform == WAVEFORM::SQUARE);
    CHECK_NEAR(s.frequency, 2500, 1e-3);
    CHECK(s.outputEnabled);
    CHECK_NEAR(s.amplitude, rig.gen.amplitude(), 0);
}

TEST(listener_gets_change_source) {
    Rig rig;
    ListenerLog log;
    rig.ctl.addListener(recordListener, &log);
    rig.ctl.setFrequency(CHANNEL_1, 500);
    CHECK(log.lastSource == CHANGE_SOURCE::LOCAL); //default
    rig.ctl.setAmplitude(CHANNEL_1, 1.0f, CHANGE_SOURCE::MCP);
    CHECK(log.lastSource == CHANGE_SOURCE::MCP);
    rig.ctl.setWaveform(CHANNEL_1, WAVEFORM::SQUARE, CHANGE_SOURCE::BLE);
    CHECK(log.lastSource == CHANGE_SOURCE::BLE);
    rig.ctl.setOutputEnabled(CHANNEL_1, true, CHANGE_SOURCE::MCP);
    CHECK(log.lastSource == CHANGE_SOURCE::MCP);
    rig.ctl.setPhase(CHANNEL_1, 10);
    CHECK(log.lastSource == CHANGE_SOURCE::LOCAL);
    rig.ctl.setDCOffset(CHANNEL_1, 0.5f, CHANGE_SOURCE::MCP);
    CHECK(log.lastSource == CHANGE_SOURCE::MCP);
    rig.ctl.setMirror(true, CHANGE_SOURCE::WEB);
    CHECK(log.lastSource == CHANGE_SOURCE::WEB);
}

//channels

TEST(channels_are_independent) {
    Rig rig;
    ListenerLog log;
    rig.ctl.addListener(recordListener, &log);

    CHECK(rig.ctl.setFrequency(CHANNEL_2, 1234));
    CHECK(rig.ctl.setWaveform(CHANNEL_2, WAVEFORM::TRIANGLE));
    CHECK(rig.ctl.setAmplitude(CHANNEL_2, 3.0f));
    CHECK(rig.ctl.setDCOffset(CHANNEL_2, -1.0f));
    CHECK(rig.ctl.setOutputEnabled(CHANNEL_2, true));
    CHECK_EQ(log.callsPerChannel[CHANNEL_2], 5);
    CHECK_EQ(log.callsPerChannel[CHANNEL_1], 0);

    FuncGenState s2 = rig.ctl.state(CHANNEL_2);
    CHECK_NEAR(s2.frequency, 1234, 1e-3);
    CHECK(s2.waveform == WAVEFORM::TRIANGLE);
    CHECK_NEAR(s2.amplitude, 3.0, 1e-6);
    CHECK_NEAR(s2.dcOffset, -1.0, 1e-6);
    CHECK(s2.outputEnabled);
    CHECK_NEAR(rig.gen2.frequency(), 1234, 1e-3);
    CHECK(rig.gen2.outputOn());

    //CH1 still at its power-on values
    FuncGenState s1 = rig.ctl.state(CHANNEL_1);
    CHECK_NEAR(s1.frequency, 1.0, 1e-6);
    CHECK(s1.waveform == WAVEFORM::SINE);
    CHECK(!s1.outputEnabled);
    CHECK(!rig.gen.outputOn());
}

TEST(bad_channel_is_refused) {
    Rig rig;
    ListenerLog log;
    rig.ctl.addListener(recordListener, &log);
    CHECK(!rig.ctl.setFrequency(CHANNEL_COUNT, 1000));
    CHECK(!rig.ctl.setOutputEnabled(7, true));
    CHECK_EQ(log.calls, 0);
    CHECK(!rig.gen.outputOn());
    CHECK(!rig.gen2.outputOn());
    //an out-of-range read gives CH1 instead of reading past the table
    CHECK_NEAR(rig.ctl.state(9).frequency, rig.gen.frequency(), 0);
}

//mirror

TEST(mirror_copies_ch1_and_keeps_outputs_separate) {
    Rig rig;
    rig.ctl.setWaveform(CHANNEL_1, WAVEFORM::SQUARE);
    rig.ctl.setFrequency(CHANNEL_1, 4321);
    rig.ctl.setPhase(CHANNEL_1, 45);
    rig.ctl.setAmplitude(CHANNEL_1, 4.0f);
    rig.ctl.setDCOffset(CHANNEL_1, 2.0f);
    rig.ctl.setOutputEnabled(CHANNEL_1, true);

    ListenerLog log;
    rig.ctl.addListener(recordListener, &log);
    CHECK(!rig.ctl.mirror());
    CHECK(!rig.ctl.state(CHANNEL_2).mirrored);
    rig.ctl.setMirror(true);
    CHECK(rig.ctl.mirror());
    CHECK_EQ(log.callsPerChannel[CHANNEL_2], 1);
    CHECK(log.lastPerChannel[CHANNEL_2].mirrored);

    FuncGenState s2 = rig.ctl.state(CHANNEL_2);
    CHECK(s2.mirrored);
    CHECK(!rig.ctl.state(CHANNEL_1).mirrored);
    CHECK(s2.waveform == WAVEFORM::SQUARE);
    CHECK_NEAR(s2.frequency, 4321, 1e-3);
    CHECK_NEAR(s2.phase, 45, 0.1);
    CHECK_NEAR(s2.amplitude, 4.0, 1e-6);
    CHECK_NEAR(s2.dcOffset, 2.0, 1e-6);
    CHECK(!s2.outputEnabled); //not copied
    CHECK(!rig.gen2.outputOn());
}

TEST(mirror_follows_every_ch1_change) {
    Rig rig;
    rig.ctl.setMirror(true);
    ListenerLog log;
    rig.ctl.addListener(recordListener, &log);

    rig.ctl.setFrequency(CHANNEL_1, 777, CHANGE_SOURCE::MCP);
    CHECK_EQ(log.callsPerChannel[CHANNEL_1], 1);
    CHECK_EQ(log.callsPerChannel[CHANNEL_2], 1);
    CHECK(log.lastChannel == CHANNEL_2);    //CH1 first, then CH2
    CHECK(log.lastSource == CHANGE_SOURCE::MCP);
    CHECK_NEAR(rig.gen2.frequency(), 777, 1e-3);

    rig.ctl.setWaveform(CHANNEL_1, WAVEFORM::TRIANGLE);
    rig.ctl.setAmplitude(CHANNEL_1, 6.0f);
    rig.ctl.setDCOffset(CHANNEL_1, -3.0f);
    rig.ctl.setPhase(CHANNEL_1, 120);
    CHECK(rig.gen2.waveform() == WAVEFORM::TRIANGLE);
    CHECK_NEAR(rig.gen2.amplitude(), 6.0, 1e-6);
    CHECK_NEAR(rig.gen2.DCOffset(), -3.0, 1e-6);
    CHECK_NEAR(rig.gen2.phase(), 120, 0.1);

    //the output is never copied, CH1's output change doesn't touch CH2
    int before = log.callsPerChannel[CHANNEL_2];
    rig.ctl.setOutputEnabled(CHANNEL_1, true);
    CHECK_EQ(log.callsPerChannel[CHANNEL_2], before);
    CHECK(!rig.gen2.outputOn());
}

TEST(mirror_locks_ch2_but_not_its_output) {
    Rig rig;
    rig.ctl.setMirror(true);
    ListenerLog log;
    rig.ctl.addListener(recordListener, &log);

    CHECK(!rig.ctl.setFrequency(CHANNEL_2, 5000, CHANGE_SOURCE::MCP));
    CHECK(!rig.ctl.setWaveform(CHANNEL_2, WAVEFORM::SQUARE));
    CHECK(!rig.ctl.setAmplitude(CHANNEL_2, 2.0f));
    CHECK(!rig.ctl.setDCOffset(CHANNEL_2, 1.0f));
    CHECK(!rig.ctl.setPhase(CHANNEL_2, 90));
    CHECK_EQ(log.calls, 0);
    CHECK_NEAR(rig.gen2.frequency(), rig.gen.frequency(), 0);
    CHECK(rig.gen2.waveform() == rig.gen.waveform());

    CHECK(rig.ctl.setOutputEnabled(CHANNEL_2, true));
    CHECK(rig.gen2.outputOn());
    CHECK(!rig.gen.outputOn());

    //off: CH2 keeps what it had and is free again
    rig.ctl.setFrequency(CHANNEL_1, 2222);
    rig.ctl.setMirror(false);
    CHECK(!rig.ctl.state(CHANNEL_2).mirrored);
    CHECK_NEAR(rig.gen2.frequency(), 2222, 1e-3);
    CHECK(rig.ctl.setFrequency(CHANNEL_2, 3333));
    CHECK_NEAR(rig.gen2.frequency(), 3333, 1e-3);
    CHECK_NEAR(rig.gen.frequency(), 2222, 1e-3);
}

TEST(mirror_copy_keeps_the_peak_rule) {
    float peak = FunctionGenerator::outputPeakMax();
    //CH2 at a big offset, CH1 at a big amplitude: copying the amplitude
    //before the offset would be clamped by CH2's old offset
    {
        Rig rig;
        rig.ctl.setAmplitude(CHANNEL_2, 0.0f);
        rig.ctl.setDCOffset(CHANNEL_2, FunctionGenerator::dcOffsetRange().min);
        rig.ctl.setDCOffset(CHANNEL_1, 0.0f);
        float bigAmp = FunctionGenerator::amplitudeRange(WAVEFORM::SINE).max;
        rig.ctl.setAmplitude(CHANNEL_1, bigAmp);
        rig.ctl.setMirror(true);
        CHECK_NEAR(rig.gen2.amplitude(), bigAmp, 1e-6);
        CHECK_NEAR(rig.gen2.DCOffset(), 0.0, 1e-6);
    }
    //the other way round: CH2 at a big amplitude, CH1 at a big offset
    {
        Rig rig;
        rig.ctl.setAmplitude(CHANNEL_2, FunctionGenerator::amplitudeRange(WAVEFORM::SINE).max);
        rig.ctl.setAmplitude(CHANNEL_1, 1.0f);
        float bigOffset = FunctionGenerator::dcOffsetRange(1.0f).min;
        rig.ctl.setDCOffset(CHANNEL_1, bigOffset);
        rig.ctl.setMirror(true);
        CHECK_NEAR(rig.gen2.amplitude(), 1.0, 1e-6);
        CHECK_NEAR(rig.gen2.DCOffset(), bigOffset, 1e-6);
        CHECK(rig.gen2.amplitude() / 2 + fabsf(rig.gen2.DCOffset()) <= peak + 1e-4f);
    }
}
