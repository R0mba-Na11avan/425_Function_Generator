#include "func_gen.h"

FunctionGenerator::FunctionGenerator(uint8_t funcSelect, uint8_t resistSelect, __SPI_CLASS__ * mySPI) 
                                    : funcIC(funcSelect, mySPI), varR(resistSelect) {
    wave = SINE;
    freq = 1.0;
    amp = 0.05;
    dcOff = 0.0;

    // set variable resistor to 1 V/V gain
}


void FunctionGenerator::setWaveform(WAVEFORM wave) {
    // Set the waveform type and change max amplitude based on the waveform type
    this->wave = wave;
}

void FunctionGenerator::setFrequency(float freq) {

    freq = (freq > AD9833_MAX_FREQ) ? AD9833_MAX_FREQ : freq;
    freq = (freq < 1) ? 1 : freq;

    this->freq = freq;
    funcIC.setFrequency(freq);
}

void FunctionGenerator::setAmplitude(float amp) {
    // validate amiplitude based on waveform type and set
    amp = (amp < AMP_MIN) ? AMP_MIN : amp;
    this->amp = amp;
}

void FunctionGenerator::setDCOffset(float dcOff) {
    this->dcOff = dcOff;
}

WAVEFORM FunctionGenerator::waveform() const {
    return wave;
}

float FunctionGenerator::frequency() const {
    return freq;
}

float FunctionGenerator::amplitude() const {
    return amp;
}

float FunctionGenerator::DCOffset() const {
    return dcOff;
}