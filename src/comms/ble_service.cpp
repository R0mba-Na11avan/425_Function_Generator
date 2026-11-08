/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: NimBLE GATT service: one characteristic per setting, notifies on state change
**************************************************************************/

#include "ble_service.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>
#include "esp32-hal-bt-mem.h"   //keeps the core from freeing the Bluetooth controller's memory at boot
#include "esp_mac.h"
#include "esp_random.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "wifi_logic.h"

extern "C" void ble_store_config_init(void);   //NimBLE's NVS bond store, not declared in its header

static const uint16_t UUID_USER_DESCRIPTION = 0x2901;
static const uint16_t UUID_PRESENTATION_FORMAT = 0x2904;
static const uint32_t PASSKEY_RANGE = 1000000;  //6 digits

//every characteristic needs an encrypted, MITM-authenticated link
static const ble_gatt_chr_flags VALUE_FLAGS =
    BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_READ_AUTHEN |
    BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN |
    BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC | BLE_GATT_CHR_F_NOTIFY_INDICATE_AUTHEN;

//the GATT table. NimBLE keeps pointers into it, so it has to live forever
static ble_uuid128_t s_serviceUuid;
static ble_uuid128_t s_fieldUuid[BLE_FIELD_COUNT];
static ble_uuid16_t s_userDescriptionUuid;
static ble_uuid16_t s_presentationUuid;
static ble_gatt_dsc_def s_descriptors[BLE_FIELD_COUNT][3];  //name, format, terminator
static ble_gatt_chr_def s_characteristics[BLE_FIELD_COUNT + 1];
static ble_gatt_svc_def s_services[2];

static ble_npl_event s_notifyEvent;
static ble_npl_event s_cancelEvent;
static ble_npl_event s_forgetEvent;

BleService* BleService::_instance = nullptr;

static void hostTask(void*) {
    nimble_port_run();  //returns only when the stack is stopped
    nimble_port_freertos_deinit();
}

static uint16_t advInterval(uint32_t ms) {
    return (uint16_t)(ms * 1000 / 625);    //units of 0.625 ms
}

//arg of a characteristic or descriptor: the field
static void* fieldArg(uint8_t field) {
    return (void*)(uintptr_t)field;
}

static void buildTable(uint16_t* valueHandles, ble_gatt_access_fn* access) {
    s_userDescriptionUuid.u.type = BLE_UUID_TYPE_16;
    s_userDescriptionUuid.value = UUID_USER_DESCRIPTION;
    s_presentationUuid.u.type = BLE_UUID_TYPE_16;
    s_presentationUuid.value = UUID_PRESENTATION_FORMAT;
    s_serviceUuid.u.type = BLE_UUID_TYPE_128;
    bleServiceUuid(s_serviceUuid.value);

    memset(s_descriptors, 0, sizeof(s_descriptors));
    memset(s_characteristics, 0, sizeof(s_characteristics));
    for (uint8_t i = 0; i < BLE_FIELD_COUNT; i++) {
        s_fieldUuid[i].u.type = BLE_UUID_TYPE_128;
        bleFieldUuid((BLE_FIELD)i, s_fieldUuid[i].value);

        //names and units are not secret, readable before pairing
        s_descriptors[i][0].uuid = &s_userDescriptionUuid.u;
        s_descriptors[i][0].att_flags = BLE_ATT_F_READ;
        s_descriptors[i][0].access_cb = access;
        s_descriptors[i][0].arg = fieldArg(i);
        s_descriptors[i][1].uuid = &s_presentationUuid.u;
        s_descriptors[i][1].att_flags = BLE_ATT_F_READ;
        s_descriptors[i][1].access_cb = access;
        s_descriptors[i][1].arg = fieldArg(i);

        ble_gatt_chr_def& c = s_characteristics[i];
        c.uuid = &s_fieldUuid[i].u;
        c.access_cb = access;
        c.arg = fieldArg(i);
        c.descriptors = s_descriptors[i];
        c.flags = VALUE_FLAGS;
        c.val_handle = &valueHandles[i];
    }

    memset(s_services, 0, sizeof(s_services));
    s_services[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
    s_services[0].uuid = &s_serviceUuid.u;
    s_services[0].characteristics = s_characteristics;
}

BleService::BleService(FuncGenController& controller)
    : _controller(controller), _task(nullptr), _ownAddrType(BLE_OWN_ADDR_PUBLIC), _synced(false),
      _haveSent(false), _pairingConn(BLE_HS_CONN_HANDLE_NONE), _stateDirty(false), _cancelRequested(false),
      _forgetRequested(false), _statusMux(portMUX_INITIALIZER_UNLOCKED) {
    memset(_valueHandle, 0, sizeof(_valueHandle));
    for (uint16_t& c : _conns) c = BLE_HS_CONN_HANDLE_NONE;
    memset(&_lastSent, 0, sizeof(_lastSent));
    memset(&_status, 0, sizeof(_status));
    _status.state = BLE_STATE::OFF;
    _status.pairing = PAIRING::NONE;
}

bool BleService::begin() {
    if (_task) return true;
    if (_instance) return false;
    _instance = this;

    //same name as the setup AP, so both are easy to match to this board
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    apSsidFromMac(mac, _status.name, sizeof(_status.name));

    if (nimble_port_init() != ESP_OK) {
        log_e("nimble_port_init failed");
        return false;
    }

    ble_hs_cfg.sync_cb = onSync;
    ble_hs_cfg.reset_cb = onReset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;  //bond store full: drop the oldest phone
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    //ID keys too, phones use random resolvable addresses
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    buildTable(_valueHandle, onAccess);
    if (ble_gatts_count_cfg(s_services) != 0 || ble_gatts_add_svcs(s_services) != 0) {
        log_e("could not register the GATT service");
        return false;
    }
    ble_svc_gap_device_name_set(_status.name);
    ble_store_config_init();

    ble_npl_event_init(&s_notifyEvent, onNotifyEvent, nullptr);
    ble_npl_event_init(&s_cancelEvent, onCancelEvent, nullptr);
    ble_npl_event_init(&s_forgetEvent, onForgetEvent, nullptr);
    if (!_controller.addListener(onStateChanged, this)) return false;

    BaseType_t ok = xTaskCreatePinnedToCore(taskEntry, "ble", BLE_TASK_STACK, this, BLE_TASK_PRIO,
                                            &_task, BLE_TASK_CORE);
    if (ok != pdPASS) return false;
    nimble_port_freertos_init(hostTask);
    return true;
}

BleStatus BleService::status() const {
    portENTER_CRITICAL(&_statusMux);
    BleStatus s = _status;
    portEXIT_CRITICAL(&_statusMux);
    return s;
}

void BleService::cancelPairing() {
    _cancelRequested = true;
    if (_task) xTaskNotifyGive(_task);
}

void BleService::requestForgetPhones() {
    _forgetRequested = true;
    if (_task) xTaskNotifyGive(_task);
}

bool BleService::forgetPhones() {
    Preferences prefs;
    if (!prefs.begin(BLE_BOND_NVS_NAMESPACE, false)) return false;
    bool ok = prefs.clear();
    prefs.end();
    return ok;
}

//BLE task: hands work to the NimBLE task

void BleService::taskEntry(void* arg) {
    ((BleService*)arg)->run();
}

void BleService::run() {
    struct ble_npl_eventq* queue = nimble_port_get_dflt_eventq();
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        //an event that is still queued isn't queued twice, so bursts collapse
        if (_stateDirty.exchange(false)) ble_npl_eventq_put(queue, &s_notifyEvent);
        if (_cancelRequested.exchange(false)) ble_npl_eventq_put(queue, &s_cancelEvent);
        if (_forgetRequested.exchange(false)) ble_npl_eventq_put(queue, &s_forgetEvent);
    }
}

void BleService::onStateChanged(const FuncGenState&, CHANGE_SOURCE, void* ctx) {
    //any task: flag and wake only
    BleService* self = (BleService*)ctx;
    self->_stateDirty = true;
    if (self->_task) xTaskNotifyGive(self->_task);
}

//NimBLE task

void BleService::onSync() {
    BleService* self = _instance;
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &self->_ownAddrType);
    self->_lastSent = self->_controller.state();
    self->_haveSent = true;
    self->_synced = true;
    self->updateBondCount();
    self->setConnected(0);
    self->startAdvertising();
    Serial.printf("ble: advertising as %s\n", self->_status.name);
}

void BleService::onReset(int reason) {
    //every connection is gone (without disconnect events). onSync follows once the stack is back
    BleService* self = _instance;
    log_w("BLE host reset, reason %d", reason);
    self->_synced = false;
    self->_pairingConn = BLE_HS_CONN_HANDLE_NONE;
    for (uint16_t& c : self->_conns) c = BLE_HS_CONN_HANDLE_NONE;
    portENTER_CRITICAL(&self->_statusMux);
    self->_status.state = BLE_STATE::OFF;
    self->_status.connected = 0;
    if (self->_status.pairing == PAIRING::SHOW_PASSKEY) self->_status.pairing = PAIRING::FAILED;
    portEXIT_CRITICAL(&self->_statusMux);
}

void BleService::onNotifyEvent(struct ble_npl_event*) {
    _instance->sendChanges();
}

void BleService::onCancelEvent(struct ble_npl_event*) {
    BleService* self = _instance;
    //the disconnect event then reports the pairing as failed
    if (self->_pairingConn != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(self->_pairingConn, BLE_ERR_REM_USER_CONN_TERM);
}

void BleService::onForgetEvent(struct ble_npl_event*) {
    BleService* self = _instance;
    //bonds first, so a phone can't get back in on its old keys while it is being dropped
    int rc = ble_store_clear();
    for (uint16_t c : self->_conns) {
        if (c != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(c, BLE_ERR_REM_USER_CONN_TERM);
    }
    self->updateBondCount();
    Serial.println(rc == 0 ? "ble: phones forgotten" : "ble: could not forget the phones");
}

void BleService::sendChanges() {
    if (!_synced) return;
    FuncGenState s = _controller.state();
    uint8_t changed = _haveSent ? bleChangedFields(_lastSent, s) : 0xFF;
    _lastSent = s;
    _haveSent = true;
    //NimBLE reads the new value through onAccess and notifies every subscribed phone
    for (uint8_t i = 0; i < BLE_FIELD_COUNT; i++) {
        if (changed & (1u << i)) ble_gatts_chr_updated(_valueHandle[i]);
    }
}

void BleService::startAdvertising() {
    if (!_synced || ble_gap_adv_active()) return;
    if (status().connected >= CONFIG_BT_NIMBLE_MAX_CONNECTIONS) return;

    //flags + service UUID in the advertisement (21 of 31 bytes), name in the scan response
    struct ble_hs_adv_fields adv;
    memset(&adv, 0, sizeof(adv));
    adv.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    adv.uuids128 = &s_serviceUuid;
    adv.num_uuids128 = 1;
    adv.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&adv);

    struct ble_hs_adv_fields rsp;
    memset(&rsp, 0, sizeof(rsp));
    rsp.name = (const uint8_t*)_status.name;   //only written in begin()
    rsp.name_len = (uint8_t)strlen(_status.name);
    rsp.name_is_complete = 1;
    if (rc == 0) rc = ble_gap_adv_rsp_set_fields(&rsp);

    struct ble_gap_adv_params params;
    memset(&params, 0, sizeof(params));
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min = advInterval(BLE_ADV_INTERVAL_MS);
    params.itvl_max = advInterval(BLE_ADV_INTERVAL_MS + BLE_ADV_INTERVAL_MS / 2);
    if (rc == 0) rc = ble_gap_adv_start(_ownAddrType, nullptr, BLE_HS_FOREVER, &params, onGapEvent, nullptr);
    if (rc != 0) log_e("could not start advertising (%d)", rc);
}

int BleService::onGapEvent(struct ble_gap_event* event, void*) {
    BleService* self = _instance;
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                self->setConnected(1);
                self->trackConnection(event->connect.conn_handle, true);
                //bonded phones just re-encrypt, new ones get the passkey prompt
                ble_gap_security_initiate(event->connect.conn_handle);
            }
            self->startAdvertising();
            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            self->setConnected(-1);
            self->trackConnection(event->disconnect.conn.conn_handle, false);
            if (event->disconnect.conn.conn_handle == self->_pairingConn) {
                self->pairingDone(self->_pairingConn, false);
            }
            self->startAdvertising();
            return 0;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            self->startAdvertising();
            return 0;

        case BLE_GAP_EVENT_PASSKEY_ACTION: {
            if (event->passkey.params.action != BLE_SM_IOACT_DISP) return 0;
            struct ble_sm_io io;
            memset(&io, 0, sizeof(io));
            io.action = BLE_SM_IOACT_DISP;
            io.passkey = esp_random() % PASSKEY_RANGE;
            ble_sm_inject_io(event->passkey.conn_handle, &io);

            self->_pairingConn = event->passkey.conn_handle;
            portENTER_CRITICAL(&self->_statusMux);
            self->_status.pairing = PAIRING::SHOW_PASSKEY;
            self->_status.passkey = io.passkey;
            self->_status.pairingId++;
            portEXIT_CRITICAL(&self->_statusMux);
            return 0;
        }

        case BLE_GAP_EVENT_ENC_CHANGE:
            if (event->enc_change.conn_handle == self->_pairingConn) {
                self->pairingDone(self->_pairingConn, event->enc_change.status == 0);
            }
            return 0;

        case BLE_GAP_EVENT_REPEAT_PAIRING: {
            //the phone forgot us but we still have its old bond: drop it and pair again
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
                ble_store_util_delete_peer(&desc.peer_id_addr);
            }
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }

        default:
            return 0;
    }
}

int BleService::onAccess(uint16_t, uint16_t, struct ble_gatt_access_ctxt* ctxt, void* arg) {
    BleService* self = _instance;
    BLE_FIELD field = (BLE_FIELD)(uintptr_t)arg;

    switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_READ_CHR: {
            uint8_t value[BLE_VALUE_MAX_LEN];
            uint8_t len = bleEncode(field, self->_controller.state(), value);
            return os_mbuf_append(ctxt->om, value, len) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }

        case BLE_GATT_ACCESS_OP_WRITE_CHR: {
            //one byte bigger than any value, so a too-long write is still seen as too long
            uint8_t data[BLE_VALUE_MAX_LEN + 1];
            uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
            if (len > sizeof(data)) return bleAttError(BLE_WRITE::BAD_LENGTH);
            if (ble_hs_mbuf_to_flat(ctxt->om, data, sizeof(data), &len) != 0) return BLE_ATT_ERR_UNLIKELY;

            BleCommand command;
            BLE_WRITE result = bleDecode(field, data, len, self->_controller.state(), self->_controller.limits(), command);
            if (result != BLE_WRITE::OK) return bleAttError(result);
            bleApply(self->_controller, command);
            return 0;
        }

        case BLE_GATT_ACCESS_OP_READ_DSC: {
            if (ble_uuid_u16(ctxt->dsc->uuid) == UUID_PRESENTATION_FORMAT) {
                uint8_t format[7];
                blePresentationFormat(field, format);
                return os_mbuf_append(ctxt->om, format, sizeof(format)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            const char* name = bleFieldName(field);
            return os_mbuf_append(ctxt->om, name, strlen(name)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }

        default:
            return BLE_ATT_ERR_UNLIKELY;
    }
}

void BleService::pairingDone(uint16_t, bool ok) {
    _pairingConn = BLE_HS_CONN_HANDLE_NONE;
    portENTER_CRITICAL(&_statusMux);
    _status.pairing = ok ? PAIRING::PAIRED : PAIRING::FAILED;
    portEXIT_CRITICAL(&_statusMux);
    updateBondCount();
    Serial.println(ok ? "ble: phone paired" : "ble: pairing failed");
}

void BleService::updateBondCount() {
    int count = 0;
    if (ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &count) != 0) count = 0;
    portENTER_CRITICAL(&_statusMux);
    _status.bonded = (uint8_t)count;
    portEXIT_CRITICAL(&_statusMux);
}

void BleService::trackConnection(uint16_t connHandle, bool connected) {
    for (uint16_t& c : _conns) {
        if (connected ? c == BLE_HS_CONN_HANDLE_NONE : c == connHandle) {
            c = connected ? connHandle : BLE_HS_CONN_HANDLE_NONE;
            return;
        }
    }
}

void BleService::setConnected(int8_t delta) {
    portENTER_CRITICAL(&_statusMux);
    int16_t n = (int16_t)_status.connected + delta;
    _status.connected = (uint8_t)(n < 0 ? 0 : n);
    _status.state = _status.connected ? BLE_STATE::CONNECTED : BLE_STATE::ADVERTISING;
    portEXIT_CRITICAL(&_statusMux);
}
