/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: host tests for the AD5452 driver on its own: chip select
             handling, the 16-bit word and clamping
**************************************************************************/

#include "test.h"
#include "chip_models.h"
#include "../src/libs/AD5452/AD5452.h"

static const uint8_t DAC_PIN = 9;

TEST(ad5452_begin_deselects_the_chip) {
    AD5452 dac(DAC_PIN);
    CHECK(fakePinMode(DAC_PIN) != OUTPUT); //constructor touches nothing
    dac.begin();
    CHECK_EQ(fakePinMode(DAC_PIN), OUTPUT);
    CHECK_EQ(fakePinLevel(DAC_PIN), HIGH);
    CHECK(SPI.frames.empty());
}

TEST(ad5452_write_sends_one_load_word) {
    AD5452 dac(DAC_PIN);
    dac.begin();
    dac.write(0xABC);
    CHECK_EQ(SPI.frames.size(), 1);
    CHECK_EQ(SPI.frames[0].csPin, DAC_PIN);
    CHECK_EQ(SPI.frames[0].bytes.size(), 2);
    CHECK_EQ(SPI.frames[0].bytes[0], 0x2A); //00 1010 1011 1100 00
    CHECK_EQ(SPI.frames[0].bytes[1], 0xF0);
    CHECK_EQ(SPI.frames[0].dataMode, SPI_MODE1);
    CHECK_EQ(SPI.frames[0].clock, AD5452::DEFAULT_SPI_HZ);
    CHECK_EQ(fakePinLevel(DAC_PIN), HIGH);
    CHECK_EQ(dac.code(), 0xABC);
}

TEST(ad5452_clamps_to_12_bits) {
    AD5452 dac(DAC_PIN, &SPI, 1000000);
    dac.begin();
    CHECK_EQ(dac.code(), 0); //power-on state
    dac.write(5000);
    Ad5452Model m = replayAd5452(SPI, DAC_PIN);
    CHECK_EQ(m.code, 4095);
    CHECK_EQ(m.badControl, 0);
    CHECK_EQ(dac.code(), 4095);
    CHECK_EQ(SPI.frames[0].clock, 1000000);
}
