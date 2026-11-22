#include "safe_pins.h"
#include "../config.h"

void driveOutputPin(uint8_t pin, uint8_t level) {
    digitalWrite(pin, level);   //lands only if the pin is already a GPIO
    pinMode(pin, OUTPUT);
    digitalWrite(pin, level);   //lands either way
}

//a pin nothing has configured since reset: claimed as an input first, so the
//level is in the output latch before the pin starts driving
static void claimAndDrive(uint8_t pin, uint8_t level) {
    pinMode(pin, INPUT);
    driveOutputPin(pin, level);
}

void holdSafePinStates() {
    //BNCs disconnected first
    claimAndDrive(PIN_OUTPUT_RELAY, LOW);
    claimAndDrive(PIN_OUTPUT_RELAY_2, LOW);

    //every generator chip deselected before anything talks on the shared bus.
    //the AD9833 modules' FSYNC pull-ups are their own, the rest are on the board
    claimAndDrive(PIN_AD9833_CS, HIGH);
    claimAndDrive(PIN_AMP_DAC_CS, HIGH);
    claimAndDrive(PIN_OFFSET_DAC_CS, HIGH);
    claimAndDrive(PIN_AD9833_CS_2, HIGH);
    claimAndDrive(PIN_AMP_DAC_CS_2, HIGH);

    //divided path until each AD9833 is known to be on sine
    claimAndDrive(PIN_WAVE_SWITCH, HIGH);
    claimAndDrive(PIN_WAVE_SWITCH_2, HIGH);
}
