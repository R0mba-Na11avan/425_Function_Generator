#include "funcgen_controller.h"

FuncGenController::FuncGenController(FunctionGenerator& ch1, FunctionGenerator& ch2)
    : _mirror(false), _lock(xSemaphoreCreateMutex()), _listenerCount(0) {
    _gen[CHANNEL_1] = &ch1;
    _gen[CHANNEL_2] = &ch2;
}

template <typename Apply>
bool FuncGenController::change(uint8_t channel, bool followed, CHANGE_SOURCE source, Apply apply) {
    if (channel >= CHANNEL_COUNT) return false;

    xSemaphoreTake(_lock, portMAX_DELAY);
    if (followed && _mirror && channel == CHANNEL_2) {
        xSemaphoreGive(_lock);
        return false;
    }
    apply(*_gen[channel]);
    bool copied = followed && _mirror && channel == CHANNEL_1;
    if (copied) syncMirror();
    xSemaphoreGive(_lock);

    notify(channel, source);
    if (copied) notify(CHANNEL_2, source);
    return true;
}

bool FuncGenController::setWaveform(uint8_t channel, WAVEFORM waveform, CHANGE_SOURCE source) {
    return change(channel, true, source, [&](FunctionGenerator& g) { g.setWaveform(waveform); });
}

bool FuncGenController::setFrequency(uint8_t channel, float frequency, CHANGE_SOURCE source) {
    return change(channel, true, source, [&](FunctionGenerator& g) { g.setFrequency(frequency); });
}

bool FuncGenController::setPhase(uint8_t channel, float phase, CHANGE_SOURCE source) {
    return change(channel, true, source, [&](FunctionGenerator& g) { g.setPhase(phase); });
}

bool FuncGenController::setAmplitude(uint8_t channel, float amplitude, CHANGE_SOURCE source) {
    return change(channel, true, source, [&](FunctionGenerator& g) { g.setAmplitude(amplitude); });
}

bool FuncGenController::setDCOffset(uint8_t channel, float dcOffset, CHANGE_SOURCE source) {
    return change(channel, true, source, [&](FunctionGenerator& g) { g.setDCOffset(dcOffset); });
}

bool FuncGenController::setOutputEnabled(uint8_t channel, bool enabled, CHANGE_SOURCE source) {
    return change(channel, false, source, [&](FunctionGenerator& g) { g.setOutput(enabled); });
}

void FuncGenController::setMirror(bool on, CHANGE_SOURCE source) {
    xSemaphoreTake(_lock, portMAX_DELAY);
    _mirror = on;
    if (on) syncMirror();
    xSemaphoreGive(_lock);
    notify(CHANNEL_2, source);
}

bool FuncGenController::mirror() {
    xSemaphoreTake(_lock, portMAX_DELAY);
    bool on = _mirror;
    xSemaphoreGive(_lock);
    return on;
}

void FuncGenController::syncMirror() {
    const FunctionGenerator& from = *_gen[CHANNEL_1];
    FunctionGenerator& to = *_gen[CHANNEL_2];

    //only what differs is written, so CH2's output doesn't glitch on a no-op.
    //the waveform first: it can pull the amplitude (and offset) into its range
    if (to.waveform() != from.waveform()) to.setWaveform(from.waveform());
    if (to.frequency() != from.frequency()) to.setFrequency(from.frequency());
    if (to.phase() != from.phase()) to.setPhase(from.phase());

    //peak rule: CH1's pair fits, CH2's current pair fits. lowering CH2's
    //amplitude first leaves room for CH1's offset, and CH1's amplitude fits
    //next to its own offset, so no step is clamped
    if (to.amplitude() > from.amplitude()) to.setAmplitude(from.amplitude());
    if (to.DCOffset() != from.DCOffset()) to.setDCOffset(from.DCOffset());
    if (to.amplitude() != from.amplitude()) to.setAmplitude(from.amplitude());
}

FuncGenState FuncGenController::state(uint8_t channel) {
    if (channel >= CHANNEL_COUNT) channel = CHANNEL_1;
    xSemaphoreTake(_lock, portMAX_DELAY);
    FuncGenState s = snapshot(channel);
    xSemaphoreGive(_lock);
    return s;
}

FuncGenLimits FuncGenController::limits() const {
    FuncGenLimits l;
    l.frequency = FunctionGenerator::frequencyRange();
    l.phase = FunctionGenerator::phaseRange();
    for (uint8_t i = 0; i < WAVEFORM_COUNT; i++) {
        l.amplitude[i] = FunctionGenerator::amplitudeRange((WAVEFORM)i);
    }
    l.dcOffset = FunctionGenerator::dcOffsetRange();
    l.outputPeak = FunctionGenerator::outputPeakMax();
    return l;
}

bool FuncGenController::addListener(Listener listener, void* ctx) {
    if (_listenerCount >= MAX_LISTENERS) return false;
    _listeners[_listenerCount] = listener;
    _listenerCtx[_listenerCount] = ctx;
    _listenerCount++;
    return true;
}

void FuncGenController::notify(uint8_t channel, CHANGE_SOURCE source) {
    FuncGenState s = state(channel);
    for (uint8_t i = 0; i < _listenerCount; i++) {
        _listeners[i](channel, s, source, _listenerCtx[i]);
    }
}

FuncGenState FuncGenController::snapshot(uint8_t channel) const {
    //the getters return what FunctionGenerator actually applied (after clamping)
    const FunctionGenerator& g = *_gen[channel];
    FuncGenState s = {g.waveform(), g.frequency(), g.phase(), g.amplitude(), g.DCOffset(), g.outputOn()};
    s.mirrored = channel == CHANNEL_2 && _mirror;
    return s;
}
