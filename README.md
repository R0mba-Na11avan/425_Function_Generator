# Function Generator (ECE 425)

Firmware for a function generator built around an ESP32-S3 (N16R8). An AD9833
produces the waveform, an MCP4131 digital potentiometer sets the amplitude, and
the ESP32 provides three ways to control it:

- **Local:** an ILI9341 LCD dashboard with a rotary encoder and touchscreen
- **AI agents:** an MCP server over Wi-Fi (required for the project)
- **Bluetooth (BLE):** planned, implemented last

oui oui baugette

## Architecture

```
 LCD + encoder + touch ─┐
 MCP server (HTTP) ─────┼──► FuncGenController ──► FunctionGenerator ──► AD9833 + MCP4131
 BLE (last) ────────────┘      (mutex, listeners)
```

Every interface is a client of `FuncGenController`. It serializes access to the
hardware (each interface runs in its own task) and notifies listeners when the
state changes, so the LCD always shows what an agent or phone just set. Nothing
outside `core/` touches `FunctionGenerator` directly.

## Layout

```
425_Function_Generator.ino   setup()/loop() only
src/
  config.h   pins, limits, task settings, network constants
  libs/      vendored third-party libs (AD9833, MCP4131, Adafruit GFX + ILI9341, ArduinoJson)
  core/      FunctionGenerator, FuncGenState, FuncGenController
  ui/        display_ui (dashboard drawn with Adafruit GFX), input (encoder + touch)
  comms/     wifi_manager, http_server, mcp_server (+ ble_service, rest_api stubs)
  web/       provisioning_html.h (Wi-Fi setup page), index_html.h (deferred)
```

Dependencies only point downward: `ui`/`comms` -> `core` -> `libs`. `ui` and
`comms` never include each other.

Arduino IDE compiles `src/` recursively but only searches `src/` roots for
headers, so include across folders with relative paths (`"../core/..."`).

## Status

| Part | State |
|---|---|
| `FunctionGenerator` (AD9833 + MCP4131) | done (incl. output enable and limit getters), amplitude mapping is a placeholder |
| `FuncGenController` / `FuncGenState` | done |
| `ui/display_ui`, `ui/input` | stub, spec in the headers |
| `comms/wifi_manager`, `http_server` | stub, spec in the headers |
| `comms/mcp_server` | stub, spec in the headers |
| `comms/ble_service` | stub, **implement last** |
| `comms/rest_api`, `web/index_html.h` | deferred, may not be built |

Suggested order: `config.h` -> display + input -> Wi-Fi + HTTP server (STA, then
AP fallback + provisioning page) -> MCP server -> BLE.

## Design decisions

- **No LVGL.** The dashboard is a single screen drawn directly with Adafruit GFX,
  redrawing only the fields that changed.
- **Wi-Fi:** join the saved network (STA) and fall back to an access point with a
  setup page when that fails, so credentials can be changed without reflashing.
  Credentials are stored in NVS and the device is reachable at `funcgen.local`.
- **MCP:** Streamable HTTP at `POST /mcp`, stateless, basic tools, **no auth**
  (trusted bench network). Revisit if it is ever exposed more widely.
- **Limits live in one place** so the LCD, MCP schemas and `get_limits` can't
  disagree.

## Build (Arduino IDE)

Board: ESP32S3 Dev Module, Flash 16MB, PSRAM "OPI PSRAM", Partition Scheme
"16M Flash (3MB APP/9.9MB FATFS)" (the default 1.2 MB app partition is too small
once Wi-Fi and BLE are in).

Libraries: everything is vendored in `src/libs`, so nothing needs installing from
the Library Manager. ArduinoJson is v7.4.3 (header-only, used by the MCP server),
include it as `#include "../libs/ArduinoJson/ArduinoJson.h"`.

## Tests

Host tests run the real core and vendored chip libraries on a PC against fake
Arduino/SPI/FreeRTOS headers (`test/fakes`), and check what would reach the
AD9833 and MCP4131. Needs g++ (on Windows: `scoop install mingw`):

    bash test/run_tests.sh

VS Code IntelliSense: `python tools/gen_intellisense.py` writes
`.vscode/compile_commands.json` from the real ESP32-S3 build flags.
