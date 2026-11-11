/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: GATT layout and byte encoding of the settings for BleService
**************************************************************************/

#include "ble_logic.h"

#include <math.h>
#include <string.h>

//54750f00-f75a-46c0-b78a-746996b46f1a, least significant byte first
static const uint8_t SERVICE_UUID[16] = {0x1a, 0x6f, 0xb4, 0x96, 0x69, 0x74, 0x8a, 0xb7,
                                         0xc0, 0x46, 0x5a, 0xf7, 0x00, 0x0f, 0x75, 0x54};
static const uint8_t UUID_ID_BYTE = 12;     //the "00" in 0f00

//GATT Presentation Format values (Bluetooth Assigned Numbers)
static const uint8_t FORMAT_BOOLEAN = 0x01;
static const uint8_t FORMAT_UINT8 = 0x04;
static const uint8_t FORMAT_UINT16 = 0x06;
static const uint8_t FORMAT_UINT32 = 0x08;
static const uint8_t FORMAT_SINT16 = 0x0E;
static const uint16_t UNIT_NONE = 0x2700;
static const uint16_t UNIT_HERTZ = 0x2722;
static const uint16_t UNIT_VOLT = 0x2728;
static const uint16_t UNIT_DEGREE = 0x2763;
static const uint8_t NAMESPACE_BLUETOOTH = 0x01;

struct FieldInfo {
    const char* name;
    uint8_t size;
    uint8_t format;
    int8_t exponent;    //value = raw * 10^exponent
    uint16_t unit;
};

//indexed by BLE_FIELD
static const FieldInfo FIELDS[BLE_FIELD_COUNT] = {
    {"Waveform (0 sine, 1 square, 2 triangle)", 1, FORMAT_UINT8, 0, UNIT_NONE},
    {"Frequency (0.01 Hz)", 4, FORMAT_UINT32, -2, UNIT_HERTZ},
    {"Amplitude (mVpp)", 2, FORMAT_UINT16, -3, UNIT_VOLT},
    {"Phase (0.01 deg)", 2, FORMAT_UINT16, -2, UNIT_DEGREE},
    {"DC offset (mV)", 2, FORMAT_SINT16, -3, UNIT_VOLT},
    {"Output (0 off, 1 on)", 1, FORMAT_BOOLEAN, 0, UNIT_NONE}
};

//raw units per setting unit
static const double FREQ_SCALE = 100.0;     //0.01 Hz
static const double AMP_SCALE = 1000.0;     //mV
static const double PHASE_SCALE = 100.0;    //0.01 deg
static const double OFFSET_SCALE = 1000.0;  //mV

static const FieldInfo& info(BLE_FIELD field) {
    return FIELDS[(uint8_t)field];
}

//limits in raw units. compared as integers so a client sending exactly the
//limit is never rejected by float rounding
static int64_t toRaw(float value, double scale) {
    return llround((double)value * scale);
}

static void putLe(uint8_t* out, uint32_t v, uint8_t size) {
    for (uint8_t i = 0; i < size; i++) out[i] = (uint8_t)(v >> (8 * i));
}

static uint32_t getLe(const uint8_t* data, uint8_t size) {
    uint32_t v = 0;
    for (uint8_t i = 0; i < size; i++) v |= (uint32_t)data[i] << (8 * i);
    return v;
}

static bool inRange(int64_t raw, ValueRange r, double scale, bool maxExclusive) {
    int64_t lo = toRaw(r.min, scale);
    int64_t hi = toRaw(r.max, scale);
    return raw >= lo && (maxExclusive ? raw < hi : raw <= hi);
}

void bleServiceUuid(uint8_t out[16]) {
    memcpy(out, SERVICE_UUID, 16);
}

void bleFieldUuid(BLE_FIELD field, uint8_t out[16]) {
    memcpy(out, SERVICE_UUID, 16);
    out[UUID_ID_BYTE] = (uint8_t)field + 1;
}

const char* bleFieldName(BLE_FIELD field) {
    return info(field).name;
}

void blePresentationFormat(BLE_FIELD field, uint8_t out[7]) {
    const FieldInfo& f = info(field);
    out[0] = f.format;
    out[1] = (uint8_t)f.exponent;
    putLe(out + 2, f.unit, 2);
    out[4] = NAMESPACE_BLUETOOTH;
    putLe(out + 5, 0, 2);   //description: unknown
}

uint8_t bleFieldSize(BLE_FIELD field) {
    return info(field).size;
}

uint8_t bleEncode(BLE_FIELD field, const FuncGenState& s, uint8_t out[BLE_VALUE_MAX_LEN]) {
    uint32_t v = 0;
    switch (field) {
        case BLE_FIELD::WAVEFORM: v = (uint8_t)s.waveform; break;
        case BLE_FIELD::FREQUENCY: v = (uint32_t)toRaw(s.frequency, FREQ_SCALE); break;
        case BLE_FIELD::AMPLITUDE: v = (uint16_t)toRaw(s.amplitude, AMP_SCALE); break;
        case BLE_FIELD::PHASE: v = (uint16_t)toRaw(s.phase, PHASE_SCALE); break;
        case BLE_FIELD::DC_OFFSET: v = (uint16_t)(int16_t)toRaw(s.dcOffset, OFFSET_SCALE); break;
        case BLE_FIELD::OUTPUT_ENABLE: v = s.outputEnabled ? 1 : 0; break;
    }
    uint8_t size = info(field).size;
    putLe(out, v, size);
    return size;
}

uint8_t bleChangedFields(const FuncGenState& a, const FuncGenState& b) {
    uint8_t mask = 0;
    for (uint8_t i = 0; i < BLE_FIELD_COUNT; i++) {
        uint8_t va[BLE_VALUE_MAX_LEN], vb[BLE_VALUE_MAX_LEN];
        uint8_t n = bleEncode((BLE_FIELD)i, a, va);
        bleEncode((BLE_FIELD)i, b, vb);
        if (memcmp(va, vb, n) != 0) mask |= (uint8_t)(1u << i);
    }
    return mask;
}

uint8_t bleAttError(BLE_WRITE result) {
    switch (result) {
        case BLE_WRITE::BAD_LENGTH: return BLE_ATT_ERROR_BAD_LENGTH;
        case BLE_WRITE::OUT_OF_RANGE: return BLE_ATT_ERROR_OUT_OF_RANGE;
        case BLE_WRITE::OK:
        default: return 0;
    }
}

BLE_WRITE bleDecode(BLE_FIELD field, const uint8_t* data, size_t len, const FuncGenState& state,
                    const FuncGenLimits& limits, BleCommand& out) {
    uint8_t size = info(field).size;
    if (len != size) return BLE_WRITE::BAD_LENGTH;
    uint32_t v = getLe(data, size);

    BleCommand c = {field, WAVEFORM::SINE, 0.0f, false};
    switch (field) {
        case BLE_FIELD::WAVEFORM:
            if (v >= WAVEFORM_COUNT) return BLE_WRITE::OUT_OF_RANGE;
            c.waveform = (WAVEFORM)v;
            break;
        case BLE_FIELD::FREQUENCY:
            if (!inRange(v, limits.frequency, FREQ_SCALE, false)) return BLE_WRITE::OUT_OF_RANGE;
            c.number = (float)(v / FREQ_SCALE);
            break;
        case BLE_FIELD::AMPLITUDE:
            if (!inRange(v, limits.amplitudeFor(state), AMP_SCALE, false)) return BLE_WRITE::OUT_OF_RANGE;
            c.number = (float)(v / AMP_SCALE);
            break;
        case BLE_FIELD::PHASE:
            if (!inRange(v, limits.phase, PHASE_SCALE, true)) return BLE_WRITE::OUT_OF_RANGE;
            c.number = (float)(v / PHASE_SCALE);
            break;
        case BLE_FIELD::DC_OFFSET: {
            int16_t raw = (int16_t)(uint16_t)v;
            if (!inRange(raw, limits.dcOffsetFor(state), OFFSET_SCALE, false)) return BLE_WRITE::OUT_OF_RANGE;
            c.number = (float)(raw / OFFSET_SCALE);
            break;
        }
        case BLE_FIELD::OUTPUT_ENABLE:
            if (v > 1) return BLE_WRITE::OUT_OF_RANGE;
            c.enabled = v == 1;
            break;
    }
    out = c;
    return BLE_WRITE::OK;
}

void bleApply(FuncGenController& controller, const BleCommand& c) {
    switch (c.field) {
        case BLE_FIELD::WAVEFORM: controller.setWaveform(c.waveform, CHANGE_SOURCE::BLE); break;
        case BLE_FIELD::FREQUENCY: controller.setFrequency(c.number, CHANGE_SOURCE::BLE); break;
        case BLE_FIELD::AMPLITUDE: controller.setAmplitude(c.number, CHANGE_SOURCE::BLE); break;
        case BLE_FIELD::PHASE: controller.setPhase(c.number, CHANGE_SOURCE::BLE); break;
        case BLE_FIELD::DC_OFFSET: controller.setDCOffset(c.number, CHANGE_SOURCE::BLE); break;
        case BLE_FIELD::OUTPUT_ENABLE: controller.setOutputEnabled(c.enabled, CHANGE_SOURCE::BLE); break;
    }
}
