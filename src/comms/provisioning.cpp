/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: Wi-Fi setup routes (AP mode) and the hand-off to WifiManager
**************************************************************************/

#include "provisioning.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "../libs/ArduinoJson/ArduinoJson.h"
#include "../web/provisioning_html.h"

static const size_t SAVE_BODY_MAX = 1024; //an escaped 32 byte SSID + 64 byte password fits easily

static void sendJsonError(HttpRequest& req, int status, const char* message) {
    JsonDocument doc;
    doc["error"] = message;
    char out[160];
    serializeJson(doc, out, sizeof(out));
    req.sendJson(status, out);
}

Provisioning::Provisioning(HttpServer& http)
    : _http(http), _lock(xSemaphoreCreateMutex()), _enabled(false),
      _scanRequested(false), _scanning(false), _scannedOnce(false), _hasPending(false) {
    clearScanList(_scan);
    memset(&_pending, 0, sizeof(_pending));
}

bool Provisioning::enable() {
    if (_enabled) return true;
    bool ok = _http.registerRoute("/", HTTP_METHOD::GET, handlePage, this)
           && _http.registerRoute("/scan", HTTP_METHOD::GET, handleScanGet, this)
           && _http.registerRoute("/scan", HTTP_METHOD::POST, handleScanPost, this)
           && _http.registerRoute("/save", HTTP_METHOD::POST, handleSave, this);
    _enabled = true;
    if (!ok) disable(); //all or nothing
    return ok;
}

void Provisioning::disable() {
    _http.unregisterRoute("/", HTTP_METHOD::GET);
    _http.unregisterRoute("/scan", HTTP_METHOD::GET);
    _http.unregisterRoute("/scan", HTTP_METHOD::POST);
    _http.unregisterRoute("/save", HTTP_METHOD::POST);
    _enabled = false;
}

//Wi-Fi task side

void Provisioning::requestScan() {
    xSemaphoreTake(_lock, portMAX_DELAY);
    if (!_scanning) _scanRequested = true;
    xSemaphoreGive(_lock);
}

bool Provisioning::takeScanRequest() {
    xSemaphoreTake(_lock, portMAX_DELAY);
    bool take = _scanRequested;
    if (take) {
        _scanRequested = false;
        _scanning = true;
    }
    xSemaphoreGive(_lock);
    return take;
}

void Provisioning::scanFinished(const ScanList& list) {
    xSemaphoreTake(_lock, portMAX_DELAY);
    _scan = list;
    _scanning = false;
    _scannedOnce = true;
    xSemaphoreGive(_lock);
}

void Provisioning::scanFailed() {
    xSemaphoreTake(_lock, portMAX_DELAY);
    _scanning = false;
    _scannedOnce = true; //the page shows the old list and can ask again
    xSemaphoreGive(_lock);
}

bool Provisioning::takeCredentials(WifiCredentials& out) {
    xSemaphoreTake(_lock, portMAX_DELAY);
    bool take = _hasPending;
    if (take) {
        out = _pending;
        memset(&_pending, 0, sizeof(_pending));
        _hasPending = false;
    }
    xSemaphoreGive(_lock);
    return take;
}

//HTTP task side

void Provisioning::handlePage(HttpRequest& req, void*) {
    req.setHeader("Cache-Control", "no-store");
    req.send(200, "text/html; charset=utf-8", PROVISIONING_HTML, strlen(PROVISIONING_HTML));
}

void Provisioning::sendScan(HttpRequest& req, int status) {
    ScanList list;
    xSemaphoreTake(_lock, portMAX_DELAY);
    list = _scan;
    bool scanning = _scanning || _scanRequested;
    xSemaphoreGive(_lock);

    size_t size = scanListJsonSize(list, scanning);
    char* out = (char*)malloc(size);
    if (!out) {
        sendJsonError(req, 503, "out of memory");
        return;
    }
    scanListJson(list, scanning, out, size);
    req.sendJson(status, out);
    free(out);
}

void Provisioning::handleScanGet(HttpRequest& req, void* ctx) {
    Provisioning* self = (Provisioning*)ctx;
    xSemaphoreTake(self->_lock, portMAX_DELAY);
    if (!self->_scannedOnce && !self->_scanning) self->_scanRequested = true;
    xSemaphoreGive(self->_lock);
    self->sendScan(req, 200);
}

void Provisioning::handleScanPost(HttpRequest& req, void* ctx) {
    Provisioning* self = (Provisioning*)ctx;
    self->requestScan();
    self->sendScan(req, 202);
}

void Provisioning::handleSave(HttpRequest& req, void* ctx) {
    Provisioning* self = (Provisioning*)ctx;

    //a cross-site form can only send text/plain or form data, not JSON
    char type[64];
    if (!req.header("Content-Type", type, sizeof(type)) || strncasecmp(type, "application/json", 16) != 0) {
        sendJsonError(req, 415, "Content-Type must be application/json");
        return;
    }

    char body[SAVE_BODY_MAX];
    size_t len;
    if (!req.readBody(body, sizeof(body), len)) return;

    WifiCredentials creds;
    CREDS_ERROR error = parseCredentials(body, len, creds);
    memset(body, 0, sizeof(body)); //the password was in there
    if (error != CREDS_ERROR::OK) {
        sendJsonError(req, 400, credsErrorMessage(error));
        return;
    }

    xSemaphoreTake(self->_lock, portMAX_DELAY);
    self->_pending = creds;
    self->_hasPending = true;
    xSemaphoreGive(self->_lock);
    memset(&creds, 0, sizeof(creds));

    req.sendJson(200, "{\"ok\":true}");
}
