# Codebase for the Function Generator for 425

this project is the codebase for the function generator 

oui oui baugette

## Layout

```
425_Function_Generator.ino   setup()/loop() only
src/
  libs/      vendored third-party libs (AD9833, MCP4131, Adafruit GFX + ILI9341)
  core/      FunctionGenerator, FuncGenState, FuncGenController
  ui/        LCD UI (planned)
  comms/     BLE, Wi-Fi, HTTP, MCP server (planned)
```

Dependencies only point downward: `ui`/`comms` -> `core` -> `libs`.
`ui` and `comms` only talk to `FuncGenController`, never to `FunctionGenerator`.

Arduino IDE compiles `src/` recursively but only searches `src/` roots for
headers, so include across folders with relative paths (`"../core/..."`).

## Build (Arduino IDE)

Board: ESP32S3 Dev Module, Flash 16MB, PSRAM "OPI PSRAM".
