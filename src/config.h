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

    Bus plan: the generator chips (AD9833 + both AD5452s) share the default
    SPI object (FSPI). The display + touch share a second bus (HSPI object),
    so a slow LCD redraw never delays a frequency change.
*/

//generator bus (default SPI / FSPI)
#define PIN_GEN_SCK         12
#define PIN_GEN_MISO        13
#define PIN_GEN_MOSI        11
#define PIN_AD9833_CS       10
#define PIN_AMP_DAC_CS      9       //AD5452 #1 SYNC: scales the amplified wave (amplitude)
#define PIN_OFFSET_DAC_CS   14      //AD5452 #2 SYNC: sets the DC offset

//TS5A3159A analog switch IN pin: LOW = NC = filtered sine/triangle path,
//HIGH = NO = divided square path. put a pull-down on it so the sine path is
//selected while the ESP32 boots
#define PIN_WAVE_SWITCH     21

//display + touch bus (HSPI). wire T_CLK/T_DIN/T_DO to the same SCK/MOSI/MISO.
//MISO is only for the touch chip's T_DO: leave the LCD's own SDO(MISO) pin
//unconnected, on some of these modules it doesn't release the line and
//corrupts touch reads (the display is never read from)
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

/*
    Output stage (see docs/parts.md): AD9833 -> TS5A3159A (sine/triangle or
    divided square) -> x20 gain -> AD5452 #1 (0..100 %) -> summer with
    AD5452 #2 (DC offset) -> 50 ohm -> output.

    ALL VALUES BELOW ARE PLACEHOLDERS until the stage is built and measured on
    a scope. Calibrating = measuring the numbers marked "measure".
*/

//amplitude (Vpp into high-Z), per waveform (the square goes through its own
//divider, so it can differ). min..max is the user-facing range
#define AMP_MIN_VPP_SINE        0.0f
#define AMP_MAX_VPP_SINE        12.0f
#define AMP_MIN_VPP_SQUARE      0.0f
#define AMP_MAX_VPP_SQUARE      12.0f
#define AMP_MIN_VPP_TRIANGLE    0.0f
#define AMP_MAX_VPP_TRIANGLE    12.0f

//measure: output Vpp with the amplitude AD5452 at full code (4095), per
//waveform. the AD5452 is linear, so every other code scales from this.
//must be >= the matching AMP_MAX (checked in func_gen.cpp)
#define AMP_FULL_CODE_VPP_SINE      12.0f
#define AMP_FULL_CODE_VPP_SQUARE    12.0f
#define AMP_FULL_CODE_VPP_TRIANGLE  12.0f

//amplitude at power-on (clamped into the waveform's range)
#define AMP_DEFAULT_VPP         1.0f

//DC offset (V). this is the widest range, the real one also has to respect
//OUTPUT_PEAK_MAX_V below
#define DC_OFFSET_MIN_V     -10.0f
#define DC_OFFSET_MAX_V     10.0f

//measure: DC at the output with the offset AD5452 at code 0 and at full code
//(4095), amplitude AD5452 at 0. if the wiring flips the sign, these just swap.
//the AD5452 powers up at code 0, so the output sits at OFFSET_V_AT_CODE_0
//until FunctionGenerator::begin() runs (keep begin() early in setup)
#define OFFSET_V_AT_CODE_0      -10.0f
#define OFFSET_V_AT_FULL_CODE   10.0f

//the op-amps can't swing past this, so amplitude / 2 + |offset| must stay at
//or below it. changing one setting never moves the other: the one being
//changed stops at the edge instead
#define OUTPUT_PEAK_MAX_V   10.0f

//SPI clock for both AD5452s (the chip allows up to 50 MHz)
#define MDAC_SPI_HZ         8000000


/*
    Display (ILI9341 320x240 + XPT2046 touch)
*/

#define LCD_WIDTH           320     //after rotation
#define LCD_HEIGHT          240
#define LCD_ROTATION        3       //landscape, flipped 180 to match the touch panel
#define LCD_SPI_HZ          40000000

//redraw rate cap for the UI task
#define UI_FRAME_MS         40      //~25 Hz
#define UI_NET_POLL_MS      500     //how often the network line is refreshed

//editing steps on the dashboard. frequency steps are 0.1 Hz .. 1 MHz (x10 each),
//picked with a press while editing frequency or by tapping a digit
#define UI_FREQ_STEP_DEFAULT    4       //index into 0.1, 1, 10, 100, 1k, ... -> 1 kHz
#define UI_AMP_STEP_V           0.05f
#define UI_OFFSET_STEP_V        0.05f

#define UI_EDIT_TIMEOUT_MS      10000   //edit mode ends after this long without input
#define UI_REMOTE_FLASH_MS      1000    //a field an agent changed is highlighted this long
#define UI_REMOTE_BADGE_MS      5000    //"MCP"/"BLE" shows in the status bar this long after a remote change
#define UI_PAIR_RESULT_MS       2000    //"Paired" / "Pairing failed" stays on screen this long

//hold the encoder button this long while powering on to forget the saved Wi-Fi
//and the paired phones, and restart into setup mode
#define BOOT_HOLD_MS            3000

//touch calibration: raw XPT2046 readings (0..4095) at the left/right and
//top/bottom screen edges, after the optional X/Y swap. if an axis is mirrored,
//just put the bigger number in _MIN. PLACEHOLDERS: set TOUCH_CALIBRATION_MODE
//to 1, tap the corners and read the raw values off the serial monitor.
//the XPT2046 runs at its own ~2 MHz SPI clock (set inside the touch library)
#define TOUCH_RAW_X_MIN     200
#define TOUCH_RAW_X_MAX     3900
#define TOUCH_RAW_Y_MIN     200
#define TOUCH_RAW_Y_MAX     3900
#define TOUCH_SWAP_XY       0       //1 if the panel's X axis runs along the screen's Y
#define TOUCH_CALIBRATION_MODE 0    //1 = print raw touch readings on Serial


/*
    Input (encoder, button, touch). All times in ms.
*/

#define INPUT_POLL_MS           5       //input task period
#define INPUT_QUEUE_LEN         16
#define INPUT_QUEUE_RESERVED    2       //slots encoder events leave free for button/touch

//rotary encoder, decoded by the PCNT peripheral (x4 quadrature)
#define ENC_COUNTS_PER_DETENT   4       //most mechanical encoders: 4 edges per click
#define ENC_GLITCH_NS           1000    //PCNT glitch filter (max ~12700 ns)
#define ENC_ACCEL_SLOW_MS       100     //clicks further apart than this count 1x
#define ENC_ACCEL_MAX           10      //max multiplier when spun fast

//encoder push button: to GND, internal pull-up, so LOW = pressed
#define BUTTON_DEBOUNCE_MS      25
#define BUTTON_LONG_MS          500

//tap = finger lifted within TOUCH_TAP_MAX_MS, moved less than TOUCH_TAP_MAX_MOVE_PX
#define TOUCH_TAP_MAX_MS        600
#define TOUCH_TAP_MAX_MOVE_PX   15
#define TOUCH_RELEASE_MS        30      //no touch for this long = finger lifted


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
#define WIFI_TASK_STACK     6144    //NVS, scan results and logging
#define WIFI_TASK_CORE      0

#define HTTP_TASK_PRIO      2
#define HTTP_TASK_STACK     10240   //ArduinoJson parsing runs on this stack
#define HTTP_TASK_CORE      0

#define BLE_TASK_PRIO       1
#define BLE_TASK_STACK      3072    //only hands work to the NimBLE task (core 0)
#define BLE_TASK_CORE       0


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
#define STA_RETRY_MIN_MS        1000        //wait after a failed attempt, doubles each time
#define STA_RETRY_MAX_MS        8000
#define AP_STA_RETRY_MS         300000      //in AP fallback with saved credentials, try the saved
                                            //network again this often while nobody is on the AP. 0 = never
#define WIFI_SCAN_MAX           20          //networks listed on the setup page
#define WIFI_RESTART_DELAY_MS   1000        //after saving credentials, lets the /save response get out

#define HTTP_PORT               80
#define HTTP_MAX_BODY           4096        //bigger request bodies get 413
#define HTTP_MAX_ROUTES         12          //path + method pairs on the shared server
#define HTTP_MAX_PATH_LEN       32
#define MCP_PATH                "/mcp"
#define MCP_SERVER_NAME         "funcgen"
#define FIRMWARE_VERSION        "0.1.0"     //reported to MCP clients as serverInfo.version

//Preferences/NVS for the saved credentials
#define NVS_NAMESPACE           "wifi"
#define NVS_KEY_SSID            "ssid"
#define NVS_KEY_PASS            "pass"


/*
    Bluetooth (BLE). Advertised name = the setup AP's SSID (FuncGen-XXXX)
*/

#define BLE_ADV_INTERVAL_MS     100         //longer = less radio time taken from Wi-Fi, slower to find
#define BLE_PAIR_TIMEOUT_MS     30000       //the passkey popup cancels a pairing after this long
#define BLE_BOND_NVS_NAMESPACE  "nimble_bond"   //where NimBLE keeps paired phones. set by the stack, don't change

#endif
