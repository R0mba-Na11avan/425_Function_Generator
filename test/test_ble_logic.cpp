/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: host tests for the BLE GATT layout and value encoding: UUIDs,
             descriptors, byte encoding, range checks and applying writes
             through the real controller
**************************************************************************/

#include "test.h"
#include <stdio.h>
#include <string.h>
#include "../src/comms/ble_logic.h"

static FuncGenState sampleState() {
    return FuncGenState{WAVEFORM::TRIANGLE, 1000.1f, 90.0f, 2.5f, -0.25f, true};
}

static FuncGenLimits configLimits() {
    FunctionGenerator gen(PIN_AD9833_CS, PIN_MCP4131_CS);
    FuncGenController ctl(gen);
    return ctl.limits();
}

static void bytes(uint8_t* out, uint32_t v, uint8_t n) {
    for (uint8_t i = 0; i < n; i++) out[i] = (uint8_t)(v >> (8 * i));
}

//writes value (little-endian, size of the field) and returns the result
static BLE_WRITE write(BLE_FIELD field, uint32_t value, const FuncGenState& s, BleCommand& out,
                       const FuncGenLimits& limits = configLimits()) {
    uint8_t data[4];
    bytes(data, value, bleFieldSize(field));
    return bleDecode(field, data, bleFieldSize(field), s, limits, out);
}

static BLE_WRITE write(BLE_FIELD field, uint32_t value, const FuncGenState& s = sampleState()) {
    BleCommand c;
    return write(field, value, s, c);
}

static void uuidString(const uint8_t uuid[16], char* out) {
    //stored least significant byte first, printed most significant first
    char* p = out;
    for (int i = 15; i >= 0; i--) {
        p += sprintf(p, "%02x", uuid[i]);
        if (i == 12 || i == 10 || i == 8 || i == 6) *p++ = '-';
    }
    *p = '\0';
}

TEST(ble_uuids) {
    uint8_t uuid[16];
    char text[40];
    bleServiceUuid(uuid);
    uuidString(uuid, text);
    CHECK(strcmp(text, "54750f00-f75a-46c0-b78a-746996b46f1a") == 0);

    bleFieldUuid(BLE_FIELD::WAVEFORM, uuid);
    uuidString(uuid, text);
    CHECK(strcmp(text, "54750f01-f75a-46c0-b78a-746996b46f1a") == 0);
    bleFieldUuid(BLE_FIELD::OUTPUT_ENABLE, uuid);
    uuidString(uuid, text);
    CHECK(strcmp(text, "54750f06-f75a-46c0-b78a-746996b46f1a") == 0);
}

TEST(ble_descriptors) {
    struct Expect {
        BLE_FIELD field;
        uint8_t size;
        uint8_t format[7];
        const char* unitInName;
    };
    const Expect expected[] = {
        {BLE_FIELD::WAVEFORM, 1, {0x04, 0x00, 0x00, 0x27, 0x01, 0x00, 0x00}, "sine"},
        {BLE_FIELD::FREQUENCY, 4, {0x08, 0xFE, 0x22, 0x27, 0x01, 0x00, 0x00}, "0.01 Hz"},
        {BLE_FIELD::AMPLITUDE, 2, {0x06, 0xFD, 0x28, 0x27, 0x01, 0x00, 0x00}, "mVpp"},
        {BLE_FIELD::PHASE, 2, {0x06, 0xFE, 0x63, 0x27, 0x01, 0x00, 0x00}, "0.01 deg"},
        {BLE_FIELD::DC_OFFSET, 2, {0x0E, 0xFD, 0x28, 0x27, 0x01, 0x00, 0x00}, "mV"},
        {BLE_FIELD::OUTPUT_ENABLE, 1, {0x01, 0x00, 0x00, 0x27, 0x01, 0x00, 0x00}, "0 off, 1 on"},
    };
    for (const Expect& e : expected) {
        CHECK_EQ(bleFieldSize(e.field), e.size);
        CHECK(bleFieldSize(e.field) <= BLE_VALUE_MAX_LEN);
        uint8_t format[7];
        blePresentationFormat(e.field, format);
        CHECK(memcmp(format, e.format, 7) == 0);
        CHECK(strstr(bleFieldName(e.field), e.unitInName) != nullptr);
    }
}

TEST(ble_encode) {
    FuncGenState s = sampleState();
    uint8_t v[BLE_VALUE_MAX_LEN];

    CHECK_EQ(bleEncode(BLE_FIELD::WAVEFORM, s, v), 1);
    CHECK_EQ(v[0], 2);

    //1000.1 Hz = 100010 = 0x000186AA
    CHECK_EQ(bleEncode(BLE_FIELD::FREQUENCY, s, v), 4);
    const uint8_t freq[] = {0xAA, 0x86, 0x01, 0x00};
    CHECK(memcmp(v, freq, 4) == 0);

    //2.5 Vpp = 2500 mV = 0x09C4
    CHECK_EQ(bleEncode(BLE_FIELD::AMPLITUDE, s, v), 2);
    CHECK_EQ(v[0], 0xC4);
    CHECK_EQ(v[1], 0x09);

    //90 deg = 9000 = 0x2328
    CHECK_EQ(bleEncode(BLE_FIELD::PHASE, s, v), 2);
    CHECK_EQ(v[0], 0x28);
    CHECK_EQ(v[1], 0x23);

    //-0.25 V = -250 mV = 0xFF06
    CHECK_EQ(bleEncode(BLE_FIELD::DC_OFFSET, s, v), 2);
    CHECK_EQ(v[0], 0x06);
    CHECK_EQ(v[1], 0xFF);

    CHECK_EQ(bleEncode(BLE_FIELD::OUTPUT_ENABLE, s, v), 1);
    CHECK_EQ(v[0], 1);

    //the top of the range fits: 12.5 MHz = 1250000000 = 0x4A817C80
    s.frequency = FREQ_MAX_HZ;
    bleEncode(BLE_FIELD::FREQUENCY, s, v);
    const uint8_t top[] = {0x80, 0x7C, 0x81, 0x4A};
    CHECK(memcmp(v, top, 4) == 0);
}

TEST(ble_decode_round_trips_the_encoding) {
    FuncGenState s = sampleState();
    FuncGenLimits limits = configLimits();
    for (uint8_t i = 0; i < BLE_FIELD_COUNT; i++) {
        BLE_FIELD field = (BLE_FIELD)i;
        uint8_t v[BLE_VALUE_MAX_LEN];
        uint8_t n = bleEncode(field, s, v);
        BleCommand c;
        CHECK(bleDecode(field, v, n, s, limits, c) == BLE_WRITE::OK);
        CHECK(c.field == field);
        switch (field) {
            case BLE_FIELD::WAVEFORM: CHECK(c.waveform == s.waveform); break;
            case BLE_FIELD::FREQUENCY: CHECK_NEAR(c.number, s.frequency, 0.005); break;
            case BLE_FIELD::AMPLITUDE: CHECK_NEAR(c.number, s.amplitude, 0.0005); break;
            case BLE_FIELD::PHASE: CHECK_NEAR(c.number, s.phase, 0.005); break;
            case BLE_FIELD::DC_OFFSET: CHECK_NEAR(c.number, s.dcOffset, 0.0005); break;
            case BLE_FIELD::OUTPUT_ENABLE: CHECK(c.enabled == s.outputEnabled); break;
        }
    }
}

TEST(ble_decode_rejects_wrong_lengths) {
    FuncGenState s = sampleState();
    FuncGenLimits limits = configLimits();
    uint8_t data[BLE_VALUE_MAX_LEN + 1] = {};
    for (uint8_t i = 0; i < BLE_FIELD_COUNT; i++) {
        BLE_FIELD field = (BLE_FIELD)i;
        uint8_t size = bleFieldSize(field);
        BleCommand c;
        CHECK(bleDecode(field, data, 0, s, limits, c) == BLE_WRITE::BAD_LENGTH);
        CHECK(bleDecode(field, data, size - 1, s, limits, c) == BLE_WRITE::BAD_LENGTH);
        CHECK(bleDecode(field, data, size + 1, s, limits, c) == BLE_WRITE::BAD_LENGTH);
    }
}

TEST(ble_decode_range_edges) {
    //frequency, 0.01 Hz units: 0.1 Hz .. 12.5 MHz
    CHECK(write(BLE_FIELD::FREQUENCY, 9) == BLE_WRITE::OUT_OF_RANGE);
    CHECK(write(BLE_FIELD::FREQUENCY, 10) == BLE_WRITE::OK);
    CHECK(write(BLE_FIELD::FREQUENCY, 1250000000) == BLE_WRITE::OK);
    CHECK(write(BLE_FIELD::FREQUENCY, 1250000001) == BLE_WRITE::OUT_OF_RANGE);
    CHECK(write(BLE_FIELD::FREQUENCY, 0xFFFFFFFF) == BLE_WRITE::OUT_OF_RANGE);

    //amplitude, mV, the limit itself is allowed even though the float limit isn't a round number
    ValueRange amp = configLimits().amplitude[(uint8_t)WAVEFORM::TRIANGLE];
    uint32_t ampMin = (uint32_t)lround(amp.min * 1000.0);
    uint32_t ampMax = (uint32_t)lround(amp.max * 1000.0);
    CHECK(write(BLE_FIELD::AMPLITUDE, ampMin - 1) == BLE_WRITE::OUT_OF_RANGE);
    CHECK(write(BLE_FIELD::AMPLITUDE, ampMin) == BLE_WRITE::OK);
    CHECK(write(BLE_FIELD::AMPLITUDE, ampMax) == BLE_WRITE::OK);
    CHECK(write(BLE_FIELD::AMPLITUDE, ampMax + 1) == BLE_WRITE::OUT_OF_RANGE);
    CHECK(write(BLE_FIELD::AMPLITUDE, 0) == BLE_WRITE::OUT_OF_RANGE);

    //phase, 0.01 deg, max exclusive
    CHECK(write(BLE_FIELD::PHASE, 0) == BLE_WRITE::OK);
    CHECK(write(BLE_FIELD::PHASE, 35999) == BLE_WRITE::OK);
    CHECK(write(BLE_FIELD::PHASE, 36000) == BLE_WRITE::OUT_OF_RANGE);

    //offset, signed mV
    CHECK(write(BLE_FIELD::DC_OFFSET, (uint16_t)-2500) == BLE_WRITE::OK);
    CHECK(write(BLE_FIELD::DC_OFFSET, (uint16_t)-2501) == BLE_WRITE::OUT_OF_RANGE);
    CHECK(write(BLE_FIELD::DC_OFFSET, 2500) == BLE_WRITE::OK);
    CHECK(write(BLE_FIELD::DC_OFFSET, 2501) == BLE_WRITE::OUT_OF_RANGE);
    CHECK(write(BLE_FIELD::DC_OFFSET, 0x8000) == BLE_WRITE::OUT_OF_RANGE);   //-32768
    BleCommand c;
    CHECK(write(BLE_FIELD::DC_OFFSET, (uint16_t)-1250, sampleState(), c) == BLE_WRITE::OK);
    CHECK_NEAR(c.number, -1.25, 1e-6);

    //enums
    CHECK(write(BLE_FIELD::WAVEFORM, 2) == BLE_WRITE::OK);
    CHECK(write(BLE_FIELD::WAVEFORM, 3) == BLE_WRITE::OUT_OF_RANGE);
    CHECK(write(BLE_FIELD::OUTPUT_ENABLE, 0) == BLE_WRITE::OK);
    CHECK(write(BLE_FIELD::OUTPUT_ENABLE, 2) == BLE_WRITE::OUT_OF_RANGE);
    CHECK(write(BLE_FIELD::OUTPUT_ENABLE, 0xFF) == BLE_WRITE::OUT_OF_RANGE);
}

TEST(ble_amplitude_range_follows_the_waveform) {
    FuncGenLimits limits = configLimits();
    limits.amplitude[(uint8_t)WAVEFORM::SQUARE].max = 3.0f;
    FuncGenState s = sampleState();
    BleCommand c;
    s.waveform = WAVEFORM::SINE;
    CHECK(write(BLE_FIELD::AMPLITUDE, 3500, s, c, limits) == BLE_WRITE::OK);
    s.waveform = WAVEFORM::SQUARE;
    CHECK(write(BLE_FIELD::AMPLITUDE, 3500, s, c, limits) == BLE_WRITE::OUT_OF_RANGE);
    CHECK(write(BLE_FIELD::AMPLITUDE, 3000, s, c, limits) == BLE_WRITE::OK);
}

TEST(ble_rejected_write_leaves_command_alone) {
    BleCommand c = {BLE_FIELD::PHASE, WAVEFORM::SQUARE, 12.0f, true};
    CHECK(write(BLE_FIELD::FREQUENCY, 1, sampleState(), c) == BLE_WRITE::OUT_OF_RANGE);
    CHECK(c.field == BLE_FIELD::PHASE);
    CHECK_NEAR(c.number, 12.0, 0);
}

TEST(ble_att_errors) {
    CHECK_EQ(bleAttError(BLE_WRITE::OK), 0);
    CHECK_EQ(bleAttError(BLE_WRITE::BAD_LENGTH), 0x0D);
    CHECK_EQ(bleAttError(BLE_WRITE::OUT_OF_RANGE), 0xFF);
}

TEST(ble_changed_fields) {
    FuncGenState a = sampleState();
    FuncGenState b = a;
    CHECK_EQ(bleChangedFields(a, b), 0);

    b.phase = 180.0f;   //phase isn't on the LCD but is sent over BLE
    CHECK_EQ(bleChangedFields(a, b), 1u << (uint8_t)BLE_FIELD::PHASE);

    //a change smaller than the encoding's step isn't worth a notification
    b = a;
    b.frequency = a.frequency + 0.001f;
    CHECK_EQ(bleChangedFields(a, b), 0);

    b = a;
    b.outputEnabled = false;
    b.amplitude = 1.0f;
    CHECK_EQ(bleChangedFields(a, b), (1u << (uint8_t)BLE_FIELD::OUTPUT_ENABLE) | (1u << (uint8_t)BLE_FIELD::AMPLITUDE));
}

static CHANGE_SOURCE s_lastSource;
static int s_changes;

static void recordSource(const FuncGenState&, CHANGE_SOURCE source, void*) {
    s_lastSource = source;
    s_changes++;
}

TEST(ble_apply_goes_through_the_controller_as_ble) {
    FunctionGenerator gen(PIN_AD9833_CS, PIN_MCP4131_CS);
    gen.begin();
    FuncGenController ctl(gen);
    ctl.addListener(recordSource);
    FuncGenLimits limits = ctl.limits();

    struct Write {
        BLE_FIELD field;
        uint32_t value;
    };
    const Write writes[] = {
        {BLE_FIELD::WAVEFORM, 1},
        {BLE_FIELD::FREQUENCY, 12345678},      //123456.78 Hz
        {BLE_FIELD::AMPLITUDE, 1800},
        {BLE_FIELD::PHASE, 4500},
        {BLE_FIELD::DC_OFFSET, (uint16_t)-500},
        {BLE_FIELD::OUTPUT_ENABLE, 1},
    };
    for (const Write& w : writes) {
        s_changes = 0;
        s_lastSource = CHANGE_SOURCE::LOCAL;
        BleCommand c;
        CHECK(write(w.field, w.value, ctl.state(), c, limits) == BLE_WRITE::OK);
        bleApply(ctl, c);
        CHECK_EQ(s_changes, 1);
        CHECK(s_lastSource == CHANGE_SOURCE::BLE);

        //what a read returns now is what was written
        uint8_t v[BLE_VALUE_MAX_LEN], expect[BLE_VALUE_MAX_LEN];
        uint8_t n = bleEncode(w.field, ctl.state(), v);
        bytes(expect, w.value, n);
        CHECK(memcmp(v, expect, n) == 0);
    }

    FuncGenState s = ctl.state();
    CHECK(s.waveform == WAVEFORM::SQUARE);
    CHECK_NEAR(s.frequency, 123456.78, 0.01);
    CHECK_NEAR(s.amplitude, 1.8, 1e-6);
    CHECK_NEAR(s.phase, 45.0, 1e-6);
    CHECK_NEAR(s.dcOffset, -0.5, 1e-6);
    CHECK(s.outputEnabled);
}

TEST(ble_minimum_amplitude_write_lands_on_the_limit) {
    FunctionGenerator gen(PIN_AD9833_CS, PIN_MCP4131_CS);
    gen.begin();
    FuncGenController ctl(gen);
    ValueRange r = ctl.limits().amplitude[(uint8_t)ctl.state().waveform];
    BleCommand c;
    CHECK(write(BLE_FIELD::AMPLITUDE, (uint32_t)lround(r.min * 1000.0), ctl.state(), c, ctl.limits()) == BLE_WRITE::OK);
    bleApply(ctl, c);
    CHECK_NEAR(ctl.state().amplitude, r.min, 1e-6);
}
