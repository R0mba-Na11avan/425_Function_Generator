/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: STA/AP state machine, credential checks, scan list (no hardware)
**************************************************************************/

#include "wifi_logic.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "../libs/ArduinoJson/ArduinoJson.h"

//WifiSupervisor

WIFI_ACTION WifiSupervisor::begin(bool hasCredentials, uint32_t nowMs) {
    _hasCredentials = hasCredentials;
    if (!hasCredentials) return enterAp(nowMs, WIFI_ACTION::AP_START);
    _phase = PHASE::JOINING;
    startJoining(nowMs, STA_CONNECT_TIMEOUT_MS);
    return WIFI_ACTION::STA_BEGIN;
}

WIFI_ACTION WifiSupervisor::update(const WifiInputs& in, uint32_t nowMs) {
    switch (_phase) {
        case PHASE::ONLINE:
            if (!in.connected) {
                //link lost. we reconnect ourselves (the core's auto-reconnect is off)
                _phase = PHASE::JOINING;
                startJoining(nowMs, STA_LOST_TIMEOUT_MS);
                scheduleRetry(nowMs);
            }
            return WIFI_ACTION::NONE;

        case PHASE::JOINING:
        case PHASE::AP_JOINING:
            if (in.connected) {
                _phase = PHASE::ONLINE;
                return WIFI_ACTION::STA_ONLINE;
            }
            //someone joined the AP, probably to change the settings. joining
            //moves the radio to the router's channel and would drop them
            if (_phase == PHASE::AP_JOINING && in.apClients > 0) return enterAp(nowMs, WIFI_ACTION::STA_ABORT);
            if (nowMs - _since >= _timeout) {
                return enterAp(nowMs, (_phase == PHASE::JOINING) ? WIFI_ACTION::AP_START : WIFI_ACTION::STA_ABORT);
            }
            if (in.attemptFailed && !_retryPending) scheduleRetry(nowMs);
            if (_retryPending && (int32_t)(nowMs - _retryAt) >= 0) {
                _retryPending = false;
                return WIFI_ACTION::STA_BEGIN;
            }
            return WIFI_ACTION::NONE;

        case PHASE::AP:
            if (_hasCredentials && AP_STA_RETRY_MS > 0 && in.apClients == 0 && nowMs - _since >= (uint32_t)AP_STA_RETRY_MS) {
                _phase = PHASE::AP_JOINING;
                startJoining(nowMs, STA_CONNECT_TIMEOUT_MS);
                return WIFI_ACTION::STA_BEGIN;
            }
            return WIFI_ACTION::NONE;

        case PHASE::IDLE:
        default:
            return WIFI_ACTION::NONE;
    }
}

WIFI_ACTION WifiSupervisor::openSetup(uint32_t nowMs) {
    switch (_phase) {
        case PHASE::AP:
            return WIFI_ACTION::NONE;
        case PHASE::AP_JOINING:
            return enterAp(nowMs, WIFI_ACTION::STA_ABORT);  //the AP is already up
        case PHASE::IDLE:
        case PHASE::JOINING:
        case PHASE::ONLINE:
        default:
            return enterAp(nowMs, WIFI_ACTION::AP_START);
    }
}

WIFI_ACTION WifiSupervisor::leaveSetup(uint32_t nowMs) {
    //only from AP mode proper: AP_JOINING is already on its way
    if (_phase != PHASE::AP || !_hasCredentials) return WIFI_ACTION::NONE;
    _phase = PHASE::JOINING;
    startJoining(nowMs, STA_CONNECT_TIMEOUT_MS);
    return WIFI_ACTION::LEAVE_AP;
}

WIFI_STATE WifiSupervisor::state() const {
    switch (_phase) {
        case PHASE::JOINING: return WIFI_STATE::CONNECTING;
        case PHASE::ONLINE: return WIFI_STATE::CONNECTED;
        case PHASE::AP:
        case PHASE::AP_JOINING: return WIFI_STATE::AP_MODE;
        case PHASE::IDLE:
        default: return WIFI_STATE::IDLE;
    }
}

void WifiSupervisor::startJoining(uint32_t nowMs, uint32_t timeoutMs) {
    _since = nowMs;
    _timeout = timeoutMs;
    _retryPending = false;
    _backoff = STA_RETRY_MIN_MS;
}

void WifiSupervisor::scheduleRetry(uint32_t nowMs) {
    _retryPending = true;
    _retryAt = nowMs + _backoff;
    _backoff = (_backoff * 2 > STA_RETRY_MAX_MS) ? STA_RETRY_MAX_MS : _backoff * 2;
}

WIFI_ACTION WifiSupervisor::enterAp(uint32_t nowMs, WIFI_ACTION action) {
    _phase = PHASE::AP;
    _since = nowMs;
    _retryPending = false;
    return action;
}

//credentials

const char* credsErrorMessage(CREDS_ERROR error) {
    switch (error) {
        case CREDS_ERROR::OK: return "ok";
        case CREDS_ERROR::BAD_JSON: return "expected a JSON object with ssid and password";
        case CREDS_ERROR::SSID_MISSING: return "ssid is missing";
        case CREDS_ERROR::SSID_TOO_LONG: return "ssid is longer than 32 bytes";
        case CREDS_ERROR::PASSWORD_INVALID: return "password must be empty (open network), 8-63 characters or 64 hex digits";
        default: return "invalid credentials";
    }
}

static bool allHex(const char* s) {
    for (; *s; s++) {
        if (!isxdigit((unsigned char)*s)) return false;
    }
    return true;
}

CREDS_ERROR validateCredentials(const char* ssid, const char* password) {
    if (!ssid || !ssid[0]) return CREDS_ERROR::SSID_MISSING;
    if (strlen(ssid) > 32) return CREDS_ERROR::SSID_TOO_LONG;

    size_t n = password ? strlen(password) : 0;
    if (n == 0 || (n >= 8 && n <= 63)) return CREDS_ERROR::OK;
    if (n == 64 && allHex(password)) return CREDS_ERROR::OK; //raw PSK
    return CREDS_ERROR::PASSWORD_INVALID;
}

CREDS_ERROR parseCredentials(const char* json, size_t len, WifiCredentials& out) {
    JsonDocument doc;
    if (deserializeJson(doc, json, len) || !doc.is<JsonObject>()) return CREDS_ERROR::BAD_JSON;

    JsonVariant ssid = doc["ssid"];
    JsonVariant password = doc["password"];
    if (!ssid.is<const char*>()) return CREDS_ERROR::SSID_MISSING;
    if (!password.isNull() && !password.is<const char*>()) return CREDS_ERROR::PASSWORD_INVALID;

    const char* s = ssid.as<const char*>();
    const char* p = password.isNull() ? "" : password.as<const char*>();
    CREDS_ERROR error = validateCredentials(s, p);
    if (error != CREDS_ERROR::OK) return error;

    snprintf(out.ssid, sizeof(out.ssid), "%s", s);
    snprintf(out.password, sizeof(out.password), "%s", p);
    return CREDS_ERROR::OK;
}

//scan list

void clearScanList(ScanList& list) {
    list.count = 0;
}

void addScanResult(ScanList& list, const char* ssid, int32_t rssi, bool secured) {
    if (!ssid || !ssid[0]) return;
    if (rssi > 0) rssi = 0;
    if (rssi < -127) rssi = -127;

    char name[sizeof(ScanEntry::ssid)];
    snprintf(name, sizeof(name), "%s", ssid);

    //same SSID already listed: keep whichever is stronger
    for (uint8_t i = 0; i < list.count; i++) {
        if (strcmp(list.entries[i].ssid, name) != 0) continue;
        if (list.entries[i].rssi >= rssi) return;
        memmove(&list.entries[i], &list.entries[i + 1], (list.count - i - 1) * sizeof(ScanEntry));
        list.count--;
        break;
    }

    uint8_t pos = 0;
    while (pos < list.count && list.entries[pos].rssi >= rssi) pos++;
    if (pos >= WIFI_SCAN_MAX) return; //full and weaker than everything listed

    uint8_t keep = (list.count < WIFI_SCAN_MAX) ? list.count : WIFI_SCAN_MAX - 1;
    memmove(&list.entries[pos + 1], &list.entries[pos], (keep - pos) * sizeof(ScanEntry));
    ScanEntry& e = list.entries[pos];
    memcpy(e.ssid, name, sizeof(e.ssid));
    e.rssi = (int8_t)rssi;
    e.secured = secured;
    list.count = keep + 1;
}

static void buildScanDoc(JsonDocument& doc, const ScanList& list, bool scanning) {
    doc["scanning"] = scanning;
    JsonArray networks = doc["networks"].to<JsonArray>();
    for (uint8_t i = 0; i < list.count; i++) {
        JsonObject n = networks.add<JsonObject>();
        n["ssid"] = list.entries[i].ssid;
        n["rssi"] = list.entries[i].rssi;
        n["secured"] = list.entries[i].secured;
    }
}

size_t scanListJsonSize(const ScanList& list, bool scanning) {
    JsonDocument doc;
    buildScanDoc(doc, list, scanning);
    return measureJson(doc) + 1;
}

size_t scanListJson(const ScanList& list, bool scanning, char* out, size_t len) {
    JsonDocument doc;
    buildScanDoc(doc, list, scanning);
    if (measureJson(doc) + 1 > len) return 0;
    return serializeJson(doc, out, len);
}

void apSsidFromMac(const uint8_t mac[6], char* out, size_t len) {
    snprintf(out, len, "%s%02X%02X", AP_SSID_PREFIX, mac[4], mac[5]);
}
