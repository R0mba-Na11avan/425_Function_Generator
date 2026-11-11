/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: minimal driver for the Analog Devices AD5452 12-bit multiplying
             DAC (current output, R-2R ladder) over SPI. no Arduino library
             exists for it, so this one lives with the vendored libs
**************************************************************************/

/*
    The AD5452 scales whatever voltage is on its VREF pin by code / 4096
    (Vout = -VREF * code / 4096 with the usual op-amp I/V stage on IOUT1/RFB).
    It only attenuates, it never amplifies.

    Serial word (datasheet, Rev. 0, Table 10 / Figure 48), 16 bits MSB first:

        C1 C0 D11 D10 ... D1 D0 X X

    C1 C0 = 00 loads and updates the DAC (the power-on default). 11 would move
    data clocking to the rising SCLK edge and leave the DAC inoperative until
    the next frame, so this driver never sends it. The two LSBs are ignored.

    Data is clocked in on the falling SCLK edge and latched when SYNC goes
    high, so SPI_MODE1 (the mode in the datasheet's interfacing example) is
    used. Each write sets its own SPISettings, so the chip can share a bus
    with chips in other modes (the AD9833 uses SPI_MODE2).

    The chip powers up at zero scale. It has no readback, so code() returns
    the last value written.
*/

#ifndef AD5452_H
#define AD5452_H

#include "Arduino.h"
#include "SPI.h"

class AD5452 {
    public:
        static const uint16_t MAX_CODE = 4095;
        static const uint32_t DEFAULT_SPI_HZ = 8000000;     //chip allows up to 50 MHz

        //syncPin is the chip select (SYNC, active low). nothing touches the
        //hardware until begin()
        AD5452(uint8_t syncPin, SPIClass* spi = &SPI, uint32_t spiHz = DEFAULT_SPI_HZ);

        //sets SYNC as an output, idle high. does not start the SPI bus
        void begin();

        //loads and updates the DAC. codes above MAX_CODE are clamped
        void write(uint16_t code);

        //last code written (0 before the first write, same as the chip's power-on state)
        uint16_t code() const;

    private:
        SPIClass* _spi;
        uint32_t _spiHz;
        uint8_t _syncPin;
        uint16_t _code;
};

#endif
