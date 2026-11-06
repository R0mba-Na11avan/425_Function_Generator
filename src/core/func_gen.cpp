#include "func_gen.h"

FunctionGenerator::FunctionGenerator(uint8_t funcSelect, uint8_t resistSelect, __SPI_CLASS__ * mySPI) 
                                    : _funcIC(funcSelect, mySPI), _varR(resistSelect) {
    _waveform = WAVEFORM::SINE;
    _frequency = 1.0;
    _phase = 0.0;
    _amplitude = 0.05;
    _dcOffset = 0.0;
    _spi = mySPI;
}

void FunctionGenerator::begin() {
    // MCP4131 is already set up by its constructor (SPI.begin + pinMode)
    // AD9833 needs begin() to set its pins and reset it, hw SPI doesn't start the bus
    _spi->begin();
    _funcIC.begin();
    _funcIC.setWave(AD9833_SINE);
    _funcIC.setFrequency(_frequency);

    // set variable resistor to 1 V/V gain
    _varR.writeWiper(ampToWipe(_amplitude));
}


void FunctionGenerator::setWaveform(WAVEFORM waveform) {
    // Set the waveform type and change max amplitude based on the waveform type
    _waveform = waveform;

    switch (_waveform){
        case WAVEFORM::SINE:
            _funcIC.setWave(AD9833_SINE);
        case WAVEFORM::SQUARE:
            _funcIC.setWave(AD9833_SQUARE1);
        case WAVEFORM::TRIANGLE:
            _funcIC.setWave(AD9833_TRIANGLE);
        default:
    }
}

void FunctionGenerator::setPhase(float phase) {
    _phase = _funcIC.setPhase(phase);
}

void FunctionGenerator::setFrequency(float frequency) {
    frequency = (frequency > AD9833_MAX_FREQ) ? AD9833_MAX_FREQ : frequency;
    frequency = (frequency < 1) ? 1 : frequency;

    _frequency = frequency;
    _funcIC.setFrequency(_frequency);
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

unsigned int FunctionGenerator::ampToWipe(float amplitude) {
    //m is just a constant that convert the given amplitude into a resistor
    //value for the MCP4131
    float m = 1;
    return amplitude*m;
}