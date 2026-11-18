/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: host tests for the MCP4822 driver on its own: chip select
             handling, the 16-bit word per channel, gain, shutdown and clamping
**************************************************************************/

#include "test.h"
#include "chip_models.h"
#include "../src/libs/MCP4822/MCP4822.h"

static const uint8_t DAC_PIN = 14;

TEST(mcp4822_begin_deselects_the_chip) {
    MCP4822 dac(DAC_PIN);
    CHECK(fakePinMode(DAC_PIN) != OUTPUT); //constructor touches nothing
    dac.begin();
    CHECK_EQ(fakePinMode(DAC_PIN), OUTPUT);
    CHECK_EQ(fakePinLevel(DAC_PIN), HIGH);
    CHECK(SPI.frames.empty());
}

TEST(mcp4822_write_channel_a_1x) {
    MCP4822 dac(DAC_PIN);
    dac.begin();
    dac.write(MCP4822::CHANNEL_A, 0xABC);
    CHECK_EQ(SPI.frames.size(), 1);
    CHECK_EQ(SPI.frames[0].csPin, DAC_PIN);
    CHECK_EQ(SPI.frames[0].bytes.size(), 2);
    CHECK_EQ(SPI.frames[0].bytes[0], 0x3A); //A/B=0, -, GA=1 (1x), SHDN=1, 1010
    CHECK_EQ(SPI.frames[0].bytes[1], 0xBC);
    CHECK_EQ(SPI.frames[0].dataMode, SPI_MODE0);
    CHECK_EQ(SPI.frames[0].clock, MCP4822::DEFAULT_SPI_HZ);
    CHECK_EQ(fakePinLevel(DAC_PIN), HIGH);
    CHECK_EQ(dac.code(MCP4822::CHANNEL_A), 0xABC);
    CHECK_EQ(dac.code(MCP4822::CHANNEL_B), 0);
}

TEST(mcp4822_write_channel_b_2x) {
    MCP4822 dac(DAC_PIN, &SPI, 1000000);
    dac.begin();
    dac.write(MCP4822::CHANNEL_B, 0x123, true);
    CHECK_EQ(SPI.frames[0].bytes[0], 0x91); //A/B=1, -, GA=0 (2x), SHDN=1, 0001
    CHECK_EQ(SPI.frames[0].bytes[1], 0x23);
    CHECK_EQ(SPI.frames[0].clock, 1000000);
    Mcp4822Model m = replayMcp4822(SPI, DAC_PIN);
    CHECK_EQ(m.code[1], 0x123);
    CHECK(m.active[1]);
    CHECK(m.gain2x[1]);
    CHECK(!m.active[0]); //channel A untouched, still shut down
    CHECK_EQ(dac.code(MCP4822::CHANNEL_B), 0x123);
    CHECK_EQ(dac.code(MCP4822::CHANNEL_A), 0);
}

TEST(mcp4822_clamps_to_12_bits) {
    MCP4822 dac(DAC_PIN);
    dac.begin();
    dac.write(MCP4822::CHANNEL_A, 5000);
    Mcp4822Model m = replayMcp4822(SPI, DAC_PIN);
    CHECK_EQ(m.code[0], 4095);
    CHECK_EQ(m.badMode, 0);
    CHECK_EQ(m.badLength, 0);
    CHECK_EQ(dac.code(MCP4822::CHANNEL_A), 4095);
}

TEST(mcp4822_shutdown_clears_shdn) {
    MCP4822 dac(DAC_PIN);
    dac.begin();
    dac.write(MCP4822::CHANNEL_B, 1000);
    dac.shutdown(MCP4822::CHANNEL_B);
    CHECK_EQ(SPI.frames.size(), 2);
    CHECK_EQ(SPI.frames[1].bytes[0], 0x80); //A/B=1, SHDN=0
    CHECK_EQ(SPI.frames[1].bytes[1], 0x00);
    Mcp4822Model m = replayMcp4822(SPI, DAC_PIN);
    CHECK(!m.active[1]);
    CHECK_EQ(dac.code(MCP4822::CHANNEL_B), 0);
}
