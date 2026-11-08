/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: things the LCD can ask the radios to do (info panel buttons).
             DisplayUI (ui) asks, the sketch passes the request on to
             WifiManager / BleService (comms). it lives in core because ui and
             comms never include each other
**************************************************************************/

#ifndef DEVICE_ACTIONS_H
#define DEVICE_ACTIONS_H

#include <stdint.h>

enum class DEVICE_ACTION : uint8_t {
    OPEN_WIFI_SETUP,    //open the setup AP now, the saved network stays saved
    LEAVE_WIFI_SETUP,   //leave setup mode and join the saved network
    FORGET_PHONES       //forget every paired phone and disconnect them
};

#endif
