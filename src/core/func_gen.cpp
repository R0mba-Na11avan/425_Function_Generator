#include "func_gen.h"

FunctionGenerator::FunctionGenerator(uint8_t funcSelect, uint8_t resistSelect, __SPI_CLASS__ * mySPI) 
                                    : _funcIC(funcSelect, mySPI), _varR(resistSelect) {
    _waveform = WAVEFORM::SINE;
    _frequency = 1.0;
    _phase = 0.0;
    _amplitude = AMP_MIN; //starts at the lowest amplitude the hardware can output
    _dcOffset = 0.0;
    _funcOn = false; //output stays off until setOutput(true)
    _spi = mySPI;
}

void FunctionGenerator::begin() {
    // MCP4131 is already set up by its constructor (SPI.begin + pinMode)
    // AD9833 needs begin() to set its pins and reset it, hw SPI doesn't start the bus
    _spi->begin();
    _funcIC.begin();
    _funcIC.setWave(AD9833_SINE);
    _funcIC.setFrequency(_frequency);
    applyOutput();

    // set variable resistor to 1 V/V gain
    _varR.writeWiper(ampToWipe(_amplitude));
}


void FunctionGenerator::setWaveform(WAVEFORM waveform) {
    // Set the waveform type and change max amplitude based on the waveform type
    _waveform = waveform;

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
    applyOutput();
}

void FunctionGenerator::setPhase(float phase) {
    _phase = _funcIC.setPhase(phase);
}

void FunctionGenerator::setFrequency(float frequency) {
    // the AD9833 clamps to 0..AD9833_MAX_FREQ and returns the frequency it applied
    _frequency = _funcIC.setFrequency(frequency);
}

void FunctionGenerator::setAmplitude(float amplitude) {
    // validate amiplitude based on waveform type and set
    amplitude = (amplitude < AMP_MIN) ? AMP_MIN : amplitude;
    _amplitude = amplitude;

    _varR.writeWiper(ampToWipe(_amplitude));
}

void FunctionGenerator::setDCOffset(float dcOffset) {
    _dcOffset = dcOffset;
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

void FunctionGenerator::applyOutput() {
    _funcIC.setPowerMode(_funcOn ? AD9833_PWR_ON : AD9833_PWR_DISABLE_ALL);
}

unsigned int FunctionGenerator::ampToWipe(float amplitude) {
    //m is just a constant that convert the given amplitude into a resistor
    //value for the MCP4131
    float m = 1;
    return amplitude*m;
}