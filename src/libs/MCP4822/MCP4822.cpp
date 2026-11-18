#include "MCP4822.h"

static const uint16_t MCP4822_CHANNEL_B = 0x8000;   //A/B bit
static const uint16_t MCP4822_GAIN_1X = 0x2000;     //GA bit (clear = 2x)
static const uint16_t MCP4822_ACTIVE = 0x1000;      //SHDN bit (clear = shut down)

MCP4822::MCP4822(uint8_t csPin, SPIClass* spi, uint32_t spiHz)
    : _spi(spi), _spiHz(spiHz), _csPin(csPin), _code{0, 0} {}

void MCP4822::begin() {
    pinMode(_csPin, OUTPUT);
    digitalWrite(_csPin, HIGH);
}

void MCP4822::write(uint8_t channel, uint16_t code, bool gain2x) {
    if (code > MAX_CODE) code = MAX_CODE;
    uint8_t ch = (channel == CHANNEL_A) ? CHANNEL_A : CHANNEL_B;

    uint16_t word = MCP4822_ACTIVE | code;
    if (ch == CHANNEL_B) word |= MCP4822_CHANNEL_B;
    if (!gain2x) word |= MCP4822_GAIN_1X;
    send(word);

    _code[ch] = code;
}

void MCP4822::shutdown(uint8_t channel) {
    uint8_t ch = (channel == CHANNEL_A) ? CHANNEL_A : CHANNEL_B;
    send(ch == CHANNEL_B ? MCP4822_CHANNEL_B : 0);
    _code[ch] = 0;
}

uint16_t MCP4822::code(uint8_t channel) const {
    return _code[(channel == CHANNEL_A) ? CHANNEL_A : CHANNEL_B];
}

void MCP4822::send(uint16_t word) {
    _spi->beginTransaction(SPISettings(_spiHz, MSBFIRST, SPI_MODE0));
    digitalWrite(_csPin, LOW);
    _spi->transfer16(word);
    digitalWrite(_csPin, HIGH);  //rising CS latches the word (LDAC tied low = output updates now)
    _spi->endTransaction();
}
