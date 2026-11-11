/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: host tests for FuncGenController: locking, listeners and limits
**************************************************************************/

#include "test.h"
#include "fakes/SPI.h"
#include "fakes/freertos/semphr.h"
#include "../src/core/funcgen_controller.h"

struct ListenerLog {
    int calls = 0;
    FuncGenState last;
    FuncGenController* controller = nullptr;
    int heldDuringCall = 0; //calls that ran with a mutex still held
    CHANGE_SOURCE lastSource = CHANGE_SOURCE::BLE;
};

static void recordListener(const FuncGenState& state, CHANGE_SOURCE source, void* ctx) {
    ListenerLog* log = (ListenerLog*)ctx;
    log->calls++;
    log->last = state;
    log->lastSource = source;
    if (fakeMutexesHeld()) log->heldDuringCall++;
    //listeners may read state() (README: "calls listeners outside the lock")
    if (log->controller) log->controller->state();
}

static void noopListener(const FuncGenState&, CHANGE_SOURCE, void*) {}

//generator + controller, already begun
struct Rig {
    FunctionGenerator gen;
    FuncGenController ctl;
    Rig() : gen(PIN_AD9833_CS, PIN_AMP_DAC_CS, PIN_OFFSET_DAC_CS, PIN_WAVE_SWITCH), ctl(gen) { gen.begin(); }
};

TEST(listener_gets_applied_state) {
    Rig rig;
    ListenerLog log;
    CHECK(rig.ctl.addListener(recordListener, &log));

    rig.ctl.setFrequency(99e6);
    CHECK_EQ(log.calls, 1);
    CHECK_NEAR(log.last.frequency, FREQ_MAX_HZ, 1e-3); //clamped value, not the request

    rig.ctl.setWaveform(WAVEFORM::TRIANGLE);
    CHECK(log.last.waveform == WAVEFORM::TRIANGLE);

    rig.ctl.setAmplitude(-3);
    CHECK_NEAR(log.last.amplitude, FunctionGenerator::amplitudeRange(WAVEFORM::TRIANGLE).min, 1e-6);

    rig.ctl.setPhase(450);
    CHECK_NEAR(log.last.phase, 90, 1e-4);

    rig.ctl.setDCOffset(1.25);
    CHECK_NEAR(log.last.dcOffset, 1.25, 1e-6);

    rig.ctl.setOutputEnabled(true);
    CHECK(log.last.outputEnabled);
    CHECK_EQ(log.calls, 6);
}

TEST(every_setter_notifies_all_listeners) {
    Rig rig;
    ListenerLog a, b;
    rig.ctl.addListener(recordListener, &a);
    rig.ctl.addListener(recordListener, &b);
    rig.ctl.setWaveform(WAVEFORM::SQUARE);
    rig.ctl.setFrequency(10);
    rig.ctl.setPhase(10);
    rig.ctl.setAmplitude(1);
    rig.ctl.setDCOffset(0);
    rig.ctl.setOutputEnabled(false);
    CHECK_EQ(a.calls, 6);
    CHECK_EQ(b.calls, 6);
}

TEST(listener_can_read_state_without_deadlock) {
    Rig rig;
    ListenerLog log;
    log.controller = &rig.ctl;
    rig.ctl.addListener(recordListener, &log);

    rig.ctl.setFrequency(5000);
    rig.ctl.setOutputEnabled(true);
    CHECK_EQ(log.calls, 2);
    CHECK_EQ(log.heldDuringCall, 0);   //listeners run outside the lock
    CHECK_EQ(fakeMutexDeadlocks(), 0); //the fake counts a take while held as a deadlock
    CHECK_EQ(fakeMutexesHeld(), 0);

    //state() after the listeners ran must still work, i.e. the lock was given back
    FuncGenState s = rig.ctl.state();
    CHECK_NEAR(s.frequency, 5000, 1e-3);
    CHECK(s.outputEnabled);
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
    rig.ctl.setWaveform(WAVEFORM::SQUARE);
    rig.ctl.setFrequency(2500);
    rig.ctl.setOutputEnabled(true);
    FuncGenState s = rig.ctl.state();
    CHECK(s.waveform == WAVEFORM::SQUARE);
    CHECK_NEAR(s.frequency, 2500, 1e-3);
    CHECK(s.outputEnabled);
    CHECK_NEAR(s.amplitude, rig.gen.amplitude(), 0);
}

TEST(listener_gets_change_source) {
    Rig rig;
    ListenerLog log;
    rig.ctl.addListener(recordListener, &log);
    rig.ctl.setFrequency(500);
    CHECK(log.lastSource == CHANGE_SOURCE::LOCAL); //default
    rig.ctl.setAmplitude(1.0f, CHANGE_SOURCE::MCP);
    CHECK(log.lastSource == CHANGE_SOURCE::MCP);
    rig.ctl.setWaveform(WAVEFORM::SQUARE, CHANGE_SOURCE::BLE);
    CHECK(log.lastSource == CHANGE_SOURCE::BLE);
    rig.ctl.setOutputEnabled(true, CHANGE_SOURCE::MCP);
    CHECK(log.lastSource == CHANGE_SOURCE::MCP);
    rig.ctl.setPhase(10);
    CHECK(log.lastSource == CHANGE_SOURCE::LOCAL);
    rig.ctl.setDCOffset(0.5f, CHANGE_SOURCE::MCP);
    CHECK(log.lastSource == CHANGE_SOURCE::MCP);
}
