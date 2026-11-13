/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: the browser control page and the /api/... JSON endpoints behind
             it. every change goes through FuncGenController, tagged
             CHANGE_SOURCE::WEB so the LCD shows a "WEB" badge
**************************************************************************/

/*
    Routes (on the shared HttpServer, registered by begin(), in STA and AP mode)
        - GET  WEB_CONTROL_PATH (/control) -> the control page (web/index_html.h)
        - GET  /                           -> the same page, but only on the normal
                                              network: in AP mode / is the Wi-Fi
                                              setup page. WifiManager swaps it
                                              (setHomePage, handlePage)
        - GET  API_STATE_PATH (/api/state) -> state + the ranges allowed right now
        - POST API_STATE_PATH              -> change exactly one setting
        - GET  API_INFO_PATH (/api/info)   -> firmware, Wi-Fi and BLE info (read-only)

    GET /api/state
        { "state": { see api_json.h },
          "ranges": { "frequency_hz": {min, max}, "amplitude_vpp": {min, max},
                      "phase_deg": {min, max, max_exclusive}, "dc_offset_v": {min, max} },
          "output_peak_v": 10 }
        amplitude and offset ranges are the ones allowed right now
        (FuncGenLimits::amplitudeFor / dcOffsetFor), so they move with each other

    POST /api/state
        - body: one key from the state JSON, e.g. {"amplitude_vpp": 2.5},
          {"waveform": "square"}, {"output_enabled": true}
        - none or more than one key, an unknown key, a wrong type or a number
          that isn't finite -> 400 {"error": "..."}
        - out of range is NOT rejected (unlike MCP and BLE): the setter clamps it
          to the edge (phase wraps into 0..360). the reply then has
          "clamped": true and a "note" saying what was applied
        - 200 reply: the GET body + "clamped" (+ "note")
        - same request checks as MCP: Content-Type must be application/json
          (415) and a browser Origin must pass originAllowed (403), so another
          site open in the same browser can't change the output
        - no auth, by choice (trusted bench network), same as MCP

    GET /api/info
        { "firmware", "poll_ms",
          "wifi": { "state": "idle"|"connecting"|"connected"|"setup", "ssid", "ip",
                    "hostname", "saved_ssid", "mcp_url" },
          "ble": { "state": "off"|"advertising"|"connected", "name", "connected", "paired" } }
        wifi / ble are left out until setInfoSources gave a way to read them
        (the sketch passes the same functions it gives DisplayUI)

    Threading
        - handlers run in the HTTP task. the controller's mutex serializes them
          with the LCD, MCP and BLE. nothing is kept between requests
*/

#ifndef REST_API_H
#define REST_API_H

#include <stddef.h>
#include "../config.h"
#include "../core/funcgen_controller.h"
#include "../core/net_status.h"
#include "../core/ble_status.h"
#include "http_server.h"

class RestApi {
    public:
        using NetSource = NetStatus (*)(void* ctx);
        using BleSource = BleStatus (*)(void* ctx);

        RestApi(FuncGenController& controller, HttpServer& http);

        //for /api/info. either may be nullptr. call before begin()
        void setInfoSources(NetSource net, void* netCtx, BleSource ble, void* bleCtx);

        //registers WEB_CONTROL_PATH and the /api routes. works before or after http.begin()
        bool begin();

        //serves the control page. a route handler (ctx unused), so WifiManager can
        //also put it on / while not in setup mode
        static void handlePage(HttpRequest& req, void* ctx);

        //the JSON bodies, without HTTP, so the tests can call them. each writes a
        //malloc'd string into out (caller frees) and returns the HTTP status
        int stateJson(char** out);
        int updateJson(const char* body, size_t len, char** out);
        int infoJson(char** out);

    private:
        static void handleStateGet(HttpRequest& req, void* ctx);
        static void handleStatePost(HttpRequest& req, void* ctx);
        static void handleInfo(HttpRequest& req, void* ctx);

        FuncGenController& _controller;
        HttpServer& _http;
        NetSource _net;
        void* _netCtx;
        BleSource _ble;
        void* _bleCtx;
};

#endif
