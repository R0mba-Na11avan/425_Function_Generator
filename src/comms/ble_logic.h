/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: the hardware-free half of BleService: the GATT layout (UUIDs,
             names, formats) and how each setting is encoded in bytes and
             checked when a phone writes it. kept separate so the host tests
             can run it without an ESP32
**************************************************************************/

/*
    GATT layout (also in the README, keep both in sync)

    Service 54750f00-f75a-46c0-b78a-746996b46f1a, one characteristic per
    setting, all read + write + notify. Numbers are little-endian integers in
    fixed units, so a client never needs floats:

        id  setting     bytes   type    unit        example
        01  waveform    1       u8      0 sine, 1 square, 2 triangle
        02  frequency   4       u32     0.01 Hz     1 kHz = 100000
        03  amplitude   2       u16     mVpp        2.5 Vpp = 2500
        04  phase       2       u16     0.01 deg    90 deg = 9000 (max exclusive)
        05  DC offset   2       s16     mV          -0.25 V = -250
        06  output      1       u8      0 off, 1 on

    Characteristic UUIDs are the service UUID with 0f00 replaced by 0f<id>.
    Each has a User Description (0x2901) and a Presentation Format (0x2904)
    descriptor, so generic apps like nRF Connect show names and units.

    Writes must have exactly the listed size. A wrong size or a value outside
    the limits is rejected with an ATT error, never clamped (same rule as the
    MCP tools). The amplitude range depends on the current waveform.

    Multichannel later: a second channel would be a second service with its
    own UUID and the same characteristics.
*/

#ifndef BLE_LOGIC_H
#define BLE_LOGIC_H

#include <stdint.h>
#include <stddef.h>
#include "../core/funcgen_controller.h"

enum class BLE_FIELD : uint8_t {
    WAVEFORM,
    FREQUENCY,
    AMPLITUDE,
    PHASE,
    DC_OFFSET,
    OUTPUT_ENABLE
};

static const uint8_t BLE_FIELD_COUNT = 6;
static const uint8_t BLE_VALUE_MAX_LEN = 4;

//ATT error codes for rejected writes
static const uint8_t BLE_ATT_ERROR_BAD_LENGTH = 0x0D;      //Invalid Attribute Value Length
static const uint8_t BLE_ATT_ERROR_OUT_OF_RANGE = 0xFF;    //Out of Range (common profile error)

//128-bit UUIDs, least significant byte first (the order NimBLE and the air use)
void bleServiceUuid(uint8_t out[16]);
void bleFieldUuid(BLE_FIELD field, uint8_t out[16]);

//what the descriptors of a characteristic say
const char* bleFieldName(BLE_FIELD field);              //User Description (0x2901)
void blePresentationFormat(BLE_FIELD field, uint8_t out[7]);   //Presentation Format (0x2904)
uint8_t bleFieldSize(BLE_FIELD field);

//current value of a setting, returns the number of bytes written to out
uint8_t bleEncode(BLE_FIELD field, const FuncGenState& state, uint8_t out[BLE_VALUE_MAX_LEN]);

//settings whose encoded value differs between two states (bit (uint8_t)BLE_FIELD)
uint8_t bleChangedFields(const FuncGenState& before, const FuncGenState& after);

enum class BLE_WRITE : uint8_t {
    OK,
    BAD_LENGTH,
    OUT_OF_RANGE
};

//ATT error for a rejected write, 0 for OK
uint8_t bleAttError(BLE_WRITE result);

//a decoded, range-checked write
struct BleCommand {
    BLE_FIELD field;
    WAVEFORM waveform;  //WAVEFORM
    float number;       //FREQUENCY / AMPLITUDE / PHASE / DC_OFFSET, in Hz, Vpp, degrees, V
    bool enabled;       //OUTPUT_ENABLE
};

//checks a write against the limits (amplitude against state's waveform).
//out is only filled when it returns OK
BLE_WRITE bleDecode(BLE_FIELD field, const uint8_t* data, size_t len, const FuncGenState& state,
                    const FuncGenLimits& limits, BleCommand& out);

//applies a decoded write through the controller, tagged CHANGE_SOURCE::BLE
void bleApply(FuncGenController& controller, const BleCommand& command);

#endif
