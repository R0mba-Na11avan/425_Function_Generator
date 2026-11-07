# Function Generator (ECE 425)

ESP32-S3 (N16R8) firmware: AD9833 waveform generator + MCP4131 amplitude pot,
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
- Controller listeners are called from whatever task made the change. A listener
  must only copy state / set a flag. It must not draw, block, or call a setter.
- All pins, limits, task settings and network constants live in `src/config.h`.
  The pins there are temporary placeholders until the wiring is confirmed. Never
  hardcode a pin elsewhere.
- Valid ranges are defined in one place so the LCD, MCP schemas and `get_limits`
  can't disagree.
- No LVGL. The UI is drawn directly with Adafruit GFX + ILI9341.
- BLE is implemented last and is not needed for anything else.

## Code style

- Match the surrounding code: naming (`_member`, `camelCase` methods, `UPPER`
  enum/consts), `//comment` style, 4-space indent, header banner with Author and
  Purpose.
- Third-party libs are vendored under `src/libs/<Name>/`. Don't modify them
  unless asked.

## Arduino IDE constraints

- The user builds with the Arduino IDE. `src/` is compiled recursively, but
  headers are only found relative to the including file, so include across
  folders with relative paths (`"../core/func_gen.h"`).
- Exactly one `.ino` (`425_Function_Generator.ino`). Everything else is `.h`/`.cpp`.
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

- `FunctionGenerator::ampToWipe()` is a linear placeholder, and the amplitude and
  DC offset limits in `config.h` are placeholders until the output stage is
  measured.
- DC offset is stored but not applied to hardware.
