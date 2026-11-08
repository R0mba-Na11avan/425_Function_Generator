# Function Generator (ECE 425)

Firmware for a function generator built around an ESP32-S3 (N16R8). An AD9833
produces the waveform, an MCP4131 digital potentiometer sets the amplitude, and
the ESP32 provides three ways to control it:

- **Local:** an ILI9341 LCD dashboard with a rotary encoder and touchscreen
- **AI agents:** an MCP server over Wi-Fi (required for the project)
- **Bluetooth (BLE):** a GATT service a phone can read, write and subscribe to
  (tested with nRF Connect), paired with a passkey shown on the LCD

oui oui baugette

## Architecture

```
 LCD + encoder + touch ─┐
 MCP server (HTTP) ─────┼──► FuncGenController ──► FunctionGenerator ──► AD9833 + MCP4131
 BLE (NimBLE) ──────────┘      (mutex, listeners)
```

Every interface is a client of `FuncGenController`. It serializes access to the
hardware (each interface runs in its own task) and notifies listeners when the
state changes, so the LCD always shows what an agent or phone just set. Nothing
outside `core/` touches `FunctionGenerator` directly.

## Layout

```
425_Function_Generator.ino   creates and wires everything in setup(), loop() deletes itself
src/
  config.h   pins, limits, task settings, network constants
  libs/      vendored third-party libs (AD9833, MCP4131, Adafruit GFX + ILI9341,
             XPT2046_Touchscreen, ArduinoJson)
  core/      FunctionGenerator, FuncGenState, FuncGenController
  core/      ... + net_status.h / ble_status.h (Wi-Fi and BLE state shared by comms and ui)
  ui/        display_ui (UI task, canvases, listener), dashboard_view (drawing),
             dashboard_logic (layout, formatting, editor), input (encoder + touch),
             input_logic (debounce/accel/tap/hold), boot_screen (Wi-Fi + phones reset countdown).
             the *_logic/_view files are host-tested
  comms/     wifi_manager (+ wifi_logic state machine, provisioning routes),
             http_server, mcp_server, ble_service (+ ble_logic GATT layout and
             encoding), rest_api stub
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
| `ui/input` (encoder via PCNT, button, XPT2046 touch) | done, not tried on hardware yet, touch calibration is a placeholder |
| `ui/display_ui` (dashboard, editing, info panel with Wi-Fi/phone buttons, remote-change flash, pairing popup) | done, not tried on hardware yet |
| `425_Function_Generator.ino` (wiring, boot-time Wi-Fi + phones reset) | done, compiled, not tried on hardware yet |
| `comms/http_server` (shared esp_http_server + route table) | done, host-tested against a fake httpd, not tried on hardware yet |
| `comms/wifi_manager` (STA, AP fallback, setup page, mDNS, NVS) | done, state machine and setup routes host-tested, radio part only compiled, not tried on hardware yet |
| `comms/mcp_server` (JSON-RPC, 8 tools) | done, host-tested end to end against a fake httpd, not tried on hardware yet |
| `comms/ble_service` (NimBLE GATT, passkey pairing, notifications) | done, encoding and range checks host-tested, the stack part only compiled, not tried on hardware yet |
| `comms/rest_api`, `web/index_html.h` | deferred, may not be built |

Suggested order: `config.h` -> display + input -> Wi-Fi + HTTP server (STA, then
AP fallback + provisioning page) -> MCP server -> BLE.

## Design decisions

- **No LVGL.** The dashboard is a single screen drawn directly with Adafruit GFX,
  redrawing only the fields that changed.
- **Wi-Fi:** join the saved network (STA) and fall back to an access point with a
  setup page when that fails, so credentials can be changed without reflashing.
  Credentials are stored in NVS and the device is reachable at `funcgen.local`.
  The setup routes only exist in AP mode. In AP mode with saved credentials it
  tries the saved network again every 5 minutes while nobody is on the AP.
  To change networks, "Change Wi-Fi" in the info panel opens setup mode while
  running (the old network stays saved until a new one is saved). The page is
  never served on the normal network, so a change always needs someone at the
  device.
- **MCP:** Streamable HTTP at `POST /mcp`, stateless, basic tools, **no auth**
  (trusted bench network). Revisit if it is ever exposed more widely.
  Out-of-range values are rejected with a tool error (the LCD clamps instead).
  Requests need `Content-Type: application/json`, and a browser `Origin` must be
  `funcgen.local` or an IP address, so a web page can't drive the generator.
- **BLE:** NimBLE (built into the ESP32 core) through its C API. The Arduino BLE
  wrapper always answers a write with success, so it couldn't reject bad values.
  Every characteristic needs an encrypted, MITM-authenticated link: a new phone
  has to type the 6-digit passkey shown on the LCD, so only someone who can see
  the screen can pair. Up to 3 phones are remembered (oldest dropped). Values are
  fixed-unit integers and out-of-range writes are rejected, like MCP.
- **Limits live in one place** so the LCD, MCP schemas, `get_limits` and the BLE
  range checks can't disagree.

## Build (Arduino IDE)

Board: ESP32S3 Dev Module, Flash 16MB, PSRAM "OPI PSRAM", Partition Scheme
"16M Flash (3MB APP/9.9MB FATFS)" (the default 1.2 MB app partition is too small
once Wi-Fi and BLE are in).

Libraries: everything is vendored in `src/libs`, so nothing needs installing from
the Library Manager. ArduinoJson is v7.4.3 as the official single-header release
(`ArduinoJson-v7.4.3.h`, renamed; the multi-file version needs the library on the
include path, which the IDE doesn't do for `src/libs`). It is used by the Wi-Fi
setup routes and the MCP server, include it as
`#include "../libs/ArduinoJson/ArduinoJson.h"`.

## Using it

- **Knob:** turn to pick a field, press to edit it (press on OUTPUT toggles the
  output), turn to change the value, press again when done. Edit mode also ends
  after 10 s without input. Long press on FREQUENCY changes the step.
- **Touch:** tap a field to select it, tap it again to edit, tap a frequency
  digit to step that digit. Tap the status bar for the network info panel.
- **Remote changes:** a field changed over MCP or BLE flashes magenta and an
  "MCP" or "BLE" badge shows in the status bar for a few seconds. If you were
  editing that field, editing stops and your next turn starts from the new value.
- **Bluetooth:** a Bluetooth icon at the right of the status bar means a phone is
  connected. When a new phone pairs, a full-screen popup shows the code to type
  on the phone; press or tap to cancel (it cancels itself after 30 s).
- **Power-on:** sine, 1 Hz, minimum amplitude, output OFF.
- **Info panel buttons:** turn the knob to highlight "Change Wi-Fi", "Forget
  phones" or "close" and press, or tap them.
  - *Change Wi-Fi* (asks first) opens setup mode; the info panel then shows the
    steps. The current network stays saved until you save a new one. In setup
    mode the button becomes *Back to <network>*, which goes straight back. With
    nobody on the setup Wi-Fi it also goes back by itself after 5 minutes.
  - *Forget phones* (asks first) forgets every paired phone and disconnects the
    connected ones. Also remove FuncGen from each phone's Bluetooth settings.
- **Backup reset:** if the screen or touch isn't usable, hold the knob button
  while powering on, through the 3 s countdown. The saved network and the paired
  phones are forgotten and the generator restarts in setup mode.
- **Serial (115200):** prints the firmware version, every network address
  change and BLE pairing results.

## Connecting an agent

Once the generator is on your network (the LCD shows its address):

    claude mcp add --transport http funcgen http://funcgen.local/mcp

If `funcgen.local` doesn't resolve, use the IP from the LCD instead. To poke at it
by hand, run `npx @modelcontextprotocol/inspector`, choose "Streamable HTTP" and
the same URL (the Inspector's default proxy mode works; its direct mode needs
CORS, which this server doesn't offer).

Tools: `set_waveform`, `set_frequency`, `set_amplitude`, `set_phase`,
`set_dc_offset`, `set_output`, `get_state`, `get_limits`.

## Connecting a phone (BLE)

1. Install **nRF Connect for Mobile** (Nordic Semiconductor, free on iOS and
   Android) and open it. It scans for nearby devices.
2. Tap **Connect** next to `FuncGen-XXXX` (same name as the setup Wi-Fi; the
   info panel shows it).
3. The phone asks to pair: type the code from the LCD popup. This happens once
   per phone.
4. Open the service `54750f00-...`. Each characteristic is labelled. Tap the up
   arrow to write a value, the triple arrow to get live updates.

| UUID | Setting | Bytes | Unit / values | Example |
|---|---|---|---|---|
| `54750f01-…` | waveform | 1 (u8) | 0 sine, 1 square, 2 triangle | `02` |
| `54750f02-…` | frequency | 4 (u32) | 0.01 Hz | 1 kHz = 100000 = `A0 86 01 00` |
| `54750f03-…` | amplitude | 2 (u16) | mVpp (range depends on waveform) | 2.5 Vpp = 2500 = `C4 09` |
| `54750f04-…` | phase | 2 (u16) | 0.01 degree, 0 to 35999 | 90° = 9000 = `28 23` |
| `54750f05-…` | DC offset | 2 (s16) | mV | -0.25 V = -250 = `06 FF` |
| `54750f06-…` | output | 1 (u8) | 0 off, 1 on | `01` |

Full UUIDs: `54750fXX-f75a-46c0-b78a-746996b46f1a`. Numbers are little-endian
(nRF Connect can also take them as plain numbers if you pick UINT16/UINT32 in the
write dialog). A wrong size or out-of-range value is refused with an ATT error
(0x0D wrong length, 0xFF out of range).

If a phone says it can't connect after the board's phones were forgotten, remove
the board from the phone's Bluetooth settings and pair again.

## Tests

Host tests run the real core, UI logic and vendored libraries on a PC against
fake Arduino/SPI/FreeRTOS headers (`test/fakes`). They check what would reach
the AD9833 and MCP4131, and render the dashboard with the real Adafruit GFX code
to BMP screenshots in `test/build/screens`. The HTTP server, Wi-Fi state machine
and setup routes run against a fake esp_http_server. The BLE value encoding and
range checks run against the real controller; the NimBLE stack itself isn't
faked. Needs g++ (on Windows:
`scoop install mingw`):

    bash test/run_tests.sh

VS Code IntelliSense: `python tools/gen_intellisense.py` writes
`.vscode/compile_commands.json` from the real ESP32-S3 build flags.
