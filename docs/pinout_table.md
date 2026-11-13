# Pinout

What every pin on every part connects to. GPIO numbers come from
`src/config.h`. They are still placeholders until the wiring is confirmed, so
if a pin moves, change it in `config.h` first, then update this file.

Rules for the ESP32-S3 N16R8: don't use GPIO26-37 (octal flash/PSRAM) or the
strapping pins 0/3/45/46. The board breaks out 35/36/37, but they're PSRAM
pins on the N16R8, so leave them alone. Also skip GPIO38 and GPIO48: the RGB
LED is on GPIO48 on early DevKitC-1 boards and GPIO38 on v1.1, and clones vary.
Everything is 3.3 V logic; never put 5 V on a GPIO.

## ESP32-S3 GPIO summary

Sorted by GPIO. Two SPI buses: the **generator bus** (FSPI: each channel's
AD9833 and two AD5452s) and the **display bus** (HSPI: LCD and touch).

CH2 rows are reserved: the pins are fixed in `config.h`, but the firmware
doesn't drive them until CH2 is added. Their pull-ups and pull-downs keep the
CH2 chips deselected and on the sine path until then. CH2's AD9833 and AD5452s
wire up exactly like CH1's (same tables below), except for the pins listed
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
| 9 | `PIN_AMP_DAC_CS` | AD5452 #1 (amplitude) `SYNC` | 10 kΩ pull-up to 3.3 V |
| 10 | `PIN_AD9833_CS` | AD9833 `FSYNC` | |
| 11 | `PIN_GEN_MOSI` | `SDATA` on both AD9833s, `SDIN` on all four AD5452s | Generator bus data out |
| 12 | `PIN_GEN_SCK` | `SCLK` on both AD9833s and all four AD5452s | Generator bus clock |
| 13 | `PIN_GEN_MISO` | Nothing | Reserved by the SPI bus; none of the chips send data back |
| 14 | `PIN_OFFSET_DAC_CS` | AD5452 #2 (offset) `SYNC` | 10 kΩ pull-up to 3.3 V |
| 15 | `PIN_LCD_DC` | LCD `DC` | |
| 16 | `PIN_LCD_RST` | LCD `RESET` | |
| 17 | `PIN_LCD_BL` | LCD `LED` | Backlight, driven HIGH |
| 18 | `PIN_TOUCH_CS` | `T_CS` | |
| 21 | `PIN_WAVE_SWITCH` | TS5A23157 `IN1` | 10 kΩ pull-down to GND (sine path at boot) |
| 39 | `PIN_AD9833_CS_2` | CH2 AD9833 `FSYNC` | 10 kΩ pull-up to 3.3 V. Reserved for CH2, not driven yet |
| 40 | `PIN_AMP_DAC_CS_2` | CH2 AD5452 (amplitude) `SYNC` | 10 kΩ pull-up to 3.3 V. Reserved for CH2, not driven yet |
| 41 | `PIN_OFFSET_DAC_CS_2` | CH2 AD5452 (offset) `SYNC` | 10 kΩ pull-up to 3.3 V. Reserved for CH2, not driven yet |
| 42 | `PIN_ENC_SW` | Encoder `SW` | Internal pull-up. Held at power-on = forget Wi-Fi/phones |
| 47 | `PIN_WAVE_SWITCH_2` | TS5A23157 `IN2` | 10 kΩ pull-down to GND. Reserved for CH2, not driven yet |
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
module; CH2's is wired the same except `FSYNC` goes to GPIO39 (with a 10 kΩ
pull-up to 3.3 V) and `OUT` feeds CH2's own low-pass and divider.

| Module pin | Connects to | Notes |
|---|---|---|
| `VCC` | 3.3 V | 3.3 V so the square is 3.3 Vpp and the logic matches the ESP32 |
| `DGND` | GND | |
| `SDATA` | GPIO11 | Generator bus MOSI |
| `SCLK` | GPIO12 | Generator bus clock |
| `FSYNC` | GPIO10 | Chip select |
| `AGND` | GND | |
| `OUT` | RC low-pass input **and** square divider input | Both paths come off this one pin; see [TS5A23157](#ts5a23157-analog-switch-module) |

## AD5452 MDAC × 2 (MSOP-8, on adapter boards)

Same pinout for both chips; only `SYNC` and the analog pins differ. The table
is CH1's pair; CH2 has its own pair wired the same way, with `SYNC` on GPIO40
(amplitude) and GPIO41 (offset), each with a 10 kΩ pull-up to 3.3 V.
Pin numbers are the MSOP-8 package (AD5452YRMZ), which is what the adapter's
DIP pins follow.

| Pin | Name | #1 amplitude connects to | #2 offset connects to |
|---|---|---|---|
| 1 | `IOUT1` | OPA2810 I/V op-amp inverting (−) input | Offset OPA2810 I/V op-amp inverting (−) input (half A for CH1, B for CH2) |
| 2 | `GND` | GND (also the I/V op-amp's + input) | GND (also the I/V op-amp's + input) |
| 3 | `SCLK` | GPIO12 | GPIO12 |
| 4 | `SDIN` | GPIO11 | GPIO11 |
| 5 | `SYNC` | GPIO9 + 10 kΩ pull-up to 3.3 V | GPIO14 + 10 kΩ pull-up to 3.3 V |
| 6 | `VDD` | 3.3 V + 100 nF to GND at the chip | 3.3 V + 100 nF to GND at the chip |
| 7 | `VREF` | Gain 2 output (OPA2810 B, ±6 V max) | Offset reference (voltage not chosen yet) |
| 8 | `RFB` | OPA2810 I/V op-amp output (1–2 pF to `IOUT1` if it rings) | Offset OPA2810 I/V op-amp output |

The AD5452 has no data-out pin, so nothing connects to the generator bus MISO.

## TS5A23157 analog switch module

Dual SPDT, VSSOP-10 (DGS) on a breakout module. Each channel has its own
control pin: `INx` low connects `COMx` to `NCx`; `INx` high connects `COMx` to
`NOx`. Channel 1 is CH1's path switch; channel 2 is reserved for CH2.

Pin numbers are the chip's (TI DGS package). The module's header labels and
order may differ, so match by name on the silkscreen. If the module has no
decoupling cap, add the 100 nF yourself.

| Pin | Name | Connects to | Notes |
|---|---|---|---|
| 1 | `IN1` | GPIO21 + 10 kΩ pull-down to GND | Pull-down keeps the sine path during boot |
| 2 | `NO1` | CH1 square divider output (~0.6 V) | Selected when `IN1` is HIGH (square) |
| 3 | `GND` | GND | |
| 4 | `NO2` | CH2 square divider output | Reserved for CH2 |
| 5 | `IN2` | GPIO47 + 10 kΩ pull-down to GND | Reserved for CH2, not driven yet |
| 6 | `COM2` | CH2 gain 1 input | Reserved for CH2 |
| 7 | `NC2` | CH2 RC low-pass output | Reserved for CH2 |
| 8 | `V+` | 3.3 V + 100 nF to GND at the chip | |
| 9 | `NC1` | CH1 RC low-pass output (sine/triangle) | Selected when `IN1` is LOW (default) |
| 10 | `COM1` | CH1 gain 1 input (OPA2810 A) | |

## Power

Only the 3.3 V side is decided. The ±12 V analog rails are still open (see
`docs/parts.md`, Open items), so the op-amps aren't listed here yet.

| Rail | Source | Feeds |
|---|---|---|
| 3.3 V | ESP32 board `3V3` pin | Display `VCC`, encoder `+`, both AD9833 `VCC`, all four AD5452 `VDD`, TS5A23157 `V+`, the SYNC/FSYNC pull-ups |
| GND | ESP32 board `GND` | Every ground above, tied to the analog supply's ground |
| ±12 V | TBD | OPA2810 × 5: two per channel plus the shared offset chip (must stay at or below ±13.5 V) |

## Sources

- `src/config.h` for every GPIO number.
- AD5450/AD5451/AD5452/AD5453 datasheet (Analog Devices), MSOP-8 pin configuration.
- TS5A23157 datasheet SCDS165 (TI), DGS pin configuration and function table.
- Module silkscreens for the display, AD9833 and encoder headers.
