/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: snapshot of the Bluetooth state. BleService (comms) produces it
             and DisplayUI (ui) shows it (status icon, info panel, pairing
             popup). it lives in core for the same reason as NetStatus
**************************************************************************/

#ifndef BLE_STATUS_H
#define BLE_STATUS_H

#include <stdint.h>

enum class BLE_STATE : uint8_t {
    OFF,            //not started (or failed to start)
    ADVERTISING,    //no phone connected
    CONNECTED       //at least one phone connected
};

//the latest pairing attempt
enum class PAIRING : uint8_t {
    NONE,           //none since boot
    SHOW_PASSKEY,   //a phone is pairing: show passkey until it finishes
    PAIRED,
    FAILED          //wrong code, cancelled, timed out or the phone went away
};

struct BleStatus {
    BLE_STATE state;
    char name[33];          //advertised name
    uint8_t connected;      //phones connected now
    uint8_t bonded;         //phones remembered
    PAIRING pairing;
    uint32_t passkey;       //6 digits, valid while SHOW_PASSKEY
    uint16_t pairingId;     //+1 for every attempt, so the UI can tell attempts apart
};

#endif
