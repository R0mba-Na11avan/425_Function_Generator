/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: boot-time Wi-Fi + Bluetooth reset countdown and messages on the LCD
**************************************************************************/

#include "boot_screen.h"
#include "input_logic.h"
#include "dashboard_view.h"

static const uint32_t BOOT_POLL_MS = 10;

BootScreen::BootScreen(SPIClass& bus)
    : _tft(&bus, PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_RST), _bus(bus), _started(false) {}

bool BootScreen::buttonHeld() {
    pinMode(PIN_ENC_SW, INPUT_PULLUP);
    delay(2); //let the pull-up charge the line
    return digitalRead(PIN_ENC_SW) == LOW;
}

void BootScreen::start() {
    if (_started) return;
    _started = true;
    _bus.begin(PIN_LCD_SCK, PIN_LCD_MISO, PIN_LCD_MOSI);
    pinMode(PIN_LCD_BL, OUTPUT);
    digitalWrite(PIN_LCD_BL, HIGH);
    _tft.begin(LCD_SPI_HZ);
    _tft.setRotation(LCD_ROTATION);
}

bool BootScreen::holdToForget() {
    start();
    HoldTimer hold(BOOT_HOLD_MS, millis());
    int shown = -1;
    for (;;) {
        uint32_t now = millis();
        HOLD_STATE s = hold.update(digitalRead(PIN_ENC_SW) == LOW, now);
        if (s == HOLD_STATE::RELEASED) return false;
        if (s == HOLD_STATE::DONE) return true;

        int seconds = (int)((hold.remainingMs(now) + 999) / 1000);
        if (seconds != shown) {
            shown = seconds;
            renderBootHold(_tft, (uint8_t)seconds);
        }
        delay(BOOT_POLL_MS);
    }
}

void BootScreen::message(const char* title, const char* detail) {
    start();
    renderBootMessage(_tft, title, detail);
}
