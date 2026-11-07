/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: gets the ESP32 onto a network: joins a saved network (STA) and
             falls back to its own access point so credentials can be changed
             at runtime without reflashing
**************************************************************************/

/*
    Modes
        - STA: join the network whose credentials are stored in NVS
          (Preferences, NVS_NAMESPACE). mDNS advertises <MDNS_HOSTNAME>.local
          so agents can reach http://funcgen.local/mcp
        - AP fallback: with no saved credentials, when joining fails within
          STA_CONNECT_TIMEOUT_MS, or when the link stays lost for
          STA_LOST_TIMEOUT_MS, start an access point (AP_SSID_PREFIX + MAC
          suffix) and serve the setup page on 192.168.4.1
        - with saved credentials the AP tries the saved network again every
          AP_STA_RETRY_MS while nobody is connected to it, so a router reboot
          doesn't leave the generator stuck in setup mode
        - after new credentials are saved the ESP32 restarts and tries STA

    Who decides what
        - WifiSupervisor (wifi_logic.h) is the state machine: when to join,
          retry (backoff STA_RETRY_MIN_MS..MAX_MS), fall back or try again
        - Provisioning (provisioning.h) owns the setup routes. its routes exist
          only in AP mode, otherwise anyone on the LAN could overwrite the
          credentials. the MCP route is registered in both modes
        - this class runs the task that feeds both and drives the radio.
          the core's auto-reconnect is off, reconnecting is done here

    Threading
        - everything runs in the Wi-Fi task (WIFI_TASK_*), setup() never blocks
        - the Wi-Fi event callback only sets a flag and wakes the task
        - status() can be called from any task (the UI polls it). this class
          never calls into the UI

    Other details
        - the AP is open by default (AP_PASSWORD). the password of the saved
          network is never logged, displayed or sent back
        - Wi-Fi and (later) BLE share the radio on the S3. coexistence costs
          throughput and adds latency, keep it in mind when BLE gets added
*/

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../config.h"
#include "../core/net_status.h"
#include "http_server.h"
#include "provisioning.h"
#include "wifi_logic.h"

class WifiManager {
    public:
        //starts and restarts the server whenever the network comes up or changes mode
        explicit WifiManager(HttpServer& http);

        //starts the Wi-Fi task and returns immediately
        bool begin();

        //snapshot for the UI (state, SSID, IP, hostname). any task
        NetStatus status() const;
        WIFI_STATE state() const;

        //clears the saved network. takes effect after the next restart
        bool forgetCredentials();

    private:
        static void taskEntry(void* arg);
        void run();

        void apply(WIFI_ACTION action);
        void startAp();
        void beginSta();
        void goOnline();
        void restartServices();
        void serviceScan();
        void serviceSave();
        void publishStatus();

        bool loadCredentials();
        bool saveCredentials(const WifiCredentials& creds);

        HttpServer& _http;
        Provisioning _provisioning;
        WifiSupervisor _supervisor;
        TaskHandle_t _task;

        WifiCredentials _creds;
        bool _hasCredentials;
        char _apSsid[33];
        bool _apRunning;
        bool _scanRunning;
        bool _mdnsStarted;
        std::atomic<bool> _attemptFailed; //set by the event callback

        mutable portMUX_TYPE _statusMux;
        NetStatus _status;
};

#endif
