#include "AD5452.h"

static const uint16_t AD5452_CONTROL_LOAD = 0x0000;     //C1 C0 = 00, load and update
static const uint8_t AD5452_DATA_SHIFT = 2;              //D11..D0 sit above two ignored LSBs

AD5452::AD5452(uint8_t syncPin, SPIClass* spi, uint32_t spiHz)
    : _spi(spi), _spiHz(spiHz), _syncPin(syncPin), _code(0) {}

void AD5452::begin() {
    pinMode(_syncPin, OUTPUT);
    digitalWrite(_syncPin, HIGH);
}

void AD5452::write(uint16_t code) {
    if (code > MAX_CODE) code = MAX_CODE;
    uint16_t word = AD5452_CONTROL_LOAD | (uint16_t)(code << AD5452_DATA_SHIFT);

    _spi->beginTransaction(SPISettings(_spiHz, MSBFIRST, SPI_MODE1));
    digitalWrite(_syncPin, LOW);
    _spi->transfer16(word);
    digitalWrite(_syncPin, HIGH);  //rising SYNC latches the word
    _spi->endTransaction();

    _code = code;
}

uint16_t AD5452::code() const {
    return _code;
}
