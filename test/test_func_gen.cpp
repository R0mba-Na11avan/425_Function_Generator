/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: host tests for FunctionGenerator: what actually reaches the
             AD9833 and MCP4131, clamping and the limit getters
**************************************************************************/

#include "test.h"
#include "chip_models.h"
#include "../src/core/func_gen.h"

static Ad9833Model dds() { return replayAd9833(SPI, PIN_AD9833_CS); }
static Mcp4131Model pot() { return replayMcp4131(SPI, PIN_MCP4131_CS); }

TEST(begin_restarts_bus_on_config_pins) {
    FunctionGenerator gen(PIN_AD9833_CS, PIN_MCP4131_CS);
    CHECK(SPI.running); //MCP4131 constructor already started it
    gen.begin();
    CHECK(SPI.endCalls >= 1);
    CHECK_EQ(SPI.sck, PIN_GEN_SCK);
    CHECK_EQ(SPI.miso, PIN_GEN_MISO);
    CHECK_EQ(SPI.mosi, PIN_GEN_MOSI);
}

TEST(begin_outputs_sine_at_defaults_with_output_off) {
    FunctionGenerator gen(PIN_AD9833_CS, PIN_MCP4131_CS);
    gen.begin();
    Ad9833Model m = dds();
    CHECK(m.words > 0);
    CHECK(!m.squareBits());
    CHECK(!m.triangleBit());
    CHECK(m.sleeping());
    CHECK_EQ(m.freq[0], ad9833FreqWord(gen.frequency()));
    CHECK_EQ(m.badMode, 0);
    CHECK(!gen.outputOn());

    Mcp4131Model p = pot();
    CHECK_EQ(p.wiper, 0); //lowest amplitude
    CHECK_EQ(p.badMode, 0);
}

TEST(chip_selects_idle_high_after_begin) {
    FunctionGenerator gen(PIN_AD9833_CS, PIN_MCP4131_CS);
    gen.begin();
    gen.setFrequency(1234);
    gen.setAmplitude(2.0);
    CHECK_EQ(fakePinLevel(PIN_AD9833_CS), HIGH);
    CHECK_EQ(fakePinLevel(PIN_MCP4131_CS), HIGH);
    CHECK_EQ(SPI.nestedTransactions, 0);
}

TEST(output_enable_toggles_sleep_bits) {
    FunctionGenerator gen(PIN_AD9833_CS, PIN_MCP4131_CS);
    gen.begin();
    gen.setOutput(true);
    CHECK(!dds().sleeping());
    CHECK(gen.outputOn());
    gen.setOutput(false);
    CHECK(dds().sleeping());
}

TEST(waveform_change_keeps_output_state) {
    FunctionGenerator gen(PIN_AD9833_CS, PIN_MCP4131_CS);
    gen.begin();

    //output off: switching waveform must not wake the chip
    gen.setWaveform(WAVEFORM::SQUARE);
    CHECK(dds().squareBits());
    CHECK(dds().sleeping());

    //output on: switching waveform must not put it to sleep
    gen.setOutput(true);
    gen.setWaveform(WAVEFORM::TRIANGLE);
    CHECK(dds().triangleBit());
    CHECK(!dds().squareBits());
    CHECK(!dds().sleeping());

    gen.setWaveform(WAVEFORM::SINE);
    CHECK(!dds().triangleBit());
    CHECK(!dds().squareBits());
    CHECK(gen.waveform() == WAVEFORM::SINE);
}

TEST(frequency_writes_tuning_word) {
    FunctionGenerator gen(PIN_AD9833_CS, PIN_MCP4131_CS);
    gen.begin();
    gen.setFrequency(1000.0);
    CHECK_EQ(dds().freq[0], ad9833FreqWord(1000.0));
    CHECK_NEAR(gen.frequency(), 1000.0, 1e-3);

    gen.setFrequency(1000000.0);
    CHECK_EQ(dds().freq[0], ad9833FreqWord(1000000.0));
}

TEST(frequency_clamps_to_limits) {
    FunctionGenerator gen(PIN_AD9833_CS, PIN_MCP4131_CS);
    gen.begin();
    gen.setFrequency(0.0);
    CHECK_NEAR(gen.frequency(), FREQ_MIN_HZ, 1e-6);
    gen.setFrequency(-50.0);
    CHECK_NEAR(gen.frequency(), FREQ_MIN_HZ, 1e-6);
    gen.setFrequency(20e6);
    CHECK_NEAR(gen.frequency(), FREQ_MAX_HZ, 1e-3);
    CHECK_EQ(dds().freq[0], ad9833FreqWord(FREQ_MAX_HZ));
}

TEST(phase_wraps_into_range) {
    FunctionGenerator gen(PIN_AD9833_CS, PIN_MCP4131_CS);
    gen.begin();
    gen.setPhase(90.0);
    CHECK_NEAR(gen.phase(), 90.0, 1e-4);
    CHECK_EQ(dds().phase[0], lround(90.0 * 4095.0 / 360.0));
    gen.setPhase(370.0);
    CHECK_NEAR(gen.phase(), 10.0, 1e-4);
    gen.setPhase(-90.0);
    CHECK_NEAR(gen.phase(), 270.0, 1e-4);
    gen.setPhase(360.0);
    CHECK_NEAR(gen.phase(), 0.0, 1e-4);
}

TEST(amplitude_clamps_and_maps_to_wiper) {
    FunctionGenerator gen(PIN_AD9833_CS, PIN_MCP4131_CS);
    gen.begin();
    ValueRange r = FunctionGenerator::amplitudeRange(WAVEFORM::SINE);

    gen.setAmplitude(r.max + 10);
    CHECK_NEAR(gen.amplitude(), r.max, 1e-6);
    CHECK_EQ(pot().wiper, MCP4131_WIPER_MAX);

    gen.setAmplitude(0.0);
    CHECK_NEAR(gen.amplitude(), r.min, 1e-6);
    CHECK_EQ(pot().wiper, 0);

    gen.setAmplitude((r.min + r.max) / 2);
    CHECK_EQ(pot().wiper, (MCP4131_WIPER_MAX + 1) / 2);
}

TEST(wiper_never_exceeds_7_bits) {
    FunctionGenerator gen(PIN_AD9833_CS, PIN_MCP4131_CS);
    gen.begin();
    for (float v = -1.0f; v < 20.0f; v += 0.05f) {
        gen.setAmplitude(v);
        int w = pot().wiper;
        CHECK(w >= 0 && w <= MCP4131_WIPER_MAX);
    }
}

TEST(waveform_change_reclamps_amplitude) {
    FunctionGenerator gen(PIN_AD9833_CS, PIN_MCP4131_CS);
    gen.begin();
    gen.setAmplitude(100.0);
    for (uint8_t i = 0; i < WAVEFORM_COUNT; i++) {
        WAVEFORM w = (WAVEFORM)i;
        int writesBefore = pot().writes;
        gen.setWaveform(w);
        ValueRange r = FunctionGenerator::amplitudeRange(w);
        CHECK(gen.amplitude() >= r.min && gen.amplitude() <= r.max);
        CHECK(pot().writes > writesBefore); //pot rewritten for the new range
    }
}

TEST(dc_offset_clamps_to_limits) {
    FunctionGenerator gen(PIN_AD9833_CS, PIN_MCP4131_CS);
    gen.begin();
    gen.setDCOffset(1.0);
    CHECK_NEAR(gen.DCOffset(), 1.0, 1e-6);
    gen.setDCOffset(100.0);
    CHECK_NEAR(gen.DCOffset(), DC_OFFSET_MAX_V, 1e-6);
    gen.setDCOffset(-100.0);
    CHECK_NEAR(gen.DCOffset(), DC_OFFSET_MIN_V, 1e-6);
}

TEST(limit_getters_match_config) {
    CHECK_NEAR(FunctionGenerator::frequencyRange().min, FREQ_MIN_HZ, 0);
    CHECK_NEAR(FunctionGenerator::frequencyRange().max, FREQ_MAX_HZ, 0);
    CHECK_NEAR(FunctionGenerator::phaseRange().min, PHASE_MIN_DEG, 0);
    CHECK_NEAR(FunctionGenerator::phaseRange().max, PHASE_MAX_DEG, 0);
    CHECK_NEAR(FunctionGenerator::amplitudeRange(WAVEFORM::SINE).max, AMP_MAX_VPP_SINE, 0);
    CHECK_NEAR(FunctionGenerator::amplitudeRange(WAVEFORM::SQUARE).max, AMP_MAX_VPP_SQUARE, 0);
    CHECK_NEAR(FunctionGenerator::amplitudeRange(WAVEFORM::TRIANGLE).max, AMP_MAX_VPP_TRIANGLE, 0);
    CHECK_NEAR(FunctionGenerator::dcOffsetRange().min, DC_OFFSET_MIN_V, 0);
    for (uint8_t i = 0; i < WAVEFORM_COUNT; i++) {
        ValueRange r = FunctionGenerator::amplitudeRange((WAVEFORM)i);
        CHECK(r.min < r.max);
    }
}
