# Pinout

What every pin on every part connects to. GPIO numbers come from
`src/config.h` and match the ordered PCB (checked against its netlist). If a
pin ever moves, change it in `config.h` first, then update this file.

Rules for the ESP32-S3 N16R8: don't use GPIO26-37 (octal flash/PSRAM) or the
strapping pins 0/3/45/46. The board breaks out 35/36/37, but they're PSRAM
pins on the N16R8, so leave them alone. Also skip GPIO38 and GPIO48: the RGB
LED is on GPIO48 on early DevKitC-1 boards and GPIO38 on v1.1, and clones vary.
Everything is 3.3 V logic; never put 5 V on a GPIO.

## ESP32-S3 GPIO summary

Sorted by GPIO. Two SPI buses: the **generator bus** (FSPI: each channel's
AD9833 and amplitude AD5452, plus one MCP4822 for both offsets) and the
**display bus** (HSPI: LCD and touch).

Both channels' chips are put into a known state at boot:
`holdSafePinStates()` (the first thing `setup()` does) deselects every
generator chip, opens both relays and puts both path switches on the divided
path, then each channel's `FunctionGenerator::begin()` resets it (AD9833 to
sine and asleep, amplitude 0, offset at the 0 V code, relay open). CH2's
AD9833 and AD5452 wire up exactly like CH1's (same tables below), except for the pins listed
here.

| GPIO | `config.h` name | Connects to | Notes |
|---|---|---|---|
| 1 | `PIN_ENC_A` | Encoder `CLK` | Internal pull-up |
| 2 | `PIN_ENC_B` | Encoder `DT` | Internal pull-up |
| 4 | `PIN_LCD_SCK` | LCD `SCK` **and** `T_CLK` | Display bus clock, one GPIO to two pins |
| 5 | `PIN_LCD_MOSI` | LCD `SDI(MOSI)` **and** `T_DIN` | Display bus data out, one GPIO to two pins |
| 6 | `PIN_LCD_MISO` | `T_DO` only | LCD `SDO(MISO)` stays unconnected |
| 7 | `PIN_LCD_CS` | LCD `CS` | |
| 8 | `PIN_TOUCH_IRQ` | `T_IRQ` | Internal pull-up |
| 9 | `PIN_AMP_DAC_CS` | AD5452 (amplitude) `SYNC` | 10 kΩ pull-up to 3.3 V, then held HIGH from the top of `setup()` |
| 10 | `PIN_AD9833_CS` | AD9833 `FSYNC` | The module's own pull-up (none on the board), then held HIGH from the top of `setup()` |
| 11 | `PIN_GEN_MOSI` | `SDATA` on both AD9833s, `SDIN` on both AD5452s, MCP4822 `SDI` | Generator bus data out |
| 12 | `PIN_GEN_SCK` | `SCLK` on both AD9833s and both AD5452s, MCP4822 `SCK` | Generator bus clock |
| 13 | `PIN_OUTPUT_RELAY_2` | CH2 relay driver: 1 kΩ to the 2N3904 base | 47 kΩ base pull-down to GND, then held LOW (open) from the top of `setup()`. HIGH = CH2's output connected (Was the generator bus MISO, which nothing used; the bus now runs without one) |
| 14 | `PIN_OFFSET_DAC_CS` | MCP4822 (offset, both channels) `CS` | 10 kΩ pull-up to 3.3 V, then held HIGH from the top of `setup()` |
| 15 | `PIN_LCD_DC` | LCD `DC` | |
| 16 | `PIN_LCD_RST` | LCD `RESET` | |
| 17 | `PIN_LCD_BL` | LCD `LED` | Backlight, driven HIGH |
| 18 | `PIN_TOUCH_CS` | `T_CS` | |
| 21 | `PIN_WAVE_SWITCH` | TS5A23157 `IN1` | The module's own pull resistor (none on the board). Driven HIGH (divided path) from the top of `setup()` until the AD9833 is reset to sine |
| 39 | `PIN_AD9833_CS_2` | CH2 AD9833 `FSYNC` | The module's own pull-up (none on the board), then held HIGH from the top of `setup()` |
| 40 | `PIN_AMP_DAC_CS_2` | CH2 AD5452 (amplitude) `SYNC` | 10 kΩ pull-up to 3.3 V, then held HIGH from the top of `setup()` |
| 41 | `PIN_OUTPUT_RELAY` | CH1 relay driver: 1 kΩ to the 2N3904 base | 47 kΩ base pull-down to GND (output disconnected at boot), then held LOW from the top of `setup()`. HIGH = output connected (Omron G5V-1 DC5 coil) |
| 42 | `PIN_ENC_SW` | Encoder `SW` | Internal pull-up. Held at power-on = forget Wi-Fi/phones |
| 47 | `PIN_WAVE_SWITCH_2` | TS5A23157 `IN2` | The module's own pull resistor (none on the board). HIGH (divided path) from the top of `setup()` until CH2's AD9833 is reset to sine, then LOW |
| 3V3 | | Every 3.3 V part's supply pin | See [Power](#power) |
| GND | | Every part's ground | One common ground with the analog supply |

## ILI9341 display + XPT2046 touch module

14-pin header, top to bottom as printed on the board.

| Module pin | Connects to | Notes |
|---|---|---|
| `T_IRQ` | GPIO8 | Low while touched |
| `T_DO` | GPIO6 | Touch data back to the ESP32 |
| `T_DIN` | GPIO5 | Same GPIO as `SDI(MOSI)` |
| `T_CS` | GPIO18 | |
| `T_CLK` | GPIO4 | Same GPIO as `SCK` |
| `SDO(MISO)` | **Unconnected** | Can hold the MISO line and corrupt touch reads; the LCD is never read |
| `LED` | GPIO17 | If your module has no backlight transistor (Q1), tie it to 3.3 V instead |
| `SCK` | GPIO4 | |
| `SDI(MOSI)` | GPIO5 | |
| `DC` | GPIO15 | |
| `RESET` | GPIO16 | |
| `CS` | GPIO7 | |
| `GND` | GND | |
| `VCC` | 3.3 V | Or 5 V. On 3.3 V only, you can short jumper `J1` to bypass the on-board regulator |

The touch pins are wired separately from the LCD pins on the module, so both
pins of each shared pair need their own wire: `SCK` and `T_CLK`, `SDI` and
`T_DIN`.

## Rotary encoder (5-pin breakout)

Order as printed on the board (shown rotated 180°: `CLK` at the bottom).

| Module pin | Connects to | Notes |
|---|---|---|
| `GND` | GND | |
| `+` | 3.3 V | Powers the module's pull-ups. Never 5 V: those pull-ups would put 5 V on the GPIOs |
| `SW` | GPIO42 | Push button, active low |
| `DT` | GPIO2 | Channel B |
| `CLK` | GPIO1 | Channel A |

If turning the knob clockwise lowers the value, swap the `CLK` and `DT` wires
(or swap `PIN_ENC_A` / `PIN_ENC_B` in `config.h`).

## AD9833 DDS module

7-pin header, top to bottom as printed on the board. The table is CH1's
module; CH2's is wired the same except `FSYNC` goes to GPIO39 and `OUT` goes
to `COM2` of the switch.

| Module pin | Connects to | Notes |
|---|---|---|
| `VCC` | 3.3 V | 3.3 V so the square is 3.3 Vpp and the logic matches the ESP32 |
| `DGND` | GND | |
| `SDATA` | GPIO11 | Generator bus MOSI |
| `SCLK` | GPIO12 | Generator bus clock |
| `FSYNC` | GPIO10 | Chip select |
| `AGND` | GND | |
| `OUT` | TS5A23157 `COM1` | The switch sends it down the direct or the divided path; see [TS5A23157](#ts5a23157-analog-switch-module) |

## AD5452 MDAC (amplitude, MSOP-8, on an adapter board)

The table is CH1's chip; CH2's is wired the same way, with `SYNC` on GPIO40
(10 kΩ pull-up to 3.3 V). Pin numbers are the MSOP-8 package (AD5452YRMZ),
which is what the adapter's DIP pins follow.

| Pin | Name | Connects to |
|---|---|---|
| 1 | `IOUT1` | OPA2810 I/V op-amp inverting (−) input |
| 2 | `GND` | GND (also the I/V op-amp's + input) |
| 3 | `SCLK` | GPIO12 |
| 4 | `SDIN` | GPIO11 |
| 5 | `SYNC` | GPIO9 + 10 kΩ pull-up to 3.3 V |
| 6 | `VDD` | 3.3 V + 100 nF to GND at the chip |
| 7 | `VREF` | Gain 2 output (OPA2810 B, ±6 V max) |
| 8 | `RFB` | OPA2810 I/V op-amp output (1–2 pF to `IOUT1` if it rings) |

The AD5452 has no data-out pin, so nothing connects to the generator bus MISO.

## MCP4822 DAC (DC offset for both channels, SOIC-8)

One chip for both channels. Pin numbers are the SOIC-8 package (MCP4822-E/SN).

| Pin | Name | Connects to |
|---|---|---|
| 1 | `VDD` | 3.3 V + 100 nF to GND at the chip |
| 2 | `CS` | GPIO14 + 10 kΩ pull-up to 3.3 V |
| 3 | `SCK` | GPIO12 |
| 4 | `SDI` | GPIO11 |
| 5 | `LDAC` | GND (output updates when `CS` goes high) |
| 6 | `VOUTB` | CH2 offset input to the summer |
| 7 | `VSS` | GND |
| 8 | `VOUTA` | CH1 offset input to the summer |

The MCP4822 has no data-out pin either.

## TS5A23157 analog switch module

Dual SPDT, VSSOP-10 (DGS) on a breakout module. Each channel has its own
control pin: `INx` low connects `COMx` to `NCx`; `INx` high connects `COMx` to
`NOx`. Channel 1 is CH1's path switch, channel 2 is CH2's.

Pin numbers are the chip's (TI DGS package). The module's header labels and
order may differ, so match by name on the silkscreen. If the module has no
decoupling cap, add the 100 nF yourself.

| Pin | Name | Connects to | Notes |
|---|---|---|---|
| 1 | `IN1` | GPIO21 | The module's own pull resistor until `setup()`, then driven by the firmware |
| 2 | `NO1` | CH1 square divider top (39.2 kΩ to gain 1's + input, 10 kΩ from there to GND: 0.67 V) | Selected when `IN1` is HIGH (square) |
| 3 | `GND` | GND | |
| 4 | `NO2` | CH2 square divider top | Selected when `IN2` is HIGH (square) |
| 5 | `IN2` | GPIO47 | Same as `IN1`, for CH2 |
| 6 | `COM2` | CH2 AD9833 `OUT` | |
| 7 | `NC2` | CH2 gain 1 + input (direct path) | Selected when `IN2` is LOW (sine/triangle) |
| 8 | `V+` | 3.3 V + 100 nF to GND at the chip | |
| 9 | `NC1` | CH1 gain 1 + input (OPA2810 A), direct sine/triangle path | Selected when `IN1` is LOW (sine/triangle) |
| 10 | `COM1` | CH1 AD9833 `OUT` | |

## Power

From the ordered PCB. A 12 V adapter comes in on the barrel jack (`J3`,
CUI PJ-102AH) through the on/off switch (`SW2`) onto the `+12C` net.

| Rail | Source | Feeds |
|---|---|---|
| +12 V in (`+12C`) | `J3` through `SW2` | TMR 6-1222 input, LM7805 input |
| ±12 V | TMR 6-1222 isolated DC-DC (`U2`) | OPA2810 × 4: two per channel (must stay at or below ±13.5 V) |
| +5 V | LM7805 (`U3`) | ESP32 board `5V` pin, both relay coils, the summers' −5 V shift (R12 / R14), AP2112K input |
| 3.3 V | AP2112K-3.3 (`U16`), tied to the ESP32 board's `3V3` pins through `R31` (0 Ω) | Display `VCC`, encoder `+`, both AD9833 `VCC`, both AD5452 `VDD`, MCP4822 `VDD`, TS5A23157 `V+`, the SYNC/CS pull-ups |
| GND | common | Every ground, analog and digital |

The −5 V term in each summer comes straight from the +5 V rail, so +5 V drift
moves the DC offset 1:1. Calibrate the offset on the real board.

## Sources

- `src/config.h` for every GPIO number.
- AD5450/AD5451/AD5452/AD5453 datasheet (Analog Devices), MSOP-8 pin configuration.
- MCP4802/4812/4822 datasheet DS20002249 (Microchip), SOIC-8 pin configuration.
- TS5A23157 datasheet SCDS165 (TI), DGS pin configuration and function table.
- Module silkscreens for the display, AD9833 and encoder headers.
