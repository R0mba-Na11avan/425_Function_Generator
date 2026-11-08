/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: the hardware-free half of WifiManager: the STA/AP state machine,
             credential validation, scan list handling and the AP SSID.
             WifiManager feeds these with what the Wi-Fi driver reports and
             carries out the actions. kept separate so the host tests can run
             them without an ESP32
**************************************************************************/

#ifndef WIFI_LOGIC_H
#define WIFI_LOGIC_H

#include <stdint.h>
#include <stddef.h>
#include "../config.h"
#include "../core/net_status.h"

//what WifiManager has to do next with the radio
enum class WIFI_ACTION : uint8_t {
    NONE,
    STA_BEGIN,  //(re)start a connection attempt to the saved network. the AP stays up if it is running
    STA_ONLINE, //joined. stop the AP and its setup routes if they are running
    STA_ABORT,  //stop trying to join, keep the AP
    AP_START,   //stop trying to join, start the AP and the setup routes
    LEAVE_AP    //stop the AP and its setup routes, then start joining the saved network
};

//what the Wi-Fi driver reported since the last update
struct WifiInputs {
    bool connected;         //joined with an IP
    bool attemptFailed;     //an attempt (or the link) failed since the last update
    uint8_t apClients;      //stations on our AP
};

//decides when to connect, retry, fall back to the AP and try the saved network again
class WifiSupervisor {
    public:
        WIFI_ACTION begin(bool hasCredentials, uint32_t nowMs);
        WIFI_ACTION update(const WifiInputs& in, uint32_t nowMs);

        //the user asked for the setup page (info panel). the saved network
        //stays saved and is tried again as usual from AP mode
        WIFI_ACTION openSetup(uint32_t nowMs);

        //the user asked to go back to the saved network from setup mode. it is
        //joined like at boot: falls back to the AP again if that fails
        WIFI_ACTION leaveSetup(uint32_t nowMs);

        //IDLE, CONNECTING (also while the link is lost), CONNECTED or AP_MODE (also while retrying from it)
        WIFI_STATE state() const;

    private:
        enum class PHASE : uint8_t { IDLE, JOINING, ONLINE, AP, AP_JOINING };

        void startJoining(uint32_t nowMs, uint32_t timeoutMs);
        void scheduleRetry(uint32_t nowMs);
        WIFI_ACTION enterAp(uint32_t nowMs, WIFI_ACTION action);

        PHASE _phase = PHASE::IDLE;
        bool _hasCredentials = false;
        uint32_t _since = 0;        //start of the current phase
        uint32_t _timeout = 0;      //JOINING/AP_JOINING: give up after this long
        bool _retryPending = false;
        uint32_t _retryAt = 0;
        uint32_t _backoff = STA_RETRY_MIN_MS;
};

struct WifiCredentials {
    char ssid[33];      //up to 32 bytes
    char password[65];  //empty (open), 8-63 characters, or 64 hex digits
};

enum class CREDS_ERROR : uint8_t {
    OK,
    BAD_JSON,
    SSID_MISSING,
    SSID_TOO_LONG,
    PASSWORD_INVALID
};

const char* credsErrorMessage(CREDS_ERROR error);

//checks an SSID/password pair (lengths as WPA2 allows them)
CREDS_ERROR validateCredentials(const char* ssid, const char* password);

//parses {"ssid": "...", "password": "..."} (password optional) and validates it
CREDS_ERROR parseCredentials(const char* json, size_t len, WifiCredentials& out);

struct ScanEntry {
    char ssid[33];
    int8_t rssi;    //dBm
    bool secured;
};

//nearby networks, strongest first, one entry per SSID
struct ScanList {
    ScanEntry entries[WIFI_SCAN_MAX];
    uint8_t count;
};

void clearScanList(ScanList& list);

//adds one scan result. hidden networks (empty SSID) are skipped, a repeated
//SSID (mesh, several APs) keeps the strongest signal, and once the list is
//full only stronger networks get in
void addScanResult(ScanList& list, const char* ssid, int32_t rssi, bool secured);

//{"scanning": bool, "networks": [{"ssid", "rssi", "secured"}]}. returns the
//length written (without the NUL), or 0 if out is too small
size_t scanListJson(const ScanList& list, bool scanning, char* out, size_t len);
size_t scanListJsonSize(const ScanList& list, bool scanning); //needed buffer, including the NUL

//AP_SSID_PREFIX + the last two MAC bytes, e.g. "FuncGen-A1B2"
void apSsidFromMac(const uint8_t mac[6], char* out, size_t len);

#endif
