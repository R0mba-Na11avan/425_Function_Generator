#include "func_gen.h"

FunctionGenerator::FunctionGenerator(uint8_t slaveSelectPin, __SPI_CLASS__ * mySPI) : funcIC(slaveSelectPin, mySPI) {
    waveform = SINE;
    frequency = 1.0;
    amplitude = 1.0;
    dcOffset = 0.0;
}

FunctionGenerator::FunctionGenerator(uint8_t slaveSelectPin, uint8_t spiData, uint8_t spiClock) : funcIC(slaveSelectPin, spiData, spiClock) {
    waveform = SINE;
    frequency = 1.0;
    amplitude = 1.0;
    dcOffset = 0.0;
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