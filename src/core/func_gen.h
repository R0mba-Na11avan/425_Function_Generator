/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: function generator class used to intefrace with AD9833 module
             and MCP4131 variable resistor
*   Date: Oct-30-2026
**************************************************************************/


#ifndef FUNC_GEN_H
#define FUNC_GEN_H

#include "../libs/AD9833/AD9833.h"
#include "../libs/MCP4131/MCP4131.h"

enum class WAVEFORM : uint8_t {
    SINE,
    SQUARE,
    TRIANGLE
};

class FunctionGenerator {
    public:
        FunctionGenerator(uint8_t funcSelect, uint8_t resistSelect,__SPI_CLASS__ * mySPI = &SPI);

        void begin();

        void setWaveform(WAVEFORM waveform);
        void setFrequency(float frequency);
        void setPhase(float phase);
        void setAmplitude(float amplitude);
        void setDCOffset(float dcOffset);

        WAVEFORM waveform() const;
        float frequency() const;
        float phase() const;
        float amplitude() const;
        float DCOffset() const;

    private:

        unsigned int ampToWipe(float amplitude);

        //hese are measured peak to peak
        //we also need to add a voltage divider if we're using a square wave to keep amplitude consistent
        //^^^ this is because the square wave is V_CC-0 while the sine wave is 680mV to 38mV
        //2N7000 mosfet switch my goat (for the voltage divider)
        static const float AMP_MIN = 0.650-0.038; //numbers from data sheet


        __SPI_CLASS__* _spi;

        AD9833 _funcIC;
        MCP4131 _varR;

        WAVEFORM _waveform;
        float _frequency;
        float _phase;
        float _amplitude;
        float _dcOffset;
};

#endif