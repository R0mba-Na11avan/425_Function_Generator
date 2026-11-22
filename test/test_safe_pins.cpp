/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: host tests for the boot sequence at the top of setup(): each
             safe pin goes straight to its level (never the other one for a
             moment), no chip select blips LOW outside a transfer, CH2's
             parking begin() leaves its chips in a known state, and CH1
             never disturbs the CH2 pins
**************************************************************************/

#include "test.h"
#include "chip_models.h"
#include "../src/core/safe_pins.h"
#include "../src/core/func_gen.h"

struct SafePin {
    uint8_t pin;
    int level;
};

static const SafePin SAFE_PINS[] = {
    {PIN_OUTPUT_RELAY, LOW},        //CH1 BNC disconnected
    {PIN_OUTPUT_RELAY_2, LOW},      //CH2 BNC disconnected
    {PIN_AD9833_CS, HIGH},          //every generator chip deselected
    {PIN_AMP_DAC_CS, HIGH},
    {PIN_OFFSET_DAC_CS, HIGH},
    {PIN_AD9833_CS_2, HIGH},
    {PIN_AMP_DAC_CS_2, HIGH},
    {PIN_WAVE_SWITCH, HIGH},        //divided path until the AD9833 is reset
    {PIN_WAVE_SWITCH_2, HIGH},
};

TEST(safe_pins_go_straight_to_their_level) {
    holdSafePinStates();
    for (const SafePin& p : SAFE_PINS) {
        CHECK_EQ(fakePinMode(p.pin), OUTPUT);
        CHECK_EQ(fakePinLevel(p.pin), p.level);
        //one drive change: from not driving to the safe level. a HIGH pin
        //that drove LOW first would show two
        CHECK_EQ(fakePinDriveCount(p.pin), 1);
        CHECK_EQ(fakePinDrive(p.pin, 0).level, p.level);
    }
    CHECK(SPI.frames.empty()); //no bus traffic, nothing else touched
}

TEST(drive_output_pin_on_a_claimed_pin_never_glitches) {
    //a pin that is already an output keeps driving its level
    holdSafePinStates();
    driveOutputPin(PIN_WAVE_SWITCH, HIGH);
    driveOutputPin(PIN_OUTPUT_RELAY, LOW);
    CHECK_EQ(fakePinDriveCount(PIN_WAVE_SWITCH), 1);
    CHECK_EQ(fakePinDriveCount(PIN_OUTPUT_RELAY), 1);
}

TEST(drive_output_pin_still_lands_on_an_unclaimed_pin) {
    //core 3.x drops the first write, the second one after pinMode lands
    driveOutputPin(PIN_WAVE_SWITCH, HIGH);
    CHECK_EQ(fakePinMode(PIN_WAVE_SWITCH), OUTPUT);
    CHECK_EQ(fakePinLevel(PIN_WAVE_SWITCH), HIGH);
}


TEST(relays_stay_open_through_boot) {
    //setup() order: safe pins, then begin(). nothing closes a relay until
    //someone turns the output on
    holdSafePinStates();
    FunctionGenerator gen(PIN_AD9833_CS, PIN_AMP_DAC_CS, PIN_OFFSET_DAC_CS, OFFSET_DAC_CHANNEL,
                          PIN_WAVE_SWITCH, PIN_OUTPUT_RELAY);
    gen.begin();
    CHECK(!gen.outputOn());
    CHECK_EQ(fakePinDriveCount(PIN_OUTPUT_RELAY), 1);
    CHECK_EQ(fakePinLevel(PIN_OUTPUT_RELAY), LOW);
    CHECK_EQ(fakePinLevel(PIN_OUTPUT_RELAY_2), LOW);

    gen.setOutput(true);
    CHECK_EQ(fakePinLevel(PIN_OUTPUT_RELAY), HIGH);
    CHECK_EQ(fakePinLevel(PIN_OUTPUT_RELAY_2), LOW); //CH2 stays open
}

//the generators in setup() order: safe pins, CH1 begin(), CH2's parking begin()
struct BootRig {
    FunctionGenerator ch1;
    FunctionGenerator ch2;
    BootRig()
        : ch1(PIN_AD9833_CS, PIN_AMP_DAC_CS, PIN_OFFSET_DAC_CS, OFFSET_DAC_CHANNEL,
              PIN_WAVE_SWITCH, PIN_OUTPUT_RELAY),
          ch2(PIN_AD9833_CS_2, PIN_AMP_DAC_CS_2, PIN_OFFSET_DAC_CS, OFFSET_DAC_CHANNEL_2,
              PIN_WAVE_SWITCH_2, PIN_OUTPUT_RELAY_2) {
        holdSafePinStates();
        ch1.begin();
        ch2.begin();
    }
};

static int framesFor(int csPin) {
    int n = 0;
    for (const SpiFrame& f : SPI.frames) if (f.csPin == csPin) n++;
    return n;
}

static int lowDrives(uint8_t pin) {
    int n = 0;
    for (int i = 0; i < fakePinDriveCount(pin); i++) if (fakePinDrive(pin, i).level == LOW) n++;
    return n;
}

TEST(chip_selects_only_go_low_for_a_transfer) {
    //the drivers' begin() run on pins already held HIGH: a select that went
    //LOW without a word behind it would be a blip the chip could act on
    BootRig rig;
    rig.ch1.setOutput(true);
    rig.ch1.setWaveform(WAVEFORM::SQUARE);
    rig.ch1.setAmplitude(2.0f);
    const uint8_t selects[] = {PIN_AD9833_CS, PIN_AMP_DAC_CS, PIN_OFFSET_DAC_CS, PIN_AD9833_CS_2, PIN_AMP_DAC_CS_2};
    for (uint8_t pin : selects) {
        CHECK(framesFor(pin) > 0);
        CHECK_EQ(lowDrives(pin), framesFor(pin));
        CHECK_EQ(fakePinDrive(pin, 0).level, HIGH); //first drive is the idle level
        CHECK_EQ(fakePinLevel(pin), HIGH);
    }
}

TEST(ch2_is_parked_in_a_known_state) {
    BootRig rig;

    //AD9833: reset, then sine and asleep
    Ad9833Model dds = replayAd9833(SPI, PIN_AD9833_CS_2);
    CHECK(dds.words > 0);
    CHECK(dds.sleeping());
    CHECK(!dds.squareBits());
    CHECK(!dds.triangleBit());
    CHECK_EQ(dds.badMode, 0);

    //amplitude DAC at zero, offset channel B on and at the 0 V code (the
    //same code CH1 writes on A while its output is off), not shut down at -5 V
    Ad5452Model amp = replayAd5452(SPI, PIN_AMP_DAC_CS_2);
    CHECK(amp.writes > 0);
    CHECK_EQ(amp.code, 0);
    Mcp4822Model off = replayMcp4822(SPI, PIN_OFFSET_DAC_CS);
    CHECK(off.active[OFFSET_DAC_CHANNEL_2]);
    CHECK(off.code[OFFSET_DAC_CHANNEL_2] > 0);
    CHECK_EQ(off.code[OFFSET_DAC_CHANNEL_2], off.code[OFFSET_DAC_CHANNEL]);

    //relay never closed, switch on the direct path only after the reset
    CHECK(!rig.ch2.outputOn());
    CHECK_EQ(fakePinDriveCount(PIN_OUTPUT_RELAY_2), 1);
    CHECK_EQ(fakePinLevel(PIN_OUTPUT_RELAY_2), LOW);
    CHECK_EQ(fakePinDriveCount(PIN_WAVE_SWITCH_2), 2); //HIGH, then LOW once
    CHECK_EQ(fakePinLevel(PIN_WAVE_SWITCH_2), LOW);
    int lowAt = fakePinDrive(PIN_WAVE_SWITCH_2, 1).frame;
    bool resetBefore = false;
    for (int i = 0; i < lowAt; i++) {
        const SpiFrame& f = SPI.frames[i];
        if (f.csPin != PIN_AD9833_CS_2) continue;
        for (size_t b = 0; b + 1 < f.bytes.size(); b += 2) {
            uint16_t word = (f.bytes[b] << 8) | f.bytes[b + 1];
            if (!(word >> 14) && (word & (1 << 8))) resetBefore = true; //control word with RESET
        }
    }
    CHECK(resetBefore);
}

TEST(ch2_parking_leaves_ch1_alone) {
    //CH2's begin() shares the bus and the MCP4822, but never writes CH1's channel
    holdSafePinStates();
    FunctionGenerator ch1(PIN_AD9833_CS, PIN_AMP_DAC_CS, PIN_OFFSET_DAC_CS, OFFSET_DAC_CHANNEL,
                          PIN_WAVE_SWITCH, PIN_OUTPUT_RELAY);
    FunctionGenerator ch2(PIN_AD9833_CS_2, PIN_AMP_DAC_CS_2, PIN_OFFSET_DAC_CS, OFFSET_DAC_CHANNEL_2,
                          PIN_WAVE_SWITCH_2, PIN_OUTPUT_RELAY_2);
    ch1.begin();
    int ch1OffsetWrites = replayMcp4822(SPI, PIN_OFFSET_DAC_CS).writes[OFFSET_DAC_CHANNEL];
    int ch1DdsWords = replayAd9833(SPI, PIN_AD9833_CS).words;
    int ch1AmpWrites = replayAd5452(SPI, PIN_AMP_DAC_CS).writes;
    int ch1PinDrives = fakePinDriveCount(PIN_WAVE_SWITCH) + fakePinDriveCount(PIN_OUTPUT_RELAY);
    ch2.begin();
    CHECK_EQ(replayMcp4822(SPI, PIN_OFFSET_DAC_CS).writes[OFFSET_DAC_CHANNEL], ch1OffsetWrites);
    CHECK_EQ(replayAd9833(SPI, PIN_AD9833_CS).words, ch1DdsWords);
    CHECK_EQ(replayAd5452(SPI, PIN_AMP_DAC_CS).writes, ch1AmpWrites);
    CHECK_EQ(fakePinDriveCount(PIN_WAVE_SWITCH) + fakePinDriveCount(PIN_OUTPUT_RELAY), ch1PinDrives);
}

TEST(ch1_never_touches_the_ch2_pins) {
    //after boot (CH2 parked), nothing CH1 does reaches a CH2 pin or chip
    BootRig rig;
    const uint8_t ch2Pins[] = {PIN_OUTPUT_RELAY_2, PIN_AD9833_CS_2, PIN_AMP_DAC_CS_2, PIN_WAVE_SWITCH_2};
    int drivesBefore[4];
    for (int i = 0; i < 4; i++) drivesBefore[i] = fakePinDriveCount(ch2Pins[i]);
    size_t framesBefore = SPI.frames.size();
    int offsetBWrites = replayMcp4822(SPI, PIN_OFFSET_DAC_CS).writes[OFFSET_DAC_CHANNEL_2];

    rig.ch1.setOutput(true);
    rig.ch1.setWaveform(WAVEFORM::SQUARE);
    rig.ch1.setAmplitude(2.0f);
    rig.ch1.setDCOffset(-1.0f);
    rig.ch1.setWaveform(WAVEFORM::SINE);
    rig.ch1.setOutput(false);

    for (int i = 0; i < 4; i++) CHECK_EQ(fakePinDriveCount(ch2Pins[i]), drivesBefore[i]);
    CHECK_EQ(fakePinLevel(PIN_OUTPUT_RELAY_2), LOW);
    CHECK_EQ(fakePinLevel(PIN_AD9833_CS_2), HIGH);
    CHECK_EQ(fakePinLevel(PIN_AMP_DAC_CS_2), HIGH);
    CHECK_EQ(replayMcp4822(SPI, PIN_OFFSET_DAC_CS).writes[OFFSET_DAC_CHANNEL_2], offsetBWrites);
    for (size_t i = framesBefore; i < SPI.frames.size(); i++) {
        CHECK(SPI.frames[i].csPin != PIN_AD9833_CS_2);
        CHECK(SPI.frames[i].csPin != PIN_AMP_DAC_CS_2);
    }
}
