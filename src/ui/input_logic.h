/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: the hardware-free half of the input handling: encoder
             acceleration, button debounce + long press, tap detection and
             touch calibration. InputManager feeds these with raw readings.
             kept separate so the host tests can run them without an ESP32
**************************************************************************/

#ifndef INPUT_LOGIC_H
#define INPUT_LOGIC_H

#include <stdint.h>
#include "../config.h"

//what InputManager delivers to DisplayUI
enum class INPUT_EVENT : uint8_t {
    ENCODER_DELTA,
    BUTTON_SHORT,
    BUTTON_LONG,
    TOUCH_TAP
};

//which physical control an event came from. there is one of each today, a
//second encoder or dedicated buttons would get their own ids
enum class INPUT_SOURCE : uint8_t {
    MAIN_ENCODER,   //the rotary encoder and its push button
    TOUCH
};

struct InputEvent {
    INPUT_EVENT type;
    INPUT_SOURCE source;
    int16_t clicks; //ENCODER_DELTA: clicks turned (use to move a cursor)
    int16_t delta;  //ENCODER_DELTA: accelerated steps (use to change a value)
    int16_t x;      //TOUCH_TAP: screen pixel
    int16_t y;
};

//turns encoder clicks into steps, multiplied when the knob is spun quickly
class EncoderAccel {
    public:
        //clicks turned since the last call (signed). returns accelerated steps
        int32_t apply(int32_t clicks, uint32_t nowMs);

    private:
        uint32_t _lastMs = 0;
        int8_t _lastDir = 0; //0 = no movement yet
};

enum class BUTTON_EVENT : uint8_t {
    NONE,
    SHORT,  //released before BUTTON_LONG_MS
    LONG    //held past BUTTON_LONG_MS, fires once while still held
};

class ButtonDebouncer {
    public:
        //pressed = raw (bouncy) button level, sampled every few ms
        BUTTON_EVENT update(bool pressed, uint32_t nowMs);

    private:
        bool _raw = false;
        bool _stable = false;
        bool _longSent = false;
        uint32_t _rawSince = 0;
        uint32_t _pressedAt = 0;
};

enum class HOLD_STATE : uint8_t {
    HOLDING,    //still pressed, not long enough yet
    RELEASED,   //let go before the time was up
    DONE        //held for the whole time
};

//times a press that has to be held for holdMs (the boot-time Wi-Fi reset).
//a bounce shorter than BUTTON_DEBOUNCE_MS doesn't count as letting go
class HoldTimer {
    public:
        HoldTimer(uint32_t holdMs, uint32_t startMs);
        HOLD_STATE update(bool pressed, uint32_t nowMs);
        uint32_t remainingMs(uint32_t nowMs) const;

    private:
        uint32_t _holdMs;
        uint32_t _startMs;
        bool _up = false;
        uint32_t _upSince = 0;
};

struct TouchPoint {
    int16_t x;
    int16_t y;
};

struct TouchCalibration {
    int16_t rawXMin, rawXMax; //raw readings at the left and right edge
    int16_t rawYMin, rawYMax; //raw readings at the top and bottom edge
    bool swapXY;
    int16_t width, height;    //screen size in pixels
};

//calibration from config.h
TouchCalibration defaultTouchCalibration();

//raw XPT2046 reading -> screen pixel, clamped to the screen
TouchPoint mapTouch(int16_t rawX, int16_t rawY, const TouchCalibration& cal);

//finds taps in a stream of touch samples: press and release quickly without moving
class TapDetector {
    public:
        //returns true (and sets tap) once, when a tap ends
        bool update(bool touched, TouchPoint point, uint32_t nowMs, TouchPoint& tap);

    private:
        bool _down = false;
        bool _moved = false;
        TouchPoint _start = {0, 0};
        uint32_t _downAt = 0;
        uint32_t _lastTouchMs = 0;
};

#endif
