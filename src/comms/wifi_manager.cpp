/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: Wi-Fi connect/reconnect, AP fallback, mDNS (funcgen.local), saved credentials
**************************************************************************/

#include "wifi_manager.h"

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <string.h>
#include "esp_mac.h"

static const uint32_t WIFI_POLL_MS = 100;

static_assert(sizeof(AP_PASSWORD) == 1 || (sizeof(AP_PASSWORD) >= 9 && sizeof(AP_PASSWORD) <= 64),
              "AP_PASSWORD must be empty (open) or 8-63 characters");

static void formatIp(const IPAddress& ip, char* out, size_t len) {
    snprintf(out, len, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

WifiManager::WifiManager(HttpServer& http)
    : _http(http), _provisioning(http), _task(nullptr), _hasCredentials(false),
      _apRunning(false), _scanRunning(false), _mdnsStarted(false), _attemptFailed(false),
      _setupRequested(false), _leaveSetupRequested(false),
      _homeHandler(nullptr), _homeCtx(nullptr),
      _statusMux(portMUX_INITIALIZER_UNLOCKED) {
    memset(&_creds, 0, sizeof(_creds));
    memset(&_status, 0, sizeof(_status));
    _status.state = WIFI_STATE::IDLE;
    snprintf(_status.hostname, sizeof(_status.hostname), "%s", MDNS_HOSTNAME);
    _apSsid[0] = '\0';
}

void WifiManager::setHomePage(HttpServer::Handler handler, void* ctx) {
    _homeHandler = handler;
    _homeCtx = ctx;
    if (!_apRunning) registerHome();
}

void WifiManager::registerHome() {
    if (_homeHandler && !_http.registerRoute("/", HTTP_METHOD::GET, _homeHandler, _homeCtx)) {
        log_e("could not register the home page");
    }
}

bool WifiManager::begin() {
    if (_task) return true;

    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    apSsidFromMac(mac, _apSsid, sizeof(_apSsid));

    BaseType_t ok = xTaskCreatePinnedToCore(taskEntry, "wifi", WIFI_TASK_STACK, this, WIFI_TASK_PRIO,
                                            &_task, WIFI_TASK_CORE);
    return ok == pdPASS;
}

NetStatus WifiManager::status() const {
    portENTER_CRITICAL(&_statusMux);
    NetStatus s = _status;
    portEXIT_CRITICAL(&_statusMux);
    return s;
}

WIFI_STATE WifiManager::state() const {
    return status().state;
}

void WifiManager::requestSetup() {
    _setupRequested = true;
    if (_task) xTaskNotifyGive(_task);
}

void WifiManager::requestLeaveSetup() {
    _leaveSetupRequested = true;
    if (_task) xTaskNotifyGive(_task);
}

bool WifiManager::forgetCredentials() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) return false;
    bool ok = prefs.clear();
    prefs.end();
    return ok;
}

void WifiManager::taskEntry(void* arg) {
    ((WifiManager*)arg)->run();
}

void WifiManager::run() {
    WiFi.persistent(false);         //credentials live in our own NVS namespace
    WiFi.setAutoReconnect(false);   //WifiSupervisor decides when to retry
    WiFi.setHostname(MDNS_HOSTNAME);

    //runs in the Arduino event task: flag and wake only
    WiFi.onEvent([this](arduino_event_id_t event, arduino_event_info_t info) {
        if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
            //ASSOC_LEAVE is our own disconnect(), not a failure
            if (info.wifi_sta_disconnected.reason != WIFI_REASON_ASSOC_LEAVE) _attemptFailed = true;
        }
        if (_task) xTaskNotifyGive(_task);
    });

    _hasCredentials = loadCredentials();
    apply(_supervisor.begin(_hasCredentials, millis()));
    publishStatus();

    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(WIFI_POLL_MS));

        if (_setupRequested.exchange(false)) apply(_supervisor.openSetup(millis()));
        if (_leaveSetupRequested.exchange(false)) apply(_supervisor.leaveSetup(millis()));

        WifiInputs in;
        in.connected = WiFi.status() == WL_CONNECTED;
        in.attemptFailed = _attemptFailed.exchange(false);
        in.apClients = _apRunning ? WiFi.softAPgetStationNum() : 0;
        apply(_supervisor.update(in, millis()));

        if (_apRunning) serviceScan();
        serviceSave();
        publishStatus();
    }
}

void WifiManager::apply(WIFI_ACTION action) {
    switch (action) {
        case WIFI_ACTION::STA_BEGIN:
            beginSta();
            break;
        case WIFI_ACTION::STA_ONLINE:
            goOnline();
            break;
        case WIFI_ACTION::STA_ABORT:
            log_i("could not join %s, staying in setup mode", _creds.ssid);
            WiFi.disconnect(false, false); //STA only, the AP stays up
            break;
        case WIFI_ACTION::AP_START:
            startAp();
            break;
        case WIFI_ACTION::LEAVE_AP:
            stopAp();
            restartServices();  //the MCP routes are served while joining too
            beginSta();
            break;
        case WIFI_ACTION::NONE:
        default:
            break;
    }
}

void WifiManager::beginSta() {
    log_i("joining %s", _creds.ssid);
    if (!_apRunning) WiFi.mode(WIFI_STA);
    WiFi.begin(_creds.ssid, _creds.password[0] ? _creds.password : nullptr);
}

void WifiManager::goOnline() {
    stopAp();
    restartServices();
    char ip[16];
    formatIp(WiFi.localIP(), ip, sizeof(ip));
    log_i("joined %s, http://%s.local (%s)", _creds.ssid, MDNS_HOSTNAME, ip);
}

void WifiManager::stopAp() {
    if (!_apRunning) return;
    _http.stop(); //no handler may be running while the setup routes go away
    _provisioning.disable();
    registerHome(); //the control page takes / back from the setup page
    WiFi.softAPdisconnect(true);
    _apRunning = false;
    _scanRunning = false;
}

void WifiManager::startAp() {
    if (WiFi.getMode() & WIFI_MODE_STA) WiFi.disconnect(false, false); //stop joining (not started without credentials)
    WiFi.mode(WIFI_AP_STA); //STA is needed for scanning and for trying the saved network again
    const char* password = AP_PASSWORD[0] ? AP_PASSWORD : nullptr;
    if (!WiFi.softAP(_apSsid, password, AP_CHANNEL)) {
        log_e("could not start the access point");
        return;
    }
    _apRunning = true;

    _http.stop();
    if (_homeHandler) _http.unregisterRoute("/", HTTP_METHOD::GET); //in AP mode / is the setup page
    if (!_provisioning.enable()) log_e("could not register the setup routes");
    _provisioning.requestScan(); //so the page has a list right away
    restartServices();

    char ip[16];
    formatIp(WiFi.softAPIP(), ip, sizeof(ip));
    log_i("setup mode: join %s, open http://%s", _apSsid, ip);
}

void WifiManager::restartServices() {
    if (!_http.begin()) log_e("http server did not start");
    if (!_mdnsStarted && MDNS.begin(MDNS_HOSTNAME)) {
        MDNS.addService("http", "tcp", HTTP_PORT);
        _mdnsStarted = true;
    }
}

void WifiManager::serviceScan() {
    if (!_scanRunning) {
        if (!_provisioning.takeScanRequest()) return;
        if (WiFi.scanNetworks(true) == WIFI_SCAN_FAILED) _provisioning.scanFailed();
        else _scanRunning = true;
        return;
    }

    int16_t n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;
    _scanRunning = false;
    if (n < 0) {
        _provisioning.scanFailed();
        return;
    }

    ScanList list;
    clearScanList(list);
    for (int16_t i = 0; i < n; i++) {
        addScanResult(list, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    }
    WiFi.scanDelete();
    _provisioning.scanFinished(list);
}

void WifiManager::serviceSave() {
    WifiCredentials creds;
    if (!_provisioning.takeCredentials(creds)) return;

    bool saved = saveCredentials(creds);
    memset(&creds, 0, sizeof(creds));
    if (!saved) {
        log_e("could not save the credentials");
        return;
    }
    log_i("credentials saved, restarting");
    vTaskDelay(pdMS_TO_TICKS(WIFI_RESTART_DELAY_MS)); //lets the /save response reach the browser
    ESP.restart();
}

void WifiManager::publishStatus() {
    NetStatus s = {};
    s.state = _supervisor.state();
    snprintf(s.hostname, sizeof(s.hostname), "%s", MDNS_HOSTNAME);
    if (_hasCredentials) snprintf(s.savedSsid, sizeof(s.savedSsid), "%s", _creds.ssid);
    switch (s.state) {
        case WIFI_STATE::AP_MODE:
            snprintf(s.ssid, sizeof(s.ssid), "%s", _apSsid);
            if (_apRunning) formatIp(WiFi.softAPIP(), s.ip, sizeof(s.ip));
            break;
        case WIFI_STATE::CONNECTED:
            snprintf(s.ssid, sizeof(s.ssid), "%s", _creds.ssid);
            formatIp(WiFi.localIP(), s.ip, sizeof(s.ip));
            break;
        case WIFI_STATE::CONNECTING:
            snprintf(s.ssid, sizeof(s.ssid), "%s", _creds.ssid);
            break;
        case WIFI_STATE::IDLE:
        default:
            break;
    }

    portENTER_CRITICAL(&_statusMux);
    bool changed = s.state != _status.state || strcmp(s.ip, _status.ip) != 0 || strcmp(s.ssid, _status.ssid) != 0;
    _status = s;
    portEXIT_CRITICAL(&_statusMux);

    //always on Serial (log_i is hidden at the default debug level), so the
    //address can be found without the LCD
    if (!changed) return;
    switch (s.state) {
        case WIFI_STATE::CONNECTING:
            Serial.printf("wifi: connecting to %s\n", s.ssid);
            break;
        case WIFI_STATE::CONNECTED:
            Serial.printf("wifi: connected to %s, MCP at http://%s.local%s (http://%s%s)\n",
                          s.ssid, s.hostname, MCP_PATH, s.ip, MCP_PATH);
            break;
        case WIFI_STATE::AP_MODE:
            if (s.ip[0]) Serial.printf("wifi: setup mode, join %s and open http://%s\n", s.ssid, s.ip);
            break;
        case WIFI_STATE::IDLE:
        default:
            break;
    }
}

bool WifiManager::loadCredentials() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) return false; //namespace doesn't exist yet
    bool found = prefs.isKey(NVS_KEY_SSID);
    if (found) {
        prefs.getString(NVS_KEY_SSID, _creds.ssid, sizeof(_creds.ssid));
        if (prefs.isKey(NVS_KEY_PASS)) prefs.getString(NVS_KEY_PASS, _creds.password, sizeof(_creds.password));
    }
    prefs.end();

    if (!found) return false;
    if (validateCredentials(_creds.ssid, _creds.password) != CREDS_ERROR::OK) {
        log_w("saved credentials are invalid, ignoring them");
        memset(&_creds, 0, sizeof(_creds));
        return false;
    }
    return true;
}

bool WifiManager::saveCredentials(const WifiCredentials& creds) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) return false;
    //putString returns the length written (0 on failure, and also for an empty password)
    bool ok = prefs.putString(NVS_KEY_SSID, creds.ssid) == strlen(creds.ssid)
           && prefs.putString(NVS_KEY_PASS, creds.password) == strlen(creds.password);
    prefs.end();
    return ok;
}
