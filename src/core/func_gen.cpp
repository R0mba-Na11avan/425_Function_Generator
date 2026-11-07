#include "func_gen.h"

static_assert(FREQ_MAX_HZ <= AD9833_MAX_FREQ, "FREQ_MAX_HZ is above what the AD9833 can output");
static_assert(PHASE_MAX_DEG <= AD9833_MAX_PHASE, "PHASE_MAX_DEG is above the AD9833 phase range");

static float clampf(float value, ValueRange range) {
    if (value < range.min) return range.min;
    if (value > range.max) return range.max;
    return value;
}

FunctionGenerator::FunctionGenerator(uint8_t funcSelect, uint8_t resistSelect, __SPI_CLASS__ * mySPI)
                                    : _funcIC(funcSelect, mySPI), _varR(resistSelect) {
    _waveform = WAVEFORM::SINE;
    _frequency = 1.0;
    _phase = 0.0;
    _amplitude = amplitudeRange(_waveform).min; //starts at the lowest amplitude the hardware can output
    _dcOffset = 0.0;
    _funcOn = false; //output stays off until setOutput(true)
    _spi = mySPI;
}

void FunctionGenerator::begin() {
    // MCP4131's constructor already started the bus with the default pins, and
    // SPIClass::begin() ignores new pins while the bus is running, so restart it
    // on the pins from config.h. AD9833 begin() doesn't start the bus itself
    _spi->end();
    _spi->begin(PIN_GEN_SCK, PIN_GEN_MISO, PIN_GEN_MOSI);
    _funcIC.begin();
    applyWaveform();
    _funcIC.setFrequency(_frequency);
    _funcIC.setPhase(_phase);
    applyOutput();

    _varR.writeWiper(ampToWipe(_amplitude));
}


void FunctionGenerator::setWaveform(WAVEFORM waveform) {
    // Set the waveform type and change max amplitude based on the waveform type
    _waveform = waveform;
    applyWaveform();
    applyOutput();

    // the amplitude range depends on the waveform, so re-clamp and rewrite the pot
    setAmplitude(_amplitude);
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
    // validate amplitude based on waveform type and set
    _amplitude = clampf(amplitude, amplitudeRange(_waveform));

    _varR.writeWiper(ampToWipe(_amplitude));
}

void FunctionGenerator::setDCOffset(float dcOffset) {
    _dcOffset = clampf(dcOffset, dcOffsetRange());
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

void FunctionGenerator::applyOutput() {
    _funcIC.setPowerMode(_funcOn ? AD9833_PWR_ON : AD9833_PWR_DISABLE_ALL);
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
}

unsigned int FunctionGenerator::ampToWipe(float amplitude) {
    //PLACEHOLDER: maps the waveform's amplitude range linearly onto the wiper
    //range. replace with the real gain curve once the output stage is built
    //and measured (amplitude sweep on the scope)
    ValueRange range = amplitudeRange(_waveform);
    if (range.max <= range.min) return 0;

    float t = (amplitude - range.min) / (range.max - range.min);
    return (unsigned int)(t * MCP4131_WIPER_MAX + 0.5f);
}
