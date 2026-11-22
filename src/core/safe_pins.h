/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: drives the generator's control pins to their safe levels as
             the first thing in setup(), and drives an output pin without
             passing through the wrong level
**************************************************************************/

/*
    The firmware holds these pins from the top of setup(). Before that only
    the module and board resistors do: the AD9833 and TS5A23157 modules have
    their own pull resistors, the AD5452 SYNCs and the MCP4822 CS have 10k
    pull-ups on the board, and each relay driver has a 47k base pull-down.

    ESP32 core 3.x drops digitalWrite() on a pin that pinMode() hasn't
    claimed yet ("IO n is not set as GPIO"), and pinMode(OUTPUT) on its own
    drives the output latch's reset level (LOW). So writing the level and
    then calling pinMode(OUTPUT) does not preset it. holdSafePinStates()
    claims each pin as an input first (drives nothing), then sets the
    level, then turns the driver on. Once a pin is claimed, the drivers'
    own begin() calls (AD9833, AD5452, MCP4822) leave it idle high without
    a LOW blip.

    Safe levels:
        PIN_OUTPUT_RELAY, PIN_OUTPUT_RELAY_2    LOW: both relays open, both
                                                BNCs disconnected. a relay
                                                only closes on setOutput(true)
        PIN_AD9833_CS, PIN_AMP_DAC_CS,          HIGH: every generator chip
        PIN_OFFSET_DAC_CS, PIN_AD9833_CS_2,     deselected until its driver
        PIN_AMP_DAC_CS_2                        talks to it
        PIN_WAVE_SWITCH, PIN_WAVE_SWITCH_2      HIGH: the divided square path.
                                                an AD9833's registers are
                                                undefined until it is reset,
                                                and the divided path is safe
                                                for any waveform (0.67 V at
                                                most). the direct path is only
                                                safe for sine/triangle. each
                                                channel's FunctionGenerator::
                                                begin() moves its switch to the
                                                direct path once its AD9833 is
                                                reset to sine

    setup() then runs begin() on both channels' FunctionGenerators right
    away (AD9833 reset to sine and asleep, amplitude 0, offset 0 V, relay
    open).
*/

#ifndef SAFE_PINS_H
#define SAFE_PINS_H

#include <Arduino.h>

//pinMode(OUTPUT) at level. a pin that is already a GPIO (holdSafePinStates()
//claimed it) never drives the other level. one that isn't yet drives LOW
//for a moment first, because core 3.x drops the first write
void driveOutputPin(uint8_t pin, uint8_t level);

//first thing in setup(), before anything that can take time. every pin in
//the table above goes straight to its safe level
void holdSafePinStates();

#endif
