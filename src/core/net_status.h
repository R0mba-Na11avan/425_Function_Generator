/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: snapshot of the network state. WifiManager (comms) produces it
             and DisplayUI (ui) shows it. it lives in core because ui and
             comms never include each other: the sketch hands DisplayUI a
             function that reads it from WifiManager
**************************************************************************/

#ifndef NET_STATUS_H
#define NET_STATUS_H

#include <stdint.h>

enum class WIFI_STATE : uint8_t {
    IDLE,
    CONNECTING,
    CONNECTED,
    AP_MODE
};

struct NetStatus {
    WIFI_STATE state;
    char ssid[33];      //network being joined, or our own AP's SSID in AP_MODE
    char ip[16];        //dotted quad, empty until there is one
    char hostname[32];  //mDNS name without ".local"
};

#endif
