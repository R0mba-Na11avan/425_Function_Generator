/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: the Wi-Fi setup routes served in AP mode, and the hand-off
             between them (HTTP task) and WifiManager (Wi-Fi task)
**************************************************************************/

/*
    Routes (registered on the shared HttpServer by enable(), only in AP mode)
        - GET  /      -> the setup page (web/provisioning_html.h)
        - GET  /scan  -> {"scanning": bool, "networks": [{"ssid", "rssi", "secured"}]}
                         the first request starts a scan if none ran yet
        - POST /scan  -> starts a new scan, 202. the page polls GET /scan
                         until scanning is false
        - POST /save  -> {"ssid", "password"} as application/json. validated
                         here, 200 {"ok": true} or 4xx {"error": "..."}

    Threading
        - the handlers never touch the radio or NVS. they leave requests in
          this object and WifiManager's task picks them up (takeScanRequest,
          takeCredentials) and reports back (scanFinished). a scan therefore
          never blocks the HTTP task
        - the password is never logged or sent back
*/

#ifndef PROVISIONING_H
#define PROVISIONING_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "http_server.h"
#include "wifi_logic.h"

class Provisioning {
    public:
        explicit Provisioning(HttpServer& http);

        //register / remove the routes (works whether or not the server is running)
        bool enable();
        void disable();
        bool enabled() const { return _enabled; }

        //Wi-Fi task side
        void requestScan();
        bool takeScanRequest(); //true once per request, the scan then counts as running
        void scanFinished(const ScanList& list);
        void scanFailed();
        bool takeCredentials(WifiCredentials& out);

    private:
        static void handlePage(HttpRequest& req, void* ctx);
        static void handleScanGet(HttpRequest& req, void* ctx);
        static void handleScanPost(HttpRequest& req, void* ctx);
        static void handleSave(HttpRequest& req, void* ctx);
        void sendScan(HttpRequest& req, int status);

        HttpServer& _http;
        SemaphoreHandle_t _lock;
        bool _enabled;

        ScanList _scan;
        bool _scanRequested;
        bool _scanning;
        bool _scannedOnce;

        WifiCredentials _pending;
        bool _hasPending;
};

#endif
