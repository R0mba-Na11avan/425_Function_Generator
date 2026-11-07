/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: reads the rotary encoder, its button and the XPT2046 touch
             controller and turns them into a stream of input events
**************************************************************************/

/*
    This class only produces events. It does not know what a field or a
    frequency is. DisplayUI decides what an event means, so adding buttons
    later should only mean adding another event source here.

    How it works
        - one input task (config.h: INPUT_TASK_*) runs every INPUT_POLL_MS
        - encoder: the PCNT peripheral decodes the quadrature in hardware with
          a glitch filter, so no edges are missed even while the task waits.
          the task just reads the count. EncoderAccel multiplies fast turns
        - button: sampled each tick, debounced, short vs long press
          (ButtonDebouncer). LONG fires while still held, SHORT on release
        - touch: XPT2046_Touchscreen sets a flag from the T_IRQ interrupt, so
          the SPI bus is only read while the screen is pressed. readings are
          mapped to pixels with the calibration in config.h (mapTouch) and
          TapDetector turns press + release into one TOUCH_TAP
        - the touch chip shares the display's SPI bus. both libraries wrap
          every access in an SPI transaction, and the ESP32 SPIClass holds a
          lock for the length of a transaction, so a touch read never cuts
          into a redraw

    Delivery
        - FreeRTOS queue of INPUT_QUEUE_LEN events, DisplayUI is the only
          consumer
        - encoder clicks are summed and only queued while more than
          INPUT_QUEUE_RESERVED slots are free, so they never crowd out a button
          or tap. clicks aren't lost meanwhile, they wait in the sum (and in
          the PCNT counter)
        - button/touch events wait for a free slot, they are never dropped
*/

#ifndef INPUT_H
#define INPUT_H

#include <Arduino.h>
#include <SPI.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/pulse_cnt.h"

#include "../config.h"
#include "../libs/XPT2046_Touchscreen/XPT2046_Touchscreen.h"
#include "input_logic.h"

enum class INPUT_EVENT : uint8_t {
    ENCODER_DELTA,
    BUTTON_SHORT,
    BUTTON_LONG,
    TOUCH_TAP
};

struct InputEvent {
    INPUT_EVENT type;
    int16_t clicks; //ENCODER_DELTA: clicks turned (use to move a cursor)
    int16_t delta;  //ENCODER_DELTA: accelerated steps (use to change a value)
    int16_t x;      //TOUCH_TAP: screen pixel
    int16_t y;
};

class InputManager {
    public:
        //touchBus is the display's SPI bus (HSPI). begin() starts it on the
        //PIN_LCD_* pins if nobody has yet
        InputManager(SPIClass& touchBus);

        //configures pins, PCNT and touch, creates the queue and starts the
        //input task. returns false if any of that failed
        bool begin();

        //waits up to timeout for the next event
        bool receive(InputEvent& event, TickType_t timeout);

    private:
        static void taskEntry(void* arg);
        void run();

        bool setupEncoder();
        void pollEncoder(uint32_t nowMs);
        void pollButton(uint32_t nowMs);
        void pollTouch(uint32_t nowMs);

        //queues the summed encoder clicks if there is room above the reserve
        void flushEncoder();
        //queues a button/touch event, waiting for room if needed
        void sendEvent(const InputEvent& event);

        SPIClass& _bus;
        XPT2046_Touchscreen _touch;
        QueueHandle_t _queue;
        pcnt_unit_handle_t _pcnt;

        int _lastCount;
        int32_t _countRemainder;    //PCNT counts not yet worth a full click
        int32_t _pendingClicks;     //summed, not queued yet
        int32_t _pendingDelta;

        EncoderAccel _accel;
        ButtonDebouncer _button;
        TapDetector _tap;
        TouchCalibration _cal;
        uint32_t _lastCalPrintMs;
};

#endif
