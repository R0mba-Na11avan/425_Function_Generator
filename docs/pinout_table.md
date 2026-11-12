# Pinout

What every pin on every part connects to. GPIO numbers come from
`src/config.h`. They are still placeholders until the wiring is confirmed, so
if a pin moves, change it in `config.h` first, then update this file.

Rules for the ESP32-S3 N16R8: don't use GPIO26-37 (octal flash/PSRAM) or the
strapping pins 0/3/45/46. Everything is 3.3 V logic; never put 5 V on a GPIO.

## ESP32-S3 GPIO summary

Sorted by GPIO. Two SPI buses: the **generator bus** (FSPI: AD9833 and both
AD5452s) and the **display bus** (HSPI: LCD and touch).

| GPIO | `config.h` name | Connects to | Notes |
|---|---|---|---|
| 1 | `PIN_ENC_A` | Encoder `CLK` | Internal pull-up |
| 2 | `PIN_ENC_B` | Encoder `DT` | Internal pull-up |
| 4 | `PIN_LCD_SCK` | LCD `SCK` **and** `T_CLK` | Display bus clock, one GPIO to two pins |
| 5 | `PIN_LCD_MOSI` | LCD `SDI(MOSI)` **and** `T_DIN` | Display bus data out, one GPIO to two pins |
| 6 | `PIN_LCD_MISO` | `T_DO` only | LCD `SDO(MISO)` stays unconnected |
| 7 | `PIN_LCD_CS` | LCD `CS` | |
| 8 | `PIN_TOUCH_IRQ` | `T_IRQ` | Internal pull-up |
| 9 | `PIN_AMP_DAC_CS` | AD5452 #1 (amplitude) `SYNC` | 10 kΩ pull-up to 3.3 V |
| 10 | `PIN_AD9833_CS` | AD9833 `FSYNC` | |
| 11 | `PIN_GEN_MOSI` | AD9833 `SDATA`, AD5452 #1 `SDIN`, AD5452 #2 `SDIN` | Generator bus data out |
| 12 | `PIN_GEN_SCK` | AD9833 `SCLK`, AD5452 #1 `SCLK`, AD5452 #2 `SCLK` | Generator bus clock |
| 13 | `PIN_GEN_MISO` | Nothing | Reserved by the SPI bus; none of the chips send data back |
| 14 | `PIN_OFFSET_DAC_CS` | AD5452 #2 (offset) `SYNC` | 10 kΩ pull-up to 3.3 V |
| 15 | `PIN_LCD_DC` | LCD `DC` | |
| 16 | `PIN_LCD_RST` | LCD `RESET` | |
| 17 | `PIN_LCD_BL` | LCD `LED` | Backlight, driven HIGH |
| 18 | `PIN_TOUCH_CS` | `T_CS` | |
| 21 | `PIN_WAVE_SWITCH` | TS5A3159A `IN` | 10 kΩ pull-down to GND (sine path at boot) |
| 42 | `PIN_ENC_SW` | Encoder `SW` | Internal pull-up. Held at power-on = forget Wi-Fi/phones |
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

7-pin header, top to bottom as printed on the board.

| Module pin | Connects to | Notes |
|---|---|---|
| `VCC` | 3.3 V | 3.3 V so the square is 3.3 Vpp and the logic matches the ESP32 |
| `DGND` | GND | |
| `SDATA` | GPIO11 | Generator bus MOSI |
| `SCLK` | GPIO12 | Generator bus clock |
| `FSYNC` | GPIO10 | Chip select |
| `AGND` | GND | |
| `OUT` | RC low-pass input **and** square divider input | Both paths come off this one pin; see [TS5A3159A](#ts5a3159a-analog-switch) |

## AD5452 MDAC × 2 (MSOP-8, on adapter boards)

Same pinout for both chips; only `SYNC` and the analog pins differ.
Pin numbers are the MSOP-8 package (AD5452YRMZ), which is what the adapter's
DIP pins follow.

| Pin | Name | #1 amplitude connects to | #2 offset connects to |
|---|---|---|---|
| 1 | `IOUT1` | OPA2810 I/V op-amp inverting (−) input | TL071 I/V op-amp inverting (−) input |
| 2 | `GND` | GND (also the I/V op-amp's + input) | GND (also the I/V op-amp's + input) |
| 3 | `SCLK` | GPIO12 | GPIO12 |
| 4 | `SDIN` | GPIO11 | GPIO11 |
| 5 | `SYNC` | GPIO9 + 10 kΩ pull-up to 3.3 V | GPIO14 + 10 kΩ pull-up to 3.3 V |
| 6 | `VDD` | 3.3 V + 100 nF to GND at the chip | 3.3 V + 100 nF to GND at the chip |
| 7 | `VREF` | Gain 2 output (OPA2810 B, ±6 V max) | Offset reference (voltage not chosen yet) |
| 8 | `RFB` | OPA2810 I/V op-amp output (1–2 pF to `IOUT1` if it rings) | TL071 I/V op-amp output |

The AD5452 has no data-out pin, so nothing connects to the generator bus MISO.

## TS5A3159A analog switch

SOT-23-6 (DBV) on an adapter board. IN low connects COM to NC; IN high connects
COM to NO.

| Pin | Name | Connects to | Notes |
|---|---|---|---|
| 1 | `NO` | Square divider output (~0.6 V) | Selected when `IN` is HIGH (square) |
| 2 | `GND` | GND | |
| 3 | `NC` | RC low-pass output (sine/triangle) | Selected when `IN` is LOW (default) |
| 4 | `COM` | Gain 1 input (OPA2810 A) | |
| 5 | `V+` | 3.3 V + 100 nF to GND at the chip | |
| 6 | `IN` | GPIO21 + 10 kΩ pull-down to GND | Pull-down keeps the sine path during boot |

## Power

Only the 3.3 V side is decided. The ±12 V analog rails are still open (see
`docs/parts.md`, Open items), so the op-amps aren't listed here yet.

| Rail | Source | Feeds |
|---|---|---|
| 3.3 V | ESP32 board `3V3` pin | Display `VCC`, encoder `+`, AD9833 `VCC`, both AD5452 `VDD`, TS5A3159A `V+`, the SYNC pull-ups |
| GND | ESP32 board `GND` | Every ground above, tied to the analog supply's ground |
| ±12 V | TBD | OPA2810 × 2, TL071 (must stay at or below ±13.5 V) |

## Sources

- `src/config.h` for every GPIO number.
- AD5450/AD5451/AD5452/AD5453 datasheet (Analog Devices), MSOP-8 pin configuration.
- TS5A3159A datasheet SCDS200F (TI), DBV pin configuration and function table.
- Module silkscreens for the display, AD9833 and encoder headers.
