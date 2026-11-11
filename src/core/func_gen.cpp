#include "func_gen.h"

static_assert(FREQ_MAX_HZ <= AD9833_MAX_FREQ, "FREQ_MAX_HZ is above what the AD9833 can output");
static_assert(PHASE_MAX_DEG <= AD9833_MAX_PHASE, "PHASE_MAX_DEG is above the AD9833 phase range");

//the amplitude DAC can't make more than its full-code output
static_assert(AMP_MAX_VPP_SINE <= AMP_FULL_CODE_VPP_SINE, "AMP_MAX_VPP_SINE is above AMP_FULL_CODE_VPP_SINE");
static_assert(AMP_MAX_VPP_SQUARE <= AMP_FULL_CODE_VPP_SQUARE, "AMP_MAX_VPP_SQUARE is above AMP_FULL_CODE_VPP_SQUARE");
static_assert(AMP_MAX_VPP_TRIANGLE <= AMP_FULL_CODE_VPP_TRIANGLE, "AMP_MAX_VPP_TRIANGLE is above AMP_FULL_CODE_VPP_TRIANGLE");
static_assert(AMP_MIN_VPP_SINE >= 0 && AMP_MIN_VPP_SQUARE >= 0 && AMP_MIN_VPP_TRIANGLE >= 0, "amplitude minimums can't be negative");

//the peak rule needs room for every amplitude at 0 V offset
static_assert(AMP_MAX_VPP_SINE <= 2 * OUTPUT_PEAK_MAX_V, "AMP_MAX_VPP_SINE doesn't fit inside OUTPUT_PEAK_MAX_V");
static_assert(AMP_MAX_VPP_SQUARE <= 2 * OUTPUT_PEAK_MAX_V, "AMP_MAX_VPP_SQUARE doesn't fit inside OUTPUT_PEAK_MAX_V");
static_assert(AMP_MAX_VPP_TRIANGLE <= 2 * OUTPUT_PEAK_MAX_V, "AMP_MAX_VPP_TRIANGLE doesn't fit inside OUTPUT_PEAK_MAX_V");

//0 V offset must be allowed (it's the power-on value and the output-off level),
//and the offset DAC has to reach the whole offset range
static_assert(DC_OFFSET_MIN_V <= 0 && DC_OFFSET_MAX_V >= 0, "the DC offset range must include 0 V");
static_assert(OFFSET_V_AT_CODE_0 != OFFSET_V_AT_FULL_CODE, "the offset DAC calibration needs two different voltages");
static_assert((OFFSET_V_AT_CODE_0 < OFFSET_V_AT_FULL_CODE ? OFFSET_V_AT_CODE_0 : OFFSET_V_AT_FULL_CODE) <= DC_OFFSET_MIN_V,
              "the offset DAC can't reach DC_OFFSET_MIN_V");
static_assert((OFFSET_V_AT_CODE_0 > OFFSET_V_AT_FULL_CODE ? OFFSET_V_AT_CODE_0 : OFFSET_V_AT_FULL_CODE) >= DC_OFFSET_MAX_V,
              "the offset DAC can't reach DC_OFFSET_MAX_V");

static float clampf(float value, ValueRange range) {
    if (value < range.min) return range.min;
    if (value > range.max) return range.max;
    return value;
}

//rounds a DAC code and keeps it in 0..MAX_CODE
static uint16_t toCode(float code) {
    if (code <= 0.0f) return 0;
    if (code >= AD5452::MAX_CODE) return AD5452::MAX_CODE;
    return (uint16_t)(code + 0.5f);
}

ValueRange peakLimitedAmplitude(ValueRange amplitude, float dcOffset, float peakMax) {
    float room = 2.0f * (peakMax - fabsf(dcOffset));
    if (room < amplitude.max) amplitude.max = room;
    //offset already at or past the edge: only the minimum is left
    if (amplitude.max < amplitude.min) amplitude.max = amplitude.min;
    return amplitude;
}

ValueRange peakLimitedOffset(ValueRange dcOffset, float amplitude, float peakMax) {
    float room = peakMax - amplitude / 2.0f;
    if (room < 0.0f) room = 0.0f;
    if (dcOffset.max > room) dcOffset.max = room;
    if (dcOffset.min < -room) dcOffset.min = -room;
    return dcOffset;
}

FunctionGenerator::FunctionGenerator(uint8_t funcSelect, uint8_t ampDacSelect, uint8_t offsetDacSelect,
                                     uint8_t waveSwitchPin, __SPI_CLASS__ * mySPI)
    : _funcIC(funcSelect, mySPI), _ampDac(ampDacSelect, mySPI, MDAC_SPI_HZ),
      _offsetDac(offsetDacSelect, mySPI, MDAC_SPI_HZ), _switchPin(waveSwitchPin) {
    _waveform = WAVEFORM::SINE;
    _frequency = 1.0;
    _phase = 0.0;
    _dcOffset = 0.0;
    _amplitude = clampf(AMP_DEFAULT_VPP, amplitudeRange(_waveform, _dcOffset));
    _funcOn = false; //output stays off until setOutput(true)
    _spi = mySPI;
}

void FunctionGenerator::begin() {
    // nothing else starts the generator bus, and AD9833 begin() doesn't either
    _spi->begin(PIN_GEN_SCK, PIN_GEN_MISO, PIN_GEN_MOSI);

    // every chip select idle high before the first transfer, so no chip
    // reads a word meant for another one
    _ampDac.begin();
    _offsetDac.begin();
    pinMode(_switchPin, OUTPUT);
    _funcIC.begin();

    // the offset DAC powers up at code 0 (one end of the offset range), so
    // bring the output to 0 V before anything else
    applyLevels();

    applyWaveform();
    _funcIC.setFrequency(_frequency);
    _funcIC.setPhase(_phase);
    applyOutput();
}


void FunctionGenerator::setWaveform(WAVEFORM waveform) {
    _waveform = waveform;

    // the amplitude range (and full-code output) depends on the waveform, so
    // re-clamp first. applyOutput() then rewrites both DACs
    _amplitude = clampf(_amplitude, amplitudeRange(_waveform, _dcOffset));
    _dcOffset = clampf(_dcOffset, dcOffsetRange(_amplitude));

    applyWaveform();
    applyOutput();
}

void FunctionGenerator::setPhase(float phase) {
    // the AD9833 wraps the phase into [0, 360) and returns the wrapped value
    _phase = _funcIC.setPhase(phase);
}

void FunctionGenerator::setFrequency(float frequency) {
    // the AD9833 returns the frequency it applied
    _frequency = _funcIC.setFrequency(clampf(frequency, frequencyRange()));
}

void FunctionGenerator::setAmplitude(float amplitude) {
    // the offset stays put, so the amplitude stops at the peak limit
    _amplitude = clampf(amplitude, amplitudeRange(_waveform, _dcOffset));
    applyLevels();
}

void FunctionGenerator::setDCOffset(float dcOffset) {
    // the amplitude stays put, so the offset stops at the peak limit
    _dcOffset = clampf(dcOffset, dcOffsetRange(_amplitude));
    applyLevels();
}

void FunctionGenerator::setOutput(bool on) {
    _funcOn = on;
    applyOutput();
}

WAVEFORM FunctionGenerator::waveform() const {
    return _waveform;
}

float FunctionGenerator::frequency() const {
    return _frequency;
}

float FunctionGenerator::phase() const {
    return _phase;
}

float FunctionGenerator::amplitude() const {
    return _amplitude;
}

float FunctionGenerator::DCOffset() const {
    return _dcOffset;
}

bool FunctionGenerator::outputOn() const {
    return _funcOn;
}

ValueRange FunctionGenerator::frequencyRange() {
    return ValueRange{FREQ_MIN_HZ, FREQ_MAX_HZ};
}

ValueRange FunctionGenerator::phaseRange() {
    return ValueRange{PHASE_MIN_DEG, PHASE_MAX_DEG};
}

ValueRange FunctionGenerator::amplitudeRange(WAVEFORM waveform) {
    switch (waveform) {
        case WAVEFORM::SQUARE:
            return ValueRange{AMP_MIN_VPP_SQUARE, AMP_MAX_VPP_SQUARE};
        case WAVEFORM::TRIANGLE:
            return ValueRange{AMP_MIN_VPP_TRIANGLE, AMP_MAX_VPP_TRIANGLE};
        case WAVEFORM::SINE:
        default:
            return ValueRange{AMP_MIN_VPP_SINE, AMP_MAX_VPP_SINE};
    }
}

ValueRange FunctionGenerator::dcOffsetRange() {
    return ValueRange{DC_OFFSET_MIN_V, DC_OFFSET_MAX_V};
}

float FunctionGenerator::outputPeakMax() {
    return OUTPUT_PEAK_MAX_V;
}

ValueRange FunctionGenerator::amplitudeRange(WAVEFORM waveform, float dcOffset) {
    return peakLimitedAmplitude(amplitudeRange(waveform), dcOffset, OUTPUT_PEAK_MAX_V);
}

ValueRange FunctionGenerator::dcOffsetRange(float amplitude) {
    return peakLimitedOffset(dcOffsetRange(), amplitude, OUTPUT_PEAK_MAX_V);
}

void FunctionGenerator::applyOutput() {
    _funcIC.setPowerMode(_funcOn ? AD9833_PWR_ON : AD9833_PWR_DISABLE_ALL);
    applyLevels();
}

void FunctionGenerator::applyLevels() {
    if (_funcOn) {
        _ampDac.write(ampToCode(_amplitude));
        _offsetDac.write(offsetToCode(_dcOffset));
    } else {
        _ampDac.write(0);
        _offsetDac.write(offsetToCode(0.0f));
    }
}

void FunctionGenerator::applyWaveform() {
    switch (_waveform){
        case WAVEFORM::SINE:
            _funcIC.setWave(AD9833_SINE);
            break;
        case WAVEFORM::SQUARE:
            _funcIC.setWave(AD9833_SQUARE1);
            break;
        case WAVEFORM::TRIANGLE:
            _funcIC.setWave(AD9833_TRIANGLE);
            break;
        default:
            break;
    }
    // HIGH = the divided square path, LOW = the filtered sine/triangle path
    digitalWrite(_switchPin, _waveform == WAVEFORM::SQUARE ? HIGH : LOW);
}

uint16_t FunctionGenerator::ampToCode(float amplitude) const {
    // the AD5452 output is code / 4096 of its input, so amplitude is linear
    // in the code. AMP_FULL_CODE_VPP_* is the measured output at code 4095
    float fullCode;
    switch (_waveform) {
        case WAVEFORM::SQUARE: fullCode = AMP_FULL_CODE_VPP_SQUARE; break;
        case WAVEFORM::TRIANGLE: fullCode = AMP_FULL_CODE_VPP_TRIANGLE; break;
        case WAVEFORM::SINE:
        default: fullCode = AMP_FULL_CODE_VPP_SINE; break;
    }
    if (fullCode <= 0.0f) return 0;
    return toCode(amplitude / fullCode * AD5452::MAX_CODE);
}

uint16_t FunctionGenerator::offsetToCode(float dcOffset) {
    // straight line between the two measured ends of the offset DAC
    float t = (dcOffset - OFFSET_V_AT_CODE_0) / (OFFSET_V_AT_FULL_CODE - OFFSET_V_AT_CODE_0);
    return toCode(t * AD5452::MAX_CODE);
}
