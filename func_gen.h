/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: function generator class sued to intefrace with AD9833 module
*   Date: Oct-30-2026
**************************************************************************/


#ifndef FUNC_GEN_H
#define FUNC_GEN_H

#include "AD9833.h"

enum WAVEFORM {
    SINE,
    SQUARE,
    TRIANGLE
};

class FunctionGenerator {
    public:
        FunctionGenerator(uint8_t slaveSelectPin, __SPI_CLASS__ * mySPI = &SPI);
        FunctionGenerator(uint8_t slaveSelectPin, uint8_t spiData, uint8_t spiClock);

        
        void setWaveform(WAVEFORM waveform);
        void setFrequency(float frequency);
        void setAmplitude(float amplitude);
        void setDCOffset(float dcOffset);

        WAVEFORM waveform() const;
        float frequency() const;
        float amplitude() const;
        float dcOffset() const;

    private:
        AD9833 funcIC;

        WAVEFORM waveform;
        float frequency;
        float amplitude;
        float dcOffset;
};

#endif