/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: /api/... JSON endpoints that call FuncGenController (backend for the web control page)
**************************************************************************/

#include "rest_api.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "api_json.h"
#include "../web/index_html.h"

static const size_t UPDATE_BODY_MAX = 256; //one setting, e.g. {"frequency_hz": 12345.6}

enum class SETTING : uint8_t {
    WAVEFORM,
    FREQUENCY,
    AMPLITUDE,
    PHASE,
    DC_OFFSET,
    OUTPUT_ENABLE
};

struct SettingKey {
    const char* key;    //same names as the state JSON
    SETTING setting;
    const char* unit;   //numbers only
};

static const SettingKey SETTINGS[] = {
    {"waveform", SETTING::WAVEFORM, nullptr},
    {"frequency_hz", SETTING::FREQUENCY, "Hz"},
    {"amplitude_vpp", SETTING::AMPLITUDE, "Vpp"},
    {"phase_deg", SETTING::PHASE, "degrees"},
    {"dc_offset_v", SETTING::DC_OFFSET, "V"},
    {"output_enabled", SETTING::OUTPUT_ENABLE, nullptr},
};

static const SettingKey* findSetting(const char* key) {
    for (const SettingKey& s : SETTINGS) {
        if (strcmp(s.key, key) == 0) return &s;
    }
    return nullptr;
}

//JSON helpers

static int finish(JsonDocument& doc, int status, char** out) {
    size_t size = measureJson(doc) + 1;
    *out = (char*)malloc(size);
    if (!*out) return 503;
    serializeJson(doc, *out, size);
    return status;
}

static int error(int status, const char* message, char** out) {
    JsonDocument doc;
    doc["error"] = message;
    return finish(doc, status, out);
}

//state + the ranges allowed with that state
static void writeStateBody(const FuncGenState& s, const FuncGenLimits& l, JsonObject out) {
    writeStateJson(s, out["state"].to<JsonObject>());
    JsonObject ranges = out["ranges"].to<JsonObject>();
    writeRangeJson(l.frequency, ranges["frequency_hz"].to<JsonObject>());
    writeRangeJson(l.amplitudeFor(s), ranges["amplitude_vpp"].to<JsonObject>());
    JsonObject phase = ranges["phase_deg"].to<JsonObject>();
    writeRangeJson(l.phase, phase);
    phase["max_exclusive"] = true;
    writeRangeJson(l.dcOffsetFor(s), ranges["dc_offset_v"].to<JsonObject>());
    out["output_peak_v"] = l.outputPeak;
}

static const char* wifiStateName(WIFI_STATE s) {
    switch (s) {
        case WIFI_STATE::CONNECTING: return "connecting";
        case WIFI_STATE::CONNECTED: return "connected";
        case WIFI_STATE::AP_MODE: return "setup";
        case WIFI_STATE::IDLE:
        default: return "idle";
    }
}

static const char* bleStateName(BLE_STATE s) {
    switch (s) {
        case BLE_STATE::ADVERTISING: return "advertising";
        case BLE_STATE::CONNECTED: return "connected";
        case BLE_STATE::OFF:
        default: return "off";
    }
}

//RestApi

RestApi::RestApi(FuncGenController& controller, HttpServer& http)
    : _controller(controller), _http(http), _net(nullptr), _netCtx(nullptr), _ble(nullptr), _bleCtx(nullptr) {}

void RestApi::setInfoSources(NetSource net, void* netCtx, BleSource ble, void* bleCtx) {
    _net = net;
    _netCtx = netCtx;
    _ble = ble;
    _bleCtx = bleCtx;
}

bool RestApi::begin() {
    return _http.registerRoute(WEB_CONTROL_PATH, HTTP_METHOD::GET, handlePage, this)
        && _http.registerRoute(API_STATE_PATH, HTTP_METHOD::GET, handleStateGet, this)
        && _http.registerRoute(API_STATE_PATH, HTTP_METHOD::POST, handleStatePost, this)
        && _http.registerRoute(API_INFO_PATH, HTTP_METHOD::GET, handleInfo, this);
}

int RestApi::stateJson(char** out) {
    JsonDocument doc;
    writeStateBody(_controller.state(), _controller.limits(), doc.to<JsonObject>());
    return finish(doc, 200, out);
}

int RestApi::updateJson(const char* body, size_t len, char** out) {
    *out = nullptr;
    JsonDocument req;
    if (deserializeJson(req, body, len)) return error(400, "body is not valid JSON", out);
    if (!req.is<JsonObject>()) return error(400, "body must be a JSON object with one setting", out);
    JsonObjectConst obj = req.as<JsonObjectConst>();
    if (obj.size() != 1) return error(400, "send exactly one setting per request, e.g. {\"amplitude_vpp\": 2.5}", out);

    JsonPairConst kv = *obj.begin();
    const SettingKey* key = findSetting(kv.key().c_str());
    char msg[160];
    if (!key) {
        snprintf(msg, sizeof(msg), "unknown setting '%.32s', use one of waveform, frequency_hz, "
                 "amplitude_vpp, phase_deg, dc_offset_v, output_enabled", kv.key().c_str());
        return error(400, msg, out);
    }
    JsonVariantConst v = kv.value();

    FuncGenState before = _controller.state();
    FuncGenLimits limits = _controller.limits();
    bool clamped = false;
    char note[128] = "";
    char num[24], lo[24], hi[24];

    switch (key->setting) {
        case SETTING::WAVEFORM: {
            WAVEFORM w;
            if (!parseWaveform(v.is<const char*>() ? v.as<const char*>() : nullptr, w)) {
                return error(400, "'waveform' must be one of \"sine\", \"square\", \"triangle\"", out);
            }
            _controller.setWaveform(w, CHANGE_SOURCE::WEB);
            //a waveform with a smaller amplitude range pulls the amplitude in
            float amp = _controller.state().amplitude;
            if (amp != before.amplitude) {
                clamped = true;
                formatNumber(amp, num, sizeof(num));
                snprintf(note, sizeof(note), "amplitude_vpp clamped to %s Vpp for the %s waveform", num, waveformJsonName(w));
            }
            break;
        }
        case SETTING::OUTPUT_ENABLE:
            if (!v.is<bool>()) return error(400, "'output_enabled' must be true or false", out);
            _controller.setOutputEnabled(v.as<bool>(), CHANGE_SOURCE::WEB);
            break;
        default: {
            //bool and string are not numbers for ArduinoJson, so they end up here
            float f = v.is<double>() ? (float)v.as<double>() : NAN;
            if (!isfinite(f)) {
                snprintf(msg, sizeof(msg), "'%s' must be a finite number in %s", key->key, key->unit);
                return error(400, msg, out);
            }

            if (key->setting == SETTING::PHASE) {
                //wrapped here: the AD9833 driver wraps with a loop, which a huge value would never finish
                float wrapped = fmodf(f, 360.0f);
                if (wrapped < 0.0f) wrapped += 360.0f;
                if (wrapped >= 360.0f) wrapped = 0.0f; //-1e-6 + 360 rounds to 360
                if (wrapped != f) {
                    clamped = true;
                    formatNumber(wrapped, num, sizeof(num));
                    snprintf(note, sizeof(note), "phase_deg wrapped to %s degrees", num);
                }
                _controller.setPhase(wrapped, CHANGE_SOURCE::WEB);
                break;
            }

            ValueRange r = limits.frequency;
            if (key->setting == SETTING::AMPLITUDE) r = limits.amplitudeFor(before);
            else if (key->setting == SETTING::DC_OFFSET) r = limits.dcOffsetFor(before);

            //the setter clamps on its own, this only builds the note
            if (f < r.min || f > r.max) {
                clamped = true;
                formatNumber(f < r.min ? r.min : r.max, num, sizeof(num));
                formatNumber(r.min, lo, sizeof(lo));
                formatNumber(r.max, hi, sizeof(hi));
                const char* why = "";
                if (key->setting == SETTING::AMPLITUDE && before.dcOffset != 0.0f) why = ", less with this offset";
                else if (key->setting == SETTING::DC_OFFSET && before.amplitude != 0.0f) why = ", less with this amplitude";
                snprintf(note, sizeof(note), "%s clamped to %s %s (allowed %s to %s %s%s)",
                         key->key, num, key->unit, lo, hi, key->unit, why);
            }

            if (key->setting == SETTING::FREQUENCY) _controller.setFrequency(f, CHANGE_SOURCE::WEB);
            else if (key->setting == SETTING::AMPLITUDE) _controller.setAmplitude(f, CHANGE_SOURCE::WEB);
            else _controller.setDCOffset(f, CHANGE_SOURCE::WEB);
            break;
        }
    }

    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    writeStateBody(_controller.state(), limits, root);
    root["clamped"] = clamped;
    if (clamped) root["note"] = (const char*)note;
    return finish(doc, 200, out);
}

int RestApi::infoJson(char** out) {
    JsonDocument doc;
    doc["firmware"] = FIRMWARE_VERSION;
    doc["poll_ms"] = WEB_POLL_MS;

    if (_net) {
        NetStatus n = _net(_netCtx);
        JsonObject w = doc["wifi"].to<JsonObject>();
        w["state"] = wifiStateName(n.state);
        w["ssid"] = (const char*)n.ssid;
        w["ip"] = (const char*)n.ip;
        w["hostname"] = (const char*)n.hostname;
        w["saved_ssid"] = (const char*)n.savedSsid;
        char url[64] = "";
        if (n.state == WIFI_STATE::CONNECTED && n.hostname[0]) snprintf(url, sizeof(url), "http://%s.local%s", n.hostname, MCP_PATH);
        else if (n.ip[0]) snprintf(url, sizeof(url), "http://%s%s", n.ip, MCP_PATH);
        w["mcp_url"] = (const char*)url;
    }

    if (_ble) {
        BleStatus b = _ble(_bleCtx);
        JsonObject o = doc["ble"].to<JsonObject>();
        o["state"] = bleStateName(b.state);
        o["name"] = (const char*)b.name;
        o["connected"] = b.connected;
        o["paired"] = b.bonded;
    }
    return finish(doc, 200, out);
}

//HTTP

static void sendBody(HttpRequest& req, int status, char* out) {
    req.setHeader("Cache-Control", "no-store");
    if (out) req.sendJson(status, out);
    else req.sendError(503, "out of memory");
    free(out);
}

static void sendJsonError(HttpRequest& req, int status, const char* message) {
    char* out;
    status = error(status, message, &out);
    sendBody(req, status, out);
}

void RestApi::handlePage(HttpRequest& req, void*) {
    req.setHeader("Cache-Control", "no-store");
    req.send(200, "text/html; charset=utf-8", INDEX_HTML, strlen(INDEX_HTML));
}

void RestApi::handleStateGet(HttpRequest& req, void* ctx) {
    char* out;
    int status = ((RestApi*)ctx)->stateJson(&out);
    sendBody(req, status, out);
}

void RestApi::handleStatePost(HttpRequest& req, void* ctx) {
    //checked before the body is read, see the header and mcp_server.h for why
    char value[96];
    if (req.hasHeader("Origin") && (!req.header("Origin", value, sizeof(value)) || !originAllowed(value))) {
        sendJsonError(req, 403, "origin not allowed");
        return;
    }
    if (!req.header("Content-Type", value, sizeof(value)) || strncasecmp(value, "application/json", 16) != 0) {
        sendJsonError(req, 415, "Content-Type must be application/json");
        return;
    }

    char body[UPDATE_BODY_MAX + 1];
    size_t len;
    if (!req.readBody(body, sizeof(body), len)) return;

    char* out;
    int status = ((RestApi*)ctx)->updateJson(body, len, &out);
    sendBody(req, status, out);
}

void RestApi::handleInfo(HttpRequest& req, void* ctx) {
    char* out;
    int status = ((RestApi*)ctx)->infoJson(&out);
    sendBody(req, status, out);
}
