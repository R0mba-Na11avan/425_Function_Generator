/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: BLE control of the function generator: one GATT characteristic
             per setting, passkey pairing shown on the LCD, notifications
             when anything changes the state
**************************************************************************/

/*
    Uses the NimBLE stack that is built into the ESP32 core (the S3 is BLE
    only), through its own C API rather than the Arduino BLE wrapper: the
    wrapper always answers a write with success, so it can't reject an
    out-of-range value the way the MCP tools do.

    GATT layout and value encoding: see ble_logic.h. Test with nRF Connect.

    Security
        - every characteristic needs an encrypted, authenticated (MITM) link:
          the first access from a new phone starts pairing. we also ask for
          security as soon as a phone connects
        - IO capability DISPLAY_ONLY: the board shows a random 6-digit passkey
          (BleStatus::pairing == SHOW_PASSKEY, drawn as a popup by DisplayUI)
          and the phone's user types it in. Just Works can't produce an
          authenticated link, so a phone can't pair without seeing the screen
        - bonds are kept in NVS (NimBLE's own "nimble_bond" namespace), up to
          CONFIG_BT_NIMBLE_MAX_BONDS (3). pairing a phone when full drops the
          oldest. forgetPhones() clears them (boot hold)
        - the popup can cancel a pairing (cancelPairing): the phone is
          disconnected

    Threading
        - NimBLE runs its host task on core 0. GATT reads/writes and GAP
          events are handled there. a write calls the controller setter
          directly (like the MCP handler on the HTTP task)
        - the controller listener only wakes the BLE task. that task posts an
          event to the NimBLE queue, and the NimBLE task compares the state
          with what was last sent and marks changed characteristics, so all
          stack calls happen on the NimBLE task
        - status() can be read from any task (spinlock)

    Wi-Fi + BLE share the radio. ESP-IDF requires Wi-Fi modem sleep while
    both are on: never call WiFi.setSleep(false), it aborts.
*/

#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "../config.h"
#include "../core/funcgen_controller.h"
#include "../core/ble_status.h"
#include "ble_logic.h"

//NimBLE types, only used through pointers here
struct ble_gap_event;
struct ble_gatt_access_ctxt;
struct ble_npl_event;

class BleService {
    public:
        BleService(FuncGenController& controller);

        //starts NimBLE, registers the service and starts advertising. only one
        //BleService can exist (NimBLE's callbacks have no context pointer)
        bool begin();

        BleStatus status() const;

        //ends the pairing that is showing a passkey (disconnects that phone). any task
        void cancelPairing();

        //forgets every paired phone. call before begin() (boot hold)
        static bool forgetPhones();

    private:
        static void taskEntry(void* arg);
        void run();

        //NimBLE callbacks (NimBLE task)
        static void onSync();
        static void onReset(int reason);
        static int onGapEvent(struct ble_gap_event* event, void* arg);
        static int onAccess(uint16_t connHandle, uint16_t attrHandle, struct ble_gatt_access_ctxt* ctxt, void* arg);
        static void onStateChanged(const FuncGenState& state, CHANGE_SOURCE source, void* ctx);
        static void onNotifyEvent(struct ble_npl_event* ev);
        static void onCancelEvent(struct ble_npl_event* ev);

        //NimBLE task only
        void startAdvertising();
        void sendChanges();
        void pairingDone(uint16_t connHandle, bool ok);
        void updateBondCount();
        void setConnected(int8_t delta);

        static BleService* _instance;

        FuncGenController& _controller;
        TaskHandle_t _task;
        uint8_t _ownAddrType;
        bool _synced;

        uint16_t _valueHandle[BLE_FIELD_COUNT];
        FuncGenState _lastSent;     //what notifications last reported
        bool _haveSent;

        uint16_t _pairingConn;      //connection showing a passkey, BLE_HS_CONN_HANDLE_NONE if none

        //set from any task, picked up by the BLE task
        std::atomic<bool> _stateDirty;
        std::atomic<bool> _cancelRequested;

        mutable portMUX_TYPE _statusMux;
        BleStatus _status;
};

#endif
