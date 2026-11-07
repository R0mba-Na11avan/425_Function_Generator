/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: pins, hardware limits, task settings and network constants
             shared across layers. nothing in here should include anything
             else from src/ (every layer is allowed to include this file)
**************************************************************************/

/*
    Plain #defines only, no code.

    Pins (ESP32-S3-N16R8): avoid GPIO26-37, they are used by the octal
    flash/PSRAM, and avoid strapping pins 0/3/45/46 unless you know why.

    Secrets: no credentials in this file. credentials live in NVS (set through
    the provisioning page). if a compile-time default is ever wanted, put it in
    a gitignored secrets.h, never here
*/

#ifndef CONFIG_H
#define CONFIG_H

/*
    ALL PIN VALUES BELOW ARE TEMPORARY PLACEHOLDERS, NOT VERIFIED AGAINST THE
    REAL WIRING. Change them here only: every file includes this header, so the
    sketch and all modules pick the new pins up on the next build.

    Bus plan: the generator chips use the default SPI object (FSPI) because
    MCP4131 currently uses the global SPI. The display + touch share a second
    bus (HSPI object), so a slow LCD redraw never delays a frequency change.
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


/*
    Signal limits. This is the only place valid ranges are defined: the LCD,
    the MCP tool schemas and get_limits all read them through
    FunctionGenerator / FuncGenController, so they can't disagree.
*/

//frequency (Hz). the max can't exceed AD9833_MAX_FREQ (checked in func_gen.cpp)
#define FREQ_MIN_HZ         0.1f
#define FREQ_MAX_HZ         12500000.0f

//phase (degrees). the max is exclusive, values wrap back to 0
#define PHASE_MIN_DEG       0.0f
#define PHASE_MAX_DEG       360.0f

//amplitude (Vpp), per waveform.
//PLACEHOLDERS until the output stage is built and measured on a scope.
//the min is the AD9833's own sine output (650 mV - 38 mV, from the datasheet).
//the square wave comes out at V_CC-0 instead, so it needs a voltage divider
//(2N7000 switch) to keep the amplitude consistent with sine/triangle
#define AMP_MIN_VPP_SINE        (0.650f - 0.038f)
#define AMP_MAX_VPP_SINE        5.0f
#define AMP_MIN_VPP_SQUARE      (0.650f - 0.038f)
#define AMP_MAX_VPP_SQUARE      5.0f
#define AMP_MIN_VPP_TRIANGLE    (0.650f - 0.038f)
#define AMP_MAX_VPP_TRIANGLE    5.0f

//DC offset (V). PLACEHOLDER, offset is stored but not applied to hardware yet
#define DC_OFFSET_MIN_V     -2.5f
#define DC_OFFSET_MAX_V     2.5f

//MCP4131 is a 7-bit pot: wiper positions 0..128
#define MCP4131_WIPER_MAX   128


/*
    Display (ILI9341 320x240 + XPT2046 touch)
*/

#define LCD_WIDTH           320     //after rotation
#define LCD_HEIGHT          240
#define LCD_ROTATION        1       //landscape
#define LCD_SPI_HZ          40000000

//redraw rate cap for the UI task
#define UI_FRAME_MS         40      //~25 Hz

//touch calibration: raw XPT2046 readings at the screen edges.
//PLACEHOLDERS, measure them with the touch calibration mode once the panel is wired.
//the XPT2046 runs at its own ~2 MHz SPI clock (set inside the touch library)
#define TOUCH_RAW_X_MIN     200
#define TOUCH_RAW_X_MAX     3900
#define TOUCH_RAW_Y_MIN     200
#define TOUCH_RAW_Y_MAX     3900


/*
    FreeRTOS tasks: priority, stack size (bytes) and core.
    The signal path has no task of its own, setters run in the caller.
    Wi-Fi and HTTP live on core 0 next to the radio, UI and input on core 1.
*/

#define UI_TASK_PRIO        2
#define UI_TASK_STACK       8192
#define UI_TASK_CORE        1

#define INPUT_TASK_PRIO     3
#define INPUT_TASK_STACK    4096
#define INPUT_TASK_CORE     1

#define WIFI_TASK_PRIO      1
#define WIFI_TASK_STACK     4096
#define WIFI_TASK_CORE      0

#define HTTP_TASK_PRIO      2
#define HTTP_TASK_STACK     10240   //ArduinoJson parsing runs on this stack
#define HTTP_TASK_CORE      0


/*
    Network
*/

#define MDNS_HOSTNAME           "funcgen"   //-> funcgen.local

//AP fallback: SSID is the prefix + the last bytes of the MAC
#define AP_SSID_PREFIX          "FuncGen-"
#define AP_PASSWORD             ""          //empty = open
#define AP_CHANNEL              6

#define STA_CONNECT_TIMEOUT_MS  15000       //before falling back to AP
#define STA_LOST_TIMEOUT_MS     30000       //lost connection tolerated this long

#define HTTP_PORT               80
#define HTTP_MAX_BODY           4096        //bigger request bodies get 413
#define MCP_PATH                "/mcp"

//Preferences/NVS for the saved credentials
#define NVS_NAMESPACE           "wifi"
#define NVS_KEY_SSID            "ssid"
#define NVS_KEY_PASS            "pass"

#endif
