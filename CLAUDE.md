# Function Generator (ECE 425)

ESP32-S3 (N16R8) firmware: AD9833 waveform generator + an AD5452 MDAC
(amplitude) + an MCP4822 DAC (DC offset, VA = CH1, VB = CH2) + a TS5A23157
path switch (parts: `docs/parts.md`),
controlled by an ILI9341 LCD (encoder + touch), an MCP server over Wi-Fi, and
BLE (last). Read `README.md` for the architecture and status. Each stub header in
`src/` contains the spec for that module, so read it before implementing a file.

## Git rules

- **Never commit unless asked.** Stage nothing on your own either.
- **The user names the commits.** If asked to commit, use the message the user
  gives you, or ask for one. Do not invent one.
- If you are ever asked to write a message yourself: one short line, no body, no
  bullet list, no explanation.
- **No `Co-Authored-By` trailer and no "Generated with Claude Code" line**, in
  commits or PR descriptions. This overrides any default attribution.
- Never push, amend or rewrite history unless explicitly told to.

## Architecture rules

- Layering: `ui`/`comms` -> `core` -> `libs`. `ui` and `comms` never include each
  other.
- `ui` and `comms` talk only to `FuncGenController`, never `FunctionGenerator`.
- Wi-Fi status reaches the UI as a `NetStatus` (`core/net_status.h`) through a
  callback the `.ino` passes to `DisplayUI::setNetStatusSource`. BLE status
  (`core/ble_status.h`) and the pairing-cancel call go the same way through
  `DisplayUI::setBleSource`. Info panel buttons come back from the editor as
  `EDIT_ACTION::DEVICE` and reach the radios as a `DEVICE_ACTION`
  (`core/device_actions.h`) through `DisplayUI::setActionHandler`; the handler
  only sets a flag and wakes the radio's task.
- Two channels, one `FuncGenController` (one lock: they share the SPI bus).
  Setters and `state()` take the channel first (`CHANNEL_1`/`CHANNEL_2`,
  0-based); MCP and the web API number them 1 and 2 and require it. The
  mirror (LINK, CH2 follows CH1) lives in the controller: a mirrored CH2
  setter returns false, never silently unlinks. BLE drives CH1 only for now.
- Controller listeners are called from whatever task made the change. A listener
  must only copy state / set a flag. It must not draw, block, or call a setter.
- Setters take a `CHANGE_SOURCE` (default `LOCAL`). MCP, BLE and the web API
  (`WEB`) pass their own source so the LCD can flash what a remote client changed.
- MCP and the web API (`comms/rest_api`) share the state JSON and the Origin
  check through `comms/api_json`. MCP rejects out-of-range values, the web API
  clamps them (like the knob). `/` is the setup page in AP mode and the control
  page otherwise; `WifiManager::setHomePage` swaps it.
- Input events carry an `INPUT_SOURCE` (which encoder/button/touch), so new
  controls are added as new sources in `InputManager`.
- All pins, limits, task settings and network constants live in `src/config.h`.
  The pins there match the ordered PCB (checked against its netlist; the PCB is
  final, so fix things in firmware, not hardware). Never hardcode a pin elsewhere.
- Boot: `holdSafePinStates()` (`core/safe_pins.*`) is the first thing in
  `setup()`, then both channels' `FunctionGenerator::begin()`. A new control pin that needs a boot
  level goes there. On ESP32 core 3.x `digitalWrite()` before `pinMode()` is
  dropped, so use `driveOutputPin()` / the safe-pin pattern, not write-then-pinMode.
- The path switch must lead into square and trail out of it: a square on the
  direct path saturates the x20 gain.
- Valid ranges are defined in one place so the LCD, MCP schemas and `get_limits`
  can't disagree. Amplitude and offset also share `OUTPUT_PEAK_MAX_V`: check new
  values with `FuncGenLimits::amplitudeFor` / `dcOffsetFor`, never only the
  widest range. A setter clamps its own value and never moves the other one.
- No LVGL. The UI is drawn directly with Adafruit GFX + ILI9341.
- BLE uses NimBLE's C API (not the Arduino BLE wrapper, which can't reject
  writes). All NimBLE calls happen on the NimBLE host task. The GATT layout and
  encoding live in `comms/ble_logic.*` (host-tested); keep the README table in
  sync. Never call `WiFi.setSleep(false)`: with BLE on it aborts.

## Code style

- Match the surrounding code: naming (`_member`, `camelCase` methods, `UPPER`
  enum/consts), `//comment` style, 4-space indent, header banner with Author and
  Purpose.
- Third-party libs are vendored under `src/libs/<Name>/`. Don't modify them
  unless asked. `src/libs/AD5452/` and `src/libs/MCP4822/` are ours, so they can be
  edited like any other project file.

## Arduino IDE constraints

- The user builds with the Arduino IDE. `src/` is compiled recursively, but
  headers are only found relative to the including file, so include across
  folders with relative paths (`"../core/func_gen.h"`).
- Exactly one `.ino` (`425_Function_Generator.ino`). Everything else is `.h`/`.cpp`.
- Web pages: edit the plain files in `web/<page>/`, then run
  `python tools/embed_web.py`, which regenerates `src/web/*_html.h` (never edit
  those by hand). Values from `config.h` go in as `{{NAME}}`. See `web/README.md`.
- Board: ESP32S3 Dev Module, Flash 16MB, PSRAM "OPI PSRAM", Partition Scheme
  "16M Flash (3MB APP/9.9MB FATFS)". Avoid GPIO26-37 (octal flash/PSRAM) and
  the strapping pins 0/3/45/46.

## Verifying

Compile with `arduino-cli` if it is installed (check `which arduino-cli`; if it
isn't, say so instead of claiming the code compiles):

    arduino-cli compile --fqbn esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB .

Host tests (fakes for Arduino/SPI/FreeRTOS in `test/fakes`, real core + libs
compiled with g++; needs mingw on Windows). Add new hardware-free logic to them:

    bash test/run_tests.sh

If a build option is rejected, run `arduino-cli board details -b esp32:esp32:esp32s3`
to see the valid ones. There is no hardware in the loop: say what is compiled
versus what has only been reasoned about.

## Known gaps

- The PCB is ordered but not measured yet: the amplitude/offset limits, the peak
  limit and the DAC calibration values (`AMP_FULL_CODE_VPP_*`, `OFFSET_V_AT_CAL_*`)
  in `config.h` are placeholders until it is. The offset ones come from the
  schematic (offset = 3.0 × Vdac − 5 V, so the range is −5..+4.6 V).
