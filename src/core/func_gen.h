/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: function generator class used to intefrace with AD9833 module
             and MCP4131 variable resistor
*   Date: Oct-30-2026
**************************************************************************/


#ifndef FUNC_GEN_H
#define FUNC_GEN_H

#include "../config.h"
#include "../libs/AD9833/AD9833.h"
#include "../libs/MCP4131/MCP4131.h"

enum class WAVEFORM : uint8_t {
    SINE,
    SQUARE,
    TRIANGLE
};

static const uint8_t WAVEFORM_COUNT = 3;

//valid range of one setting, inclusive unless the getter says otherwise
struct ValueRange {
    float min;
    float max;
};

class FunctionGenerator {
    public:
        FunctionGenerator(uint8_t funcSelect, uint8_t resistSelect,__SPI_CLASS__ * mySPI = &SPI);

        void begin();

        //setters clamp to the limits below, the getters return what was applied
        void setWaveform(WAVEFORM waveform);
        void setFrequency(float frequency);
        void setPhase(float phase);
        void setAmplitude(float amplitude);
        void setDCOffset(float dcOffset);
        void setOutput(bool on);

        WAVEFORM waveform() const;
        float frequency() const;
        float phase() const;
        float amplitude() const;
        float DCOffset() const;
        bool outputOn() const;

        //limits, all taken from config.h
        static ValueRange frequencyRange();
        static ValueRange phaseRange(); //max is exclusive, phase wraps
        static ValueRange amplitudeRange(WAVEFORM waveform);
        static ValueRange dcOffsetRange();

    private:

        unsigned int ampToWipe(float amplitude);

        //writes _funcOn to the AD9833 power bits. setWave() clears them, so this has to be
        //re-applied after every waveform change
        void applyOutput();

        //writes the AD9833 wave bits for _waveform
        void applyWaveform();

        __SPI_CLASS__* _spi;

        AD9833 _funcIC;
        MCP4131 _varR;

        WAVEFORM _waveform;
        float _frequency;
        float _phase;
        float _amplitude;
        float _dcOffset;
        bool _funcOn;
};

#endif
