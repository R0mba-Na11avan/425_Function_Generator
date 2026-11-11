/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: host-test stand-in for the ESP32 SPIClass. records every
             transaction (chip select, mode, clock, bytes) so tests can decode
             what was sent to the AD9833 and AD5452s
**************************************************************************/

#ifndef FAKE_SPI_H
#define FAKE_SPI_H

#include "Arduino.h"
#include <vector>

#define SPI_MODE0 0
#define SPI_MODE1 1
#define SPI_MODE2 2
#define SPI_MODE3 3

class SPISettings {
    public:
        SPISettings() : _clock(1000000), _bitOrder(MSBFIRST), _dataMode(SPI_MODE0) {}
        SPISettings(uint32_t clock, uint8_t bitOrder, uint8_t dataMode)
            : _clock(clock), _bitOrder(bitOrder), _dataMode(dataMode) {}

        uint32_t _clock;
        uint8_t _bitOrder;
        uint8_t _dataMode;
};

//one beginTransaction() .. endTransaction()
struct SpiFrame {
    int csPin;      //chip select that was LOW during the first transfer, -1 if none
    uint8_t dataMode;
    uint32_t clock;
    std::vector<uint8_t> bytes;
};

class SPIClass {
    public:
        SPIClass();

        bool begin(int8_t sck = -1, int8_t miso = -1, int8_t mosi = -1, int8_t ss = -1);
        void end();
        void beginTransaction(SPISettings settings);
        void endTransaction();
        uint8_t transfer(uint8_t data);
        uint16_t transfer16(uint16_t data);

        //test inspection
        void reset();
        bool running;
        bool inTransaction;
        int beginCalls;
        int endCalls;
        int8_t sck, miso, mosi;
        std::vector<SpiFrame> frames;
        uint8_t readValue; //returned by every transfer()
        int nestedTransactions; //beginTransaction while already in one (a bug)
};

extern SPIClass SPI;

#endif
