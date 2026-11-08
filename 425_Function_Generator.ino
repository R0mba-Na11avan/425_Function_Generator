/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: ECE 425 function generator. creates the modules, wires them
             together and starts their tasks. all the work happens in those
             tasks (UI, input, Wi-Fi, HTTP), loop() isn't used
**************************************************************************/

/*
    Startup order
        1. Serial banner
        2. encoder button held at power-on: countdown on the LCD, after
           BOOT_HOLD_MS the saved Wi-Fi is forgotten and the board restarts
           into setup mode. let go early and it boots normally
        3. generator + controller (output starts OFF, sine, 1 Hz, minimum amplitude)
        4. MCP routes on the shared HTTP server (it starts once Wi-Fi is up)
        5. input + display. the network line reads WifiManager through a
           function given to DisplayUI (ui and comms never include each other)
        6. Wi-Fi task: joins the saved network or opens the setup AP

    Objects are created here and not as globals: global constructors run
    before the Arduino core is ready, and MCP4131's constructor already starts
    the SPI bus.
*/

#include "src/config.h"
#include "src/core/func_gen.h"
#include "src/core/funcgen_controller.h"
#include "src/comms/http_server.h"
#include "src/comms/wifi_manager.h"
#include "src/comms/mcp_server.h"
#include "src/ui/input.h"
#include "src/ui/display_ui.h"
#include "src/ui/boot_screen.h"

static NetStatus readNetStatus(void* ctx) {
    return ((WifiManager*)ctx)->status();
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
        if (boot.holdToForgetWifi()) {
            bool forgot = wifi->forgetCredentials();
            Serial.println(forgot ? "funcgen: Wi-Fi forgotten, restarting" : "funcgen: could not clear Wi-Fi");
            boot.message(forgot ? "Wi-Fi forgotten" : "Could not clear Wi-Fi", "Restarting...");
            delay(1500);
            ESP.restart();
        }
    }

    FunctionGenerator* funcGen = new FunctionGenerator(PIN_AD9833_CS, PIN_MCP4131_CS);
    funcGen->begin();
    FuncGenController* controller = new FuncGenController(*funcGen);

    McpServer* mcp = new McpServer(*controller, *http);
    check(mcp->begin(), "MCP routes");

    InputManager* input = new InputManager(*lcdBus);
    check(input->begin(), "input");

    DisplayUI* display = new DisplayUI(*controller, *input, *lcdBus);
    display->setNetStatusSource(readNetStatus, wifi);
    check(display->begin(), "display");

    check(wifi->begin(), "Wi-Fi");
}

void loop() {
    //everything runs in its own task
    vTaskDelete(nullptr);
}
