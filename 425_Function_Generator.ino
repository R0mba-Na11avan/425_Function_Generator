/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: ECE 425 function generator. creates the modules, wires them
             together and starts their tasks. all the work happens in those
             tasks (UI, input, Wi-Fi, HTTP, BLE), loop() isn't used
**************************************************************************/

/*
    Startup order
        1. Serial banner
        2. encoder button held at power-on: countdown on the LCD, after
           BOOT_HOLD_MS the saved Wi-Fi and the paired phones are forgotten
           and the board restarts into setup mode. let go early and it boots
           normally
        3. generator + controller (output starts OFF, sine, 1 Hz, 1 Vpp, 0 V offset)
        4. MCP and web control routes on the shared HTTP server (it starts
           once Wi-Fi is up). the control page is also the home page (/)
           except in setup mode, where / is the Wi-Fi setup page
        5. input + display. the network line and the BLE status/pairing popup
           read WifiManager and BleService through functions given to
           DisplayUI (ui and comms never include each other)
        6. Wi-Fi task: joins the saved network or opens the setup AP
        7. BLE: advertises, phones pair with the passkey shown on the LCD

    The info panel's buttons (Change Wi-Fi, Back to <network>, Forget phones)
    reach WifiManager and BleService through runDeviceAction.

    Objects are created here and not as globals: global constructors run
    before the Arduino core is ready.
*/

#include "src/config.h"
#include "src/core/func_gen.h"
#include "src/core/funcgen_controller.h"
#include "src/comms/http_server.h"
#include "src/comms/wifi_manager.h"
#include "src/comms/mcp_server.h"
#include "src/comms/rest_api.h"
#include "src/comms/ble_service.h"
#include "src/ui/input.h"
#include "src/ui/display_ui.h"
#include "src/ui/boot_screen.h"

static NetStatus readNetStatus(void* ctx) {
    return ((WifiManager*)ctx)->status();
}

static BleStatus readBleStatus(void* ctx) {
    return ((BleService*)ctx)->status();
}

static void cancelBlePairing(void* ctx) {
    ((BleService*)ctx)->cancelPairing();
}

//what runDeviceAction needs, set once in setup()
struct Radios {
    WifiManager* wifi;
    BleService* ble;
};
static Radios s_radios;

//UI task: the requests only set a flag and wake the radio's own task
static void runDeviceAction(DEVICE_ACTION action, void* ctx) {
    Radios* r = (Radios*)ctx;
    switch (action) {
        case DEVICE_ACTION::OPEN_WIFI_SETUP: r->wifi->requestSetup(); break;
        case DEVICE_ACTION::LEAVE_WIFI_SETUP: r->wifi->requestLeaveSetup(); break;
        case DEVICE_ACTION::FORGET_PHONES: r->ble->requestForgetPhones(); break;
    }
}

static void check(bool ok, const char* what) {
    if (!ok) Serial.printf("setup: %s failed\n", what);
}

void setup() {
    Serial.begin(115200);
    Serial.printf("\nfuncgen: firmware %s\n", FIRMWARE_VERSION);

    //display + touch bus. the generator chips use the default SPI (FSPI)
    SPIClass* lcdBus = new SPIClass(HSPI);
    HttpServer* http = new HttpServer();
    WifiManager* wifi = new WifiManager(*http);

    if (BootScreen::buttonHeld()) {
        BootScreen boot(*lcdBus);
        if (boot.holdToForget()) {
            bool wifiForgot = wifi->forgetCredentials();
            bool phonesForgot = BleService::forgetPhones();
            bool forgot = wifiForgot && phonesForgot;
            Serial.println(forgot ? "funcgen: Wi-Fi and phones forgotten, restarting"
                                  : "funcgen: could not clear Wi-Fi or phones");
            boot.message(forgot ? "Wi-Fi and phones forgotten" : "Could not clear everything", "Restarting...");
            delay(1500);
            ESP.restart();
        }
    }

    FunctionGenerator* funcGen = new FunctionGenerator(PIN_AD9833_CS, PIN_AMP_DAC_CS, PIN_OFFSET_DAC_CS, PIN_WAVE_SWITCH);
    funcGen->begin();
    FuncGenController* controller = new FuncGenController(*funcGen);

    McpServer* mcp = new McpServer(*controller, *http);
    check(mcp->begin(), "MCP routes");

    BleService* ble = new BleService(*controller);

    RestApi* web = new RestApi(*controller, *http);
    web->setInfoSources(readNetStatus, wifi, readBleStatus, ble);
    check(web->begin(), "web control routes");
    wifi->setHomePage(RestApi::handlePage, web);

    InputManager* input = new InputManager(*lcdBus);
    check(input->begin(), "input");

    DisplayUI* display = new DisplayUI(*controller, *input, *lcdBus);
    display->setNetStatusSource(readNetStatus, wifi);
    display->setBleSource(readBleStatus, cancelBlePairing, ble);
    s_radios = Radios{wifi, ble};
    display->setActionHandler(runDeviceAction, &s_radios);
    check(display->begin(), "display");

    check(wifi->begin(), "Wi-Fi");
    check(ble->begin(), "Bluetooth");
}

void loop() {
    //everything runs in its own task
    vTaskDelete(nullptr);
}
