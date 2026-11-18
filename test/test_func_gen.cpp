/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: host tests for FunctionGenerator: what actually reaches the
             AD9833, the AD5452, the MCP4822, the path switch and the output
             relay, clamping, the peak
             rule and the limit getters
**************************************************************************/

#include "test.h"
#include "chip_models.h"
#include "../src/core/func_gen.h"

static Ad9833Model dds() { return replayAd9833(SPI, PIN_AD9833_CS); }
static Ad5452Model ampDac() { return replayAd5452(SPI, PIN_AMP_DAC_CS); }
static Mcp4822Model offsetChip() { return replayMcp4822(SPI, PIN_OFFSET_DAC_CS); }

//the offset channel this generator owns (CH1 = VA)
struct OffsetDac {
    int code;
    int writes;
    bool active;
    bool gain2x;
};
static OffsetDac offsetDac() {
    Mcp4822Model m = offsetChip();
    int ch = OFFSET_DAC_CHANNEL;
    return OffsetDac{m.code[ch], m.writes[ch], m.active[ch], m.gain2x[ch]};
}

//codes worked out straight from config.h, independent of func_gen.cpp
static int ampCode(float vpp, float fullCodeVpp) {
    return (int)lround(vpp / fullCodeVpp * 4095.0);
}

static int offsetCode(float volts) {
    double c = OFFSET_CAL_CODE_LOW + (volts - (double)OFFSET_V_AT_CAL_LOW) *
               (OFFSET_CAL_CODE_HIGH - OFFSET_CAL_CODE_LOW) / ((double)OFFSET_V_AT_CAL_HIGH - OFFSET_V_AT_CAL_LOW);
    if (c < 0) return 0;
    if (c > OFFSET_DAC_CODE_MAX) return OFFSET_DAC_CODE_MAX;
    return (int)lround(c);
}

struct Gen {
    FunctionGenerator gen;
    Gen() : gen(PIN_AD9833_CS, PIN_AMP_DAC_CS, PIN_OFFSET_DAC_CS, OFFSET_DAC_CHANNEL, PIN_WAVE_SWITCH, PIN_OUTPUT_RELAY) { gen.begin(); }
};

TEST(begin_starts_bus_on_config_pins) {
    FunctionGenerator gen(PIN_AD9833_CS, PIN_AMP_DAC_CS, PIN_OFFSET_DAC_CS, OFFSET_DAC_CHANNEL, PIN_WAVE_SWITCH, PIN_OUTPUT_RELAY);
    CHECK(!SPI.running); //nothing touches the bus before begin()
    gen.begin();
    CHECK(SPI.running);
    CHECK_EQ(SPI.sck, PIN_GEN_SCK);
    CHECK_EQ(SPI.miso, PIN_GEN_MISO);
    CHECK_EQ(SPI.mosi, PIN_GEN_MOSI);
}

TEST(begin_outputs_sine_at_defaults_with_output_off) {
    Gen g;
    Ad9833Model m = dds();
    CHECK(m.words > 0);
    CHECK(!m.squareBits());
    CHECK(!m.triangleBit());
    CHECK(m.sleeping());
    CHECK_EQ(m.freq[0], ad9833FreqWord(g.gen.frequency()));
    CHECK_EQ(m.badMode, 0);
    CHECK(!g.gen.outputOn());
    CHECK_NEAR(g.gen.amplitude(), AMP_DEFAULT_VPP, 1e-6);
    CHECK_NEAR(g.gen.DCOffset(), 0.0, 1e-6);

    //output off = 0 V: no wave, offset DAC at its 0 V code
    CHECK(ampDac().writes > 0);
    CHECK_EQ(ampDac().code, 0);
    CHECK(offsetDac().writes > 0);
    CHECK_EQ(offsetDac().code, offsetCode(0.0f));

    CHECK_EQ(fakePinMode(PIN_WAVE_SWITCH), OUTPUT);
    CHECK_EQ(fakePinLevel(PIN_WAVE_SWITCH), LOW); //sine path
}

TEST(begin_sets_the_offset_dac_before_waking_anything) {
    //the offset DAC powers up shut down (about 0 V out = one end of the range),
    //so 0 V has to be written before the AD9833 gets its settings
    Gen g;
    int offsetFrame = -1, lastDdsFrame = -1;
    for (size_t i = 0; i < SPI.frames.size(); i++) {
        if (SPI.frames[i].csPin == PIN_OFFSET_DAC_CS && offsetFrame < 0) offsetFrame = (int)i;
        if (SPI.frames[i].csPin == PIN_AD9833_CS) lastDdsFrame = (int)i;
    }
    CHECK(offsetFrame >= 0);
    CHECK(offsetFrame < lastDdsFrame);
}

TEST(chip_selects_idle_high_after_begin) {
    Gen g;
    g.gen.setOutput(true);
    g.gen.setFrequency(1234);
    g.gen.setAmplitude(2.0);
    g.gen.setDCOffset(1.0);
    CHECK_EQ(fakePinLevel(PIN_AD9833_CS), HIGH);
    CHECK_EQ(fakePinLevel(PIN_AMP_DAC_CS), HIGH);
    CHECK_EQ(fakePinLevel(PIN_OFFSET_DAC_CS), HIGH);
    CHECK_EQ(SPI.nestedTransactions, 0);
}

TEST(dac_words_are_well_formed) {
    Gen g;
    g.gen.setOutput(true);
    for (float v = -12.0f; v <= 12.0f; v += 0.7f) {
        g.gen.setAmplitude(fabsf(v));
        g.gen.setDCOffset(v);
    }
    g.gen.setWaveform(WAVEFORM::SQUARE);
    g.gen.setOutput(false);
    Ad5452Model a = ampDac();
    CHECK(a.writes > 10);
    CHECK_EQ(a.badMode, 0);
    CHECK_EQ(a.badControl, 0);
    CHECK_EQ(a.badLength, 0);

    Mcp4822Model o = offsetChip();
    CHECK(o.writes[OFFSET_DAC_CHANNEL] > 10);
    CHECK_EQ(o.badMode, 0);
    CHECK_EQ(o.badLength, 0);
    CHECK(o.active[OFFSET_DAC_CHANNEL]);
    CHECK_EQ(o.gain2x[OFFSET_DAC_CHANNEL], (bool)OFFSET_DAC_GAIN_2X);
}

TEST(offset_only_touches_its_own_mcp4822_channel) {
    //the MCP4822 is shared with CH2 (the other channel), which CH1 must never write
    Gen g;
    g.gen.setOutput(true);
    g.gen.setDCOffset(2.0);
    g.gen.setOutput(false);
    Mcp4822Model m = offsetChip();
    CHECK(m.writes[OFFSET_DAC_CHANNEL] > 0);
    CHECK_EQ(m.writes[1 - OFFSET_DAC_CHANNEL], 0);
}

TEST(offset_channel_b_generator_writes_channel_b) {
    FunctionGenerator gen(PIN_AD9833_CS, PIN_AMP_DAC_CS, PIN_OFFSET_DAC_CS, OFFSET_DAC_CHANNEL_2, PIN_WAVE_SWITCH_2, PIN_OUTPUT_RELAY_2);
    gen.begin();
    gen.setOutput(true);
    gen.setDCOffset(-1.5);
    Mcp4822Model m = offsetChip();
    CHECK_EQ(m.writes[OFFSET_DAC_CHANNEL], 0);
    CHECK(m.writes[OFFSET_DAC_CHANNEL_2] > 0);
    CHECK_EQ(m.code[OFFSET_DAC_CHANNEL_2], offsetCode(-1.5f));
}

TEST(offset_calibration_points_map_to_their_codes) {
    //the measured points have to land back on the codes they were measured at
    CHECK_EQ(offsetCode(OFFSET_V_AT_CAL_LOW), OFFSET_CAL_CODE_LOW);
    CHECK_EQ(offsetCode(OFFSET_V_AT_CAL_HIGH), OFFSET_CAL_CODE_HIGH);
    Gen g;
    g.gen.setOutput(true);
    g.gen.setAmplitude(0.0);
    g.gen.setDCOffset(DC_OFFSET_MAX_V);
    CHECK_EQ(offsetDac().code, offsetCode(DC_OFFSET_MAX_V));
    CHECK(offsetDac().code <= OFFSET_DAC_CODE_MAX);
    g.gen.setDCOffset(DC_OFFSET_MIN_V);
    CHECK_EQ(offsetDac().code, offsetCode(DC_OFFSET_MIN_V));
}

TEST(begin_keeps_the_output_relay_open_from_the_start) {
    FunctionGenerator gen(PIN_AD9833_CS, PIN_AMP_DAC_CS, PIN_OFFSET_DAC_CS, OFFSET_DAC_CHANNEL,
                          PIN_WAVE_SWITCH, PIN_OUTPUT_RELAY);
    CHECK(fakePinMode(PIN_OUTPUT_RELAY) != OUTPUT); //constructor touches nothing
    gen.begin();
    CHECK_EQ(fakePinMode(PIN_OUTPUT_RELAY), OUTPUT);
    CHECK_EQ(fakePinLevel(PIN_OUTPUT_RELAY), LOW);
    CHECK_EQ(fakePinFirstWriteFrame(PIN_OUTPUT_RELAY), 0); //set LOW before the first SPI word
}

TEST(output_relay_follows_the_output) {
    Gen g;
    g.gen.setOutput(true);
    CHECK_EQ(fakePinLevel(PIN_OUTPUT_RELAY), HIGH);
    //closed only after every chip got its new settings
    CHECK_EQ(fakePinWriteFrame(PIN_OUTPUT_RELAY), (int)SPI.frames.size());

    //a waveform change while on keeps the output connected
    g.gen.setWaveform(WAVEFORM::SQUARE);
    CHECK_EQ(fakePinLevel(PIN_OUTPUT_RELAY), HIGH);

    //opened before any chip is touched
    int framesBefore = (int)SPI.frames.size();
    g.gen.setOutput(false);
    CHECK_EQ(fakePinLevel(PIN_OUTPUT_RELAY), LOW);
    CHECK_EQ(fakePinWriteFrame(PIN_OUTPUT_RELAY), framesBefore);
    CHECK((int)SPI.frames.size() > framesBefore);

    //changes while off never close it
    g.gen.setWaveform(WAVEFORM::TRIANGLE);
    g.gen.setAmplitude(2.0);
    g.gen.setDCOffset(1.0);
    CHECK_EQ(fakePinLevel(PIN_OUTPUT_RELAY), LOW);
}

TEST(output_enable_toggles_sleep_bits) {
    Gen g;
    g.gen.setOutput(true);
    CHECK(!dds().sleeping());
    CHECK(g.gen.outputOn());
    g.gen.setOutput(false);
    CHECK(dds().sleeping());
}

TEST(output_off_holds_0v_and_keeps_the_settings) {
    Gen g;
    g.gen.setOutput(true);
    g.gen.setAmplitude(3.0);
    g.gen.setDCOffset(2.0);
    CHECK_EQ(ampDac().code, ampCode(3.0f, AMP_FULL_CODE_VPP_SINE));
    CHECK_EQ(offsetDac().code, offsetCode(2.0f));

    g.gen.setOutput(false);
    CHECK_EQ(ampDac().code, 0);
    CHECK_EQ(offsetDac().code, offsetCode(0.0f));
    CHECK_NEAR(g.gen.amplitude(), 3.0, 1e-6);
    CHECK_NEAR(g.gen.DCOffset(), 2.0, 1e-6);

    //changes while off are stored, not output
    g.gen.setAmplitude(4.0);
    g.gen.setDCOffset(-1.0);
    CHECK_EQ(ampDac().code, 0);
    CHECK_EQ(offsetDac().code, offsetCode(0.0f));

    g.gen.setOutput(true);
    CHECK_EQ(ampDac().code, ampCode(4.0f, AMP_FULL_CODE_VPP_SINE));
    CHECK_EQ(offsetDac().code, offsetCode(-1.0f));
}

TEST(waveform_change_keeps_output_state) {
    Gen g;

    //output off: switching waveform must not wake the chip
    g.gen.setWaveform(WAVEFORM::SQUARE);
    CHECK(dds().squareBits());
    CHECK(dds().sleeping());
    CHECK_EQ(ampDac().code, 0);

    //output on: switching waveform must not put it to sleep
    g.gen.setOutput(true);
    g.gen.setWaveform(WAVEFORM::TRIANGLE);
    CHECK(dds().triangleBit());
    CHECK(!dds().squareBits());
    CHECK(!dds().sleeping());
    CHECK(ampDac().code > 0);

    g.gen.setWaveform(WAVEFORM::SINE);
    CHECK(!dds().triangleBit());
    CHECK(!dds().squareBits());
    CHECK(g.gen.waveform() == WAVEFORM::SINE);
}

TEST(waveform_switch_selects_the_path) {
    Gen g;
    g.gen.setWaveform(WAVEFORM::SQUARE);
    CHECK_EQ(fakePinLevel(PIN_WAVE_SWITCH), HIGH);
    g.gen.setWaveform(WAVEFORM::TRIANGLE);
    CHECK_EQ(fakePinLevel(PIN_WAVE_SWITCH), LOW);
    g.gen.setWaveform(WAVEFORM::SQUARE);
    g.gen.setWaveform(WAVEFORM::SINE);
    CHECK_EQ(fakePinLevel(PIN_WAVE_SWITCH), LOW);
}

TEST(waveform_change_uses_that_waveforms_calibration) {
    Gen g;
    g.gen.setOutput(true);
    g.gen.setAmplitude(2.0);
    g.gen.setWaveform(WAVEFORM::SQUARE);
    CHECK_EQ(ampDac().code, ampCode(2.0f, AMP_FULL_CODE_VPP_SQUARE));
    g.gen.setWaveform(WAVEFORM::TRIANGLE);
    CHECK_EQ(ampDac().code, ampCode(2.0f, AMP_FULL_CODE_VPP_TRIANGLE));
}

TEST(frequency_writes_tuning_word) {
    Gen g;
    g.gen.setFrequency(1000.0);
    CHECK_EQ(dds().freq[0], ad9833FreqWord(1000.0));
    CHECK_NEAR(g.gen.frequency(), 1000.0, 1e-3);

    g.gen.setFrequency(1000000.0);
    CHECK_EQ(dds().freq[0], ad9833FreqWord(1000000.0));
}

TEST(frequency_clamps_to_limits) {
    Gen g;
    g.gen.setFrequency(0.0);
    CHECK_NEAR(g.gen.frequency(), FREQ_MIN_HZ, 1e-6);
    g.gen.setFrequency(-50.0);
    CHECK_NEAR(g.gen.frequency(), FREQ_MIN_HZ, 1e-6);
    g.gen.setFrequency(20e6);
    CHECK_NEAR(g.gen.frequency(), FREQ_MAX_HZ, 1e-3);
    CHECK_EQ(dds().freq[0], ad9833FreqWord(FREQ_MAX_HZ));
}

TEST(phase_wraps_into_range) {
    Gen g;
    g.gen.setPhase(90.0);
    CHECK_NEAR(g.gen.phase(), 90.0, 1e-4);
    CHECK_EQ(dds().phase[0], lround(90.0 * 4095.0 / 360.0));
    g.gen.setPhase(370.0);
    CHECK_NEAR(g.gen.phase(), 10.0, 1e-4);
    g.gen.setPhase(-90.0);
    CHECK_NEAR(g.gen.phase(), 270.0, 1e-4);
    g.gen.setPhase(360.0);
    CHECK_NEAR(g.gen.phase(), 0.0, 1e-4);
}

TEST(amplitude_clamps_and_maps_to_code) {
    Gen g;
    g.gen.setOutput(true);
    ValueRange r = FunctionGenerator::amplitudeRange(WAVEFORM::SINE);

    g.gen.setAmplitude(r.max + 10);
    CHECK_NEAR(g.gen.amplitude(), r.max, 1e-6);
    CHECK_EQ(ampDac().code, ampCode(r.max, AMP_FULL_CODE_VPP_SINE));

    g.gen.setAmplitude(-1.0);
    CHECK_NEAR(g.gen.amplitude(), r.min, 1e-6);
    CHECK_EQ(ampDac().code, ampCode(r.min, AMP_FULL_CODE_VPP_SINE));

    g.gen.setAmplitude(1.5);
    CHECK_EQ(ampDac().code, ampCode(1.5f, AMP_FULL_CODE_VPP_SINE));
}

TEST(full_code_amplitude_is_code_4095) {
    //AMP_FULL_CODE_VPP_* is the output at code 4095
    CHECK_EQ(ampCode(AMP_FULL_CODE_VPP_SINE, AMP_FULL_CODE_VPP_SINE), 4095);
    Gen g;
    g.gen.setOutput(true);
    g.gen.setAmplitude(AMP_FULL_CODE_VPP_SINE);
    if (AMP_MAX_VPP_SINE >= AMP_FULL_CODE_VPP_SINE) CHECK_EQ(ampDac().code, 4095);
    g.gen.setAmplitude(0.0);
    if (AMP_MIN_VPP_SINE <= 0.0f) CHECK_EQ(ampDac().code, 0);
}

TEST(dac_codes_never_exceed_12_bits) {
    Gen g;
    g.gen.setOutput(true);
    for (float v = -25.0f; v < 25.0f; v += 0.05f) {
        g.gen.setAmplitude(v);
        g.gen.setDCOffset(v);
        int a = ampDac().code;
        int o = offsetDac().code;
        CHECK(a >= 0 && a <= 4095);
        CHECK(o >= 0 && o <= OFFSET_DAC_CODE_MAX);
    }
}

TEST(waveform_change_reclamps_amplitude) {
    Gen g;
    g.gen.setAmplitude(100.0);
    for (uint8_t i = 0; i < WAVEFORM_COUNT; i++) {
        WAVEFORM w = (WAVEFORM)i;
        int writesBefore = ampDac().writes;
        g.gen.setWaveform(w);
        ValueRange r = FunctionGenerator::amplitudeRange(w, g.gen.DCOffset());
        CHECK(g.gen.amplitude() >= r.min && g.gen.amplitude() <= r.max);
        CHECK(ampDac().writes > writesBefore); //DAC rewritten for the new waveform
    }
}

TEST(dc_offset_maps_to_code) {
    Gen g;
    g.gen.setOutput(true);
    g.gen.setAmplitude(1.0);
    g.gen.setDCOffset(2.5);
    CHECK_NEAR(g.gen.DCOffset(), 2.5, 1e-6);
    CHECK_EQ(offsetDac().code, offsetCode(2.5f));
    g.gen.setDCOffset(-3.25);
    CHECK_EQ(offsetDac().code, offsetCode(-3.25f));
    g.gen.setDCOffset(0.0);
    CHECK_EQ(offsetDac().code, offsetCode(0.0f));
}

TEST(dc_offset_clamps_to_limits) {
    Gen g;
    g.gen.setAmplitude(1.0);
    float room = OUTPUT_PEAK_MAX_V - 0.5f;
    g.gen.setDCOffset(100.0);
    CHECK_NEAR(g.gen.DCOffset(), fminf(DC_OFFSET_MAX_V, room), 1e-6);
    g.gen.setDCOffset(-100.0);
    CHECK_NEAR(g.gen.DCOffset(), fmaxf(DC_OFFSET_MIN_V, -room), 1e-6);
}

TEST(amplitude_stops_at_the_peak_limit_and_offset_stays) {
    Gen g;
    g.gen.setAmplitude(1.0);
    g.gen.setDCOffset(6.0);
    g.gen.setAmplitude(100.0);
    CHECK_NEAR(g.gen.DCOffset(), 6.0, 1e-6);
    CHECK_NEAR(g.gen.amplitude(), fminf(AMP_MAX_VPP_SINE, 2.0f * (OUTPUT_PEAK_MAX_V - 6.0f)), 1e-5);
    CHECK(g.gen.amplitude() / 2 + fabsf(g.gen.DCOffset()) <= OUTPUT_PEAK_MAX_V + 1e-5f);

    //same for a negative offset
    g.gen.setAmplitude(1.0);
    g.gen.setDCOffset(-7.0);
    CHECK_NEAR(g.gen.DCOffset(), -7.0, 1e-6);
    g.gen.setAmplitude(100.0);
    CHECK_NEAR(g.gen.amplitude(), fminf(AMP_MAX_VPP_SINE, 2.0f * (OUTPUT_PEAK_MAX_V - 7.0f)), 1e-5);
}

TEST(offset_stops_at_the_peak_limit_and_amplitude_stays) {
    Gen g;
    g.gen.setAmplitude(AMP_MAX_VPP_SINE);
    g.gen.setDCOffset(100.0);
    CHECK_NEAR(g.gen.amplitude(), AMP_MAX_VPP_SINE, 1e-6);
    CHECK_NEAR(g.gen.DCOffset(), fminf(DC_OFFSET_MAX_V, OUTPUT_PEAK_MAX_V - AMP_MAX_VPP_SINE / 2), 1e-5);
    g.gen.setDCOffset(-100.0);
    CHECK_NEAR(g.gen.DCOffset(), fmaxf(DC_OFFSET_MIN_V, -(OUTPUT_PEAK_MAX_V - AMP_MAX_VPP_SINE / 2)), 1e-5);
}

TEST(peak_limited_ranges) {
    ValueRange amp = {0.0f, 12.0f};
    ValueRange off = {-10.0f, 10.0f};

    //plenty of room: the outer range is left alone
    ValueRange a = peakLimitedAmplitude(amp, 0.0f, 10.0f);
    CHECK_NEAR(a.min, 0.0, 0);
    CHECK_NEAR(a.max, 12.0, 0);

    //offset eats into the amplitude, same for either sign
    a = peakLimitedAmplitude(amp, 7.0f, 10.0f);
    CHECK_NEAR(a.max, 6.0, 1e-6);
    a = peakLimitedAmplitude(amp, -7.0f, 10.0f);
    CHECK_NEAR(a.max, 6.0, 1e-6);

    //offset at or past the edge: only the minimum is left, never an empty range
    a = peakLimitedAmplitude(ValueRange{0.5f, 12.0f}, 10.0f, 10.0f);
    CHECK_NEAR(a.min, 0.5, 0);
    CHECK_NEAR(a.max, 0.5, 0);

    ValueRange o = peakLimitedOffset(off, 12.0f, 10.0f);
    CHECK_NEAR(o.min, -4.0, 1e-6);
    CHECK_NEAR(o.max, 4.0, 1e-6);
    o = peakLimitedOffset(off, 0.0f, 10.0f);
    CHECK_NEAR(o.min, -10.0, 0);
    CHECK_NEAR(o.max, 10.0, 0);

    //outer range narrower than the room: kept
    o = peakLimitedOffset(ValueRange{-1.0f, 2.0f}, 1.0f, 10.0f);
    CHECK_NEAR(o.min, -1.0, 0);
    CHECK_NEAR(o.max, 2.0, 0);

    //amplitude bigger than the whole swing: offset pinned to 0, never inverted
    o = peakLimitedOffset(off, 30.0f, 10.0f);
    CHECK_NEAR(o.min, 0.0, 0);
    CHECK_NEAR(o.max, 0.0, 0);
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
    CHECK_NEAR(FunctionGenerator::dcOffsetRange().max, DC_OFFSET_MAX_V, 0);
    CHECK_NEAR(FunctionGenerator::outputPeakMax(), OUTPUT_PEAK_MAX_V, 0);
    for (uint8_t i = 0; i < WAVEFORM_COUNT; i++) {
        ValueRange r = FunctionGenerator::amplitudeRange((WAVEFORM)i);
        CHECK(r.min < r.max);
    }
}
