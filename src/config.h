/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: pins, hardware limits, task settings and network constants
             shared across layers. nothing in here should include anything
             else from src/ (every layer is allowed to include this file)
**************************************************************************/

/*
    TO IMPLEMENT (plain #defines or constexpr, no code):

    Pins (ESP32-S3-N16R8: avoid GPIO26-37, they are used by the octal
    flash/PSRAM, and avoid strapping pins 0/3/45/46 unless you know why)
        - AD9833 chip select (+ SPI pins if it gets its own bus)
        - MCP4131 chip select
        - ILI9341: CS, DC, RST, backlight (SCK/MOSI/MISO)
        - XPT2046 touch: CS and IRQ (shares the ILI9341 SPI bus, so SCK/MOSI/MISO
          are the display's)
        - rotary encoder: A, B, push button
        Decide which SPI hosts are used: the display + touch bus and the
        AD9833/MCP4131 bus should be separate hosts so a slow LCD redraw never
        delays a frequency change.

    Display
        - panel width/height/rotation, SPI clock for the display (write) and a
          much slower SPI clock for the touch controller (XPT2046 is ~2 MHz)
        - touch calibration constants (raw min/max for x and y)

    Task settings (FreeRTOS)
        - priority, stack size and core for: UI task, input task, Wi-Fi task
        - the signal path has no task of its own, setters run in the caller

    Network
        - mDNS hostname ("funcgen" -> funcgen.local)
        - AP fallback: SSID prefix (suffix is the last bytes of the MAC), password
          (empty = open), channel
        - STA connect timeout before falling back to AP, and how long a lost
          connection is tolerated before falling back
        - HTTP server port (80) and MCP path ("/mcp")
        - Preferences/NVS namespace and key names for saved credentials

    Secrets
        - no credentials in this file. credentials live in NVS (set through the
          provisioning page). if a compile-time default is ever wanted, put it in
          a gitignored secrets.h, never here
*/

#ifndef CONFIG_H
#define CONFIG_H

/*
    ALL VALUES BELOW ARE TEMPORARY PLACEHOLDERS, NOT VERIFIED AGAINST THE REAL
    WIRING. Change them here only: every file includes this header, so the
    sketch and all modules pick the new pins up on the next build.

    Bus plan: the generator chips use the default SPI object (FSPI) because
    MCP4131 currently uses the global SPI. The display + touch share a second
    bus (HSPI object).
*/

//generator bus (default SPI / FSPI)
#define PIN_GEN_SCK     12
#define PIN_GEN_MISO    13
#define PIN_GEN_MOSI    11
#define PIN_AD9833_CS   10
#define PIN_MCP4131_CS  9

//display + touch bus (HSPI)
#define PIN_LCD_SCK     4
#define PIN_LCD_MOSI    5
#define PIN_LCD_MISO    6
#define PIN_LCD_CS      7
#define PIN_LCD_DC      15
#define PIN_LCD_RST     16
#define PIN_LCD_BL      17
#define PIN_TOUCH_CS    18
#define PIN_TOUCH_IRQ   8

//rotary encoder
#define PIN_ENC_A       1
#define PIN_ENC_B       2
#define PIN_ENC_SW      42

#endif
