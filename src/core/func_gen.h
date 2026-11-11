/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: function generator class used to interface with the AD9833
             DDS, the TS5A3159A waveform path switch and the two AD5452
             multiplying DACs (amplitude and DC offset)
*   Date: Oct-30-2026
**************************************************************************/

/*
    Signal path (docs/parts.md): the AD9833 makes the wave, the switch picks
    the sine/triangle path or the divided square path, a fixed x20 gain stage
    boosts it, AD5452 #1 scales it down to the amplitude, and a summer adds the
    DC offset from AD5452 #2.

    Peak rule: the output can't swing past OUTPUT_PEAK_MAX_V, so
    amplitude / 2 + |offset| <= OUTPUT_PEAK_MAX_V. The range of each setting
    depends on the other one (see the two-argument range getters). A setter
    clamps only its own value; it never moves the other one.

    Output off: the AD9833 sleeps, and both DACs are set so the output sits at
    0 V (amplitude code 0, offset code for 0 V). The stored amplitude and
    offset are kept and come back when the output is turned on.
*/

#ifndef FUNC_GEN_H
#define FUNC_GEN_H

#include "../config.h"
#include "../libs/AD9833/AD9833.h"
#include "../libs/AD5452/AD5452.h"

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

//the peak rule, written once so the core and every interface agree.
//amplitude's range with the offset held where it is, and the other way round
ValueRange peakLimitedAmplitude(ValueRange amplitude, float dcOffset, float peakMax);
ValueRange peakLimitedOffset(ValueRange dcOffset, float amplitude, float peakMax);

class FunctionGenerator {
    public:
        FunctionGenerator(uint8_t funcSelect, uint8_t ampDacSelect, uint8_t offsetDacSelect,
                          uint8_t waveSwitchPin, __SPI_CLASS__ * mySPI = &SPI);

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

        //limits, all taken from config.h. these are the widest ranges
        static ValueRange frequencyRange();
        static ValueRange phaseRange(); //max is exclusive, phase wraps
        static ValueRange amplitudeRange(WAVEFORM waveform);
        static ValueRange dcOffsetRange();
        static float outputPeakMax();

        //what is allowed right now, with the other setting held where it is
        static ValueRange amplitudeRange(WAVEFORM waveform, float dcOffset);
        static ValueRange dcOffsetRange(float amplitude);

    private:

        //DAC codes for a setting. linear, scaled by the calibration values in config.h
        uint16_t ampToCode(float amplitude) const;
        static uint16_t offsetToCode(float dcOffset);

        //writes _funcOn to the AD9833 power bits and both DACs. setWave() clears the
        //power bits, so this has to be re-applied after every waveform change
        void applyOutput();

        //writes both DACs: the stored amplitude and offset, or 0 V while the output is off
        void applyLevels();

        //writes the AD9833 wave bits and the path switch for _waveform
        void applyWaveform();

        __SPI_CLASS__* _spi;

        AD9833 _funcIC;
        AD5452 _ampDac;
        AD5452 _offsetDac;
        uint8_t _switchPin;

        WAVEFORM _waveform;
        float _frequency;
        float _phase;
        float _amplitude;
        float _dcOffset;
        bool _funcOn;
};

#endif
