/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: the one shared HTTP server. other modules register their routes
             on it so there is only a single listening socket/task
**************************************************************************/

/*
    TO IMPLEMENT: class HttpServer

    Why shared
        - the ESP32 has a limited number of sockets and RAM. WifiManager
          (provisioning) and McpServer (and a later REST API/web UI) each add routes
          to the same server instead of starting their own

    Implementation
        - use esp_http_server (ships with the Arduino-ESP32 core, so no extra
          library). handlers run in the httpd task, which is fine because every
          write goes through FuncGenController's mutex
        - raise max_uri_handlers and stack_size in the config to fit the routes
          and ArduinoJson parsing

    Public interface (sketch)
        - begin(): start listening on the port in config.h. safe to call once
          Wi-Fi (STA or AP) is up. it should be restartable if the mode changes
        - registerRoute(path, method, handler, userCtx): thin wrapper over
          httpd_register_uri_handler so callers don't touch the esp-idf types
        - unregisterRoute(path, method): needed for the provisioning routes,
          which must disappear when leaving AP mode
        - helpers for the common patterns: read the whole request body into a
          buffer (with a max size to prevent heap exhaustion), send JSON with
          the right Content-Type, send an error status

    Limits
        - cap the body size (a few KB is plenty for every request here) and
          return 413 beyond it
        - no TLS and no auth, by choice, for now. this is a bench instrument on a
          trusted network
*/
