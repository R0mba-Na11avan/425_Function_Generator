/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: gets the ESP32 onto a network: joins a saved network (STA) and
             falls back to its own access point so credentials can be changed
             at runtime without reflashing
**************************************************************************/

/*
    TO IMPLEMENT: class WifiManager(HttpServer&)

    Modes
        - STA: join the network whose credentials are stored in NVS
          (Preferences). advertise mDNS as <hostname>.local (config.h) so agents
          can reach http://funcgen.local/mcp
        - AP fallback: if there are no saved credentials, the STA connection
          fails within the timeout, or the connection is lost for too long, start
          an access point (SSID prefix + MAC suffix, from config.h) and serve the
          provisioning page on 192.168.4.1
        - after new credentials are saved, stop the AP and try STA again (or just
          restart the ESP32, which is simpler and acceptable)

    State
        - enum WifiState { IDLE, CONNECTING, CONNECTED, AP_MODE }
        - getters for the state, the current SSID, the IP address and the
          hostname. DisplayUI polls these for the network line, WifiManager must
          not call into the UI

    Provisioning (routes registered on the shared HttpServer, AP mode only)
        - GET  /            -> the setup page (src/web/provisioning_html.h)
        - GET  /scan        -> JSON list of nearby SSIDs (+ RSSI, secured?). use
                               an async scan so the HTTP task isn't blocked
        - POST /save        -> SSID + password, store in NVS, respond, then
                               switch back to STA/restart
        - the page and routes must not exist while in STA mode (otherwise anyone
          on the LAN can overwrite the credentials). note the MCP route is
          registered in both modes

    Other details
        - run the connect/monitor logic in its own task, never block setup()
        - handle reconnect events (WiFi.onEvent) with a backoff
        - since the AP is open by default, do not log or display the password
        - Wi-Fi and (later) BLE share the radio on the S3. keep that in mind
          when BLE gets added

    Public interface (sketch)
        - begin(): start the task
        - state(), ssid(), ip(), hostname()
        - forgetCredentials() to clear NVS (e.g. long-press at boot, optional)
*/
