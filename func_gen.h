/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: function generator class used to intefrace with AD9833 module
             and MCP4131 variable resistor
*   Date: Oct-30-2026
**************************************************************************/


#ifndef FUNC_GEN_H
#define FUNC_GEN_H

#include "AD9833.h"
#include "MCP4131.h"

enum WAVEFORM {
    SINE,
    SQUARE,
    TRIANGLE
};

class FunctionGenerator {
    public:
        FunctionGenerator(uint8_t funcSelect, uint8_t resistSelect,__SPI_CLASS__ * mySPI = &SPI);
        
        void setWaveform(WAVEFORM waveform);
        void setFrequency(float frequency);
        void setAmplitude(float amplitude);
        void setDCOffset(float dcOffset);

        WAVEFORM waveform() const;
        float frequency() const;
        float amplitude() const;
        float DCOffset() const;

    private:
        AD9833 funcIC;
        MCP4131 varR;

        WAVEFORM wave;
        float freq;
        float amp;
        float dcOff;
};

#endif