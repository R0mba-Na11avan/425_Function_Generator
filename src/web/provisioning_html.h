/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: Wi-Fi setup page served in AP mode, embedded as a PROGMEM string
**************************************************************************/

/*
    TO IMPLEMENT: one self-contained HTML string (inline CSS and JS, no external
    files or CDNs, since the client has no internet while on the AP).

    Behavior
        - on load, fetch /scan and show the networks (SSID, signal strength, lock
          icon), with a refresh button. also allow typing a hidden SSID
        - password field and a Save button that POSTs {ssid, password} to /save
        - show a "saved, connecting..." message afterwards. the device then leaves
          the AP, so the page loses connection, tell the user that
        - phone-width layout, no framework

    Served by WifiManager only while in AP mode.
*/
