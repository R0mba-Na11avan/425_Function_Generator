/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: minimal driver for the Microchip MCP4822 dual 12-bit voltage
             output DAC (internal 2.048 V reference) over SPI. written for
             this project, like the AD5452 driver
**************************************************************************/

/*
    Vout = 2.048 V * code / 4096 * gain, gain 1x or 2x (1 mV per code at 2x).
    The output can't go above VDD, so at 2x gain on 3.3 V the top codes clip.

    Serial word (datasheet DS20002249, Register 5-1), 16 bits MSB first:

        A/B  -  GA  SHDN  D11 ... D0

    A/B   0 = DAC A (VOUTA), 1 = DAC B (VOUTB)
    GA    1 = 1x gain, 0 = 2x gain
    SHDN  1 = output on, 0 = that channel shut down (VOUT pulled to GND
          through ~500 kOhm)

    Data is latched on the rising SCK edge, SPI mode 0 or 3 (this driver
    uses SPI_MODE0). With LDAC tied to GND the output updates when CS goes
    high. Each write sets its own SPISettings, so the chip can share a bus
    with chips in other modes (the AD9833 uses SPI_MODE2, the AD5452 SPI_MODE1).

    Both channels power up shut down (VOUT about 0 V). The chip has no
    readback, so code() returns the last value written to that channel.
*/

#ifndef MCP4822_H
#define MCP4822_H

#include "Arduino.h"
#include "SPI.h"

class MCP4822 {
    public:
        static const uint16_t MAX_CODE = 4095;
        static const uint32_t DEFAULT_SPI_HZ = 8000000;     //chip allows up to 20 MHz
        static const uint8_t CHANNEL_A = 0;
        static const uint8_t CHANNEL_B = 1;

        //csPin is the chip select (CS, active low). nothing touches the
        //hardware until begin()
        MCP4822(uint8_t csPin, SPIClass* spi = &SPI, uint32_t spiHz = DEFAULT_SPI_HZ);

        //sets CS as an output, idle high. a pin that is already an output
        //(set HIGH early in setup()) never blips LOW. does not start the SPI bus
        void begin();

        //sets one channel and turns its output on. any channel other than
        //CHANNEL_A counts as CHANNEL_B. codes above MAX_CODE are clamped
        void write(uint8_t channel, uint16_t code, bool gain2x = false);

        //shuts one channel down (VOUT pulled to GND, like at power-on)
        void shutdown(uint8_t channel);

        //last code written to a channel (0 before the first write or after shutdown)
        uint16_t code(uint8_t channel) const;

    private:
        void send(uint16_t word);

        SPIClass* _spi;
        uint32_t _spiHz;
        uint8_t _csPin;
        uint16_t _code[2];
};

#endif
