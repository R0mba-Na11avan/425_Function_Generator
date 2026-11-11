/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: host-test stand-in for the Arduino core. only what the vendored
             AD9833/AD5452 libs and src/core need. pin writes are recorded
             so the fake SPI can tell which chip select is active
**************************************************************************/

#ifndef FAKE_ARDUINO_H
#define FAKE_ARDUINO_H

#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

typedef uint8_t byte;
typedef bool boolean;

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

#define LSBFIRST 0
#define MSBFIRST 1

#define B00 0
#define B01 1
#define B10 2
#define B11 3

#define RAD_TO_DEG 57.295779513082320876798154814105
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define radians(deg) ((deg) * DEG_TO_RAD)

#define F(s) (s)
#define PROGMEM

//just enough for Adafruit_GFX's String / flash-string overloads
class __FlashStringHelper;

class String {
    public:
        String(const char* s = "") : _s(s) {}
        const char* c_str() const { return _s; }
        unsigned int length() const { return (unsigned int)strlen(_s); }

    private:
        const char* _s;
};

static const int FAKE_PIN_COUNT = 64;

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
int digitalRead(uint8_t pin);

//test inspection
int fakePinMode(uint8_t pin);
int fakePinLevel(uint8_t pin);
void fakeResetPins();

#endif
