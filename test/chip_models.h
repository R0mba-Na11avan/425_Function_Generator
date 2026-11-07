/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: replays the fake SPI log through simple models of the AD9833
             and MCP4131 so tests can check what the chips would hold,
             instead of the exact order the libraries write things in
**************************************************************************/

#ifndef CHIP_MODELS_H
#define CHIP_MODELS_H

#include "fakes/SPI.h"

//AD9833 registers, decoded per the datasheet (16-bit words, top bits = address)
struct Ad9833Model {
    uint16_t control = 0;
    uint32_t freq[2] = {0, 0};
    uint16_t phase[2] = {0, 0};
    int words = 0;
    int badMode = 0; //frames not sent in SPI mode 2

    bool sleeping() const { return control & ((1 << 7) | (1 << 6)); }
    bool squareBits() const { return (control & (1 << 5)) && (control & (1 << 3)); } //OPBITEN | DIV2
    bool triangleBit() const { return control & (1 << 1); }                          //MODE
};

inline Ad9833Model replayAd9833(const SPIClass& spi, int csPin) {
    Ad9833Model m;
    bool lsbNext[2] = {true, true}; //with B28 set, the LSB half arrives first
    for (const SpiFrame& f : spi.frames) {
        if (f.csPin != csPin) continue;
        if (f.dataMode != SPI_MODE2) m.badMode++;
        for (size_t i = 0; i + 1 < f.bytes.size(); i += 2) {
            uint16_t word = (f.bytes[i] << 8) | f.bytes[i + 1];
            m.words++;
            uint8_t addr = word >> 14;
            if (addr == 0) {
                m.control = word & 0x3FFF;
                lsbNext[0] = lsbNext[1] = true;
            } else if (addr == 1 || addr == 2) {
                int ch = (addr == 1) ? 0 : 1;
                uint32_t half = word & 0x3FFF;
                bool b28 = m.control & (1 << 13);
                bool isLsb = b28 ? lsbNext[ch] : !(m.control & (1 << 12));
                if (isLsb) m.freq[ch] = (m.freq[ch] & ~0x3FFFUL) | half;
                else m.freq[ch] = (m.freq[ch] & 0x3FFFUL) | (half << 14);
                if (b28) lsbNext[ch] = !lsbNext[ch];
            } else {
                int ch = (word & (1 << 13)) ? 1 : 0;
                m.phase[ch] = word & 0x0FFF;
            }
        }
    }
    return m;
}

//MCP4131 wiper 0, from the write commands
struct Mcp4131Model {
    int wiper = -1; //-1 = never written
    int writes = 0;
    int badMode = 0;
};

inline Mcp4131Model replayMcp4131(const SPIClass& spi, int csPin) {
    Mcp4131Model m;
    for (const SpiFrame& f : spi.frames) {
        if (f.csPin != csPin) continue;
        if (f.dataMode != SPI_MODE0) m.badMode++;
        if (f.bytes.size() < 2) continue;
        uint8_t address = f.bytes[0] >> 4;
        uint8_t command = (f.bytes[0] >> 2) & 0x3;
        if (address == 0 && command == 0) {
            m.wiper = f.bytes[1];
            m.writes++;
        }
    }
    return m;
}

//tuning word the AD9833 needs for a frequency (25 MHz crystal, rounded like the library)
inline uint32_t ad9833FreqWord(double hz) {
    return (uint32_t)llround(hz * 268435456.0 / 25000000.0);
}

#endif
