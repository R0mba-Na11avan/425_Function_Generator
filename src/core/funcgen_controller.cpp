#include "funcgen_controller.h"

FuncGenController::FuncGenController(FunctionGenerator& funcGen)
    : _funcGen(funcGen), _lock(xSemaphoreCreateMutex()), _listenerCount(0) {}

void FuncGenController::setWaveform(WAVEFORM waveform) {
    xSemaphoreTake(_lock, portMAX_DELAY);
    _funcGen.setWaveform(waveform);
    xSemaphoreGive(_lock);
    notify();
}

void FuncGenController::setFrequency(float frequency) {
    xSemaphoreTake(_lock, portMAX_DELAY);
    _funcGen.setFrequency(frequency);
    xSemaphoreGive(_lock);
    notify();
}

void FuncGenController::setPhase(float phase) {
    xSemaphoreTake(_lock, portMAX_DELAY);
    _funcGen.setPhase(phase);
    xSemaphoreGive(_lock);
    notify();
}

void FuncGenController::setAmplitude(float amplitude) {
    xSemaphoreTake(_lock, portMAX_DELAY);
    _funcGen.setAmplitude(amplitude);
    xSemaphoreGive(_lock);
    notify();
}

void FuncGenController::setDCOffset(float dcOffset) {
    xSemaphoreTake(_lock, portMAX_DELAY);
    _funcGen.setDCOffset(dcOffset);
    xSemaphoreGive(_lock);
    notify();
}

void FuncGenController::setOutputEnabled(bool enabled) {
    xSemaphoreTake(_lock, portMAX_DELAY);
    _funcGen.setOutput(enabled);
    xSemaphoreGive(_lock);
    notify();
}

FuncGenState FuncGenController::state() {
    xSemaphoreTake(_lock, portMAX_DELAY);
    FuncGenState s = snapshot();
    xSemaphoreGive(_lock);
    return s;
}

bool FuncGenController::addListener(Listener listener, void* ctx) {
    if (_listenerCount >= MAX_LISTENERS) return false;
    _listeners[_listenerCount] = listener;
    _listenerCtx[_listenerCount] = ctx;
    _listenerCount++;
    return true;
}

void FuncGenController::notify() {
    FuncGenState s = state();
    for (uint8_t i = 0; i < _listenerCount; i++) {
        _listeners[i](s, _listenerCtx[i]);
    }
}

FuncGenState FuncGenController::snapshot() const {
    //the getters return what FunctionGenerator actually applied (after clamping)
    return FuncGenState{
        _funcGen.waveform(),
        _funcGen.frequency(),
        _funcGen.phase(),
        _funcGen.amplitude(),
        _funcGen.DCOffset(),
        _funcGen.outputOn()
    };
}
