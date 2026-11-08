/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: what the LCD shows before the dashboard starts: the
             hold-the-button-at-boot reset countdown (Wi-Fi + paired phones)
             and short messages
**************************************************************************/

/*
    Used by the sketch in setup(), before DisplayUI and InputManager start, so
    it has the panel and the button to itself. It brings up its own
    Adafruit_ILI9341 on the display bus. DisplayUI initialises the panel again
    afterwards, which is harmless.

    It doesn't know about Wi-Fi or BLE (ui never includes comms): holdToForget()
    only reports that the button was held, the sketch does the forgetting.
*/

#ifndef BOOT_SCREEN_H
#define BOOT_SCREEN_H

#include <Arduino.h>
#include <SPI.h>
#include "../config.h"
#include "../libs/Adafruit_ILI9341/Adafruit_ILI9341.h"

class BootScreen {
    public:
        //bus is the display's SPI bus (HSPI)
        explicit BootScreen(SPIClass& bus);

        //true if the encoder button is down right now
        static bool buttonHeld();

        //shows a countdown while the button stays down. true once it was held
        //for BOOT_HOLD_MS, false as soon as it is let go
        bool holdToForget();

        //full-screen message, e.g. "Wi-Fi and phones forgotten" / "Restarting..."
        void message(const char* title, const char* detail);

    private:
        void start();

        Adafruit_ILI9341 _tft;
        SPIClass& _bus;
        bool _started;
};

#endif
