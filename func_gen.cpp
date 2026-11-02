#include "func_gen.h"

FunctionGenerator::FunctionGenerator(uint8_t funcSelect, uint8_t resistSelect, __SPI_CLASS__ * mySPI) 
                                    : funcIC(slaveSelectPin, mySPI), varR(resistSelect) {
    waveform = SINE;
    frequency = 1.0;
    amplitude = 0.05;
    dcOffset = 0.0;

    // set variable resistor to 1 V/V gain
}


void FunctionGenerator::setWaveform(WAVEFORM waveform) {
    // Set the waveform type and change max amplitude based on the waveform type
    this->waveform = waveform;
}

void FunctionGenerator::setFrequency(float frequency) {
    this->frequency = frequency;
    funcIC.setFrequency(static_cast<float>(frequency));
}

void FunctionGenerator::setAmplitude(float amplitude) {
    // validate amiplitude based on waveform type and set
    this->amplitude = amplitude;
}

void FunctionGenerator::setDCOffset(float dcOffset) {
    this->dcOffset = dcOffset;
}

WAVEFORM FunctionGenerator::waveform() const {
    return waveform;
}

float FunctionGenerator::frequency() const {
    return frequency;
}

float FunctionGenerator::amplitude() const {
    return amplitude;
}

float FunctionGenerator::dcOffset() const {
    return dcOffset;
}