/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: reads the rotary encoder, its button and the XPT2046 touch
             controller and turns them into a stream of input events
**************************************************************************/

/*
    TO IMPLEMENT: class InputManager

    This class only produces events. It does not know what a field or a
    frequency is. DisplayUI decides what an event means. The FunctionGenerator is
    deliberately easy to drive from any input, so adding buttons later should
    only mean adding another event source here.

    Event type (struct + enum, shared with display_ui)
        - ENCODER_DELTA   (signed steps since the last event, accelerated if
                           the knob is turned quickly)
        - BUTTON_SHORT    (press and release under ~500 ms)
        - BUTTON_LONG     (held past ~500 ms)
        - TOUCH_TAP       (x, y in screen coordinates, after calibration)

    Delivery
        - events go into a FreeRTOS queue (small, e.g. 16 entries). DisplayUI
          is the only consumer. if the queue is full, drop the oldest
          ENCODER_DELTA, never drop button/touch events

    Rotary encoder
        - quadrature decoding. either the ESP32-S3 PCNT peripheral (preferred,
          no missed edges, hardware glitch filter) or a pin interrupt with a state
          table. no polling in loop()
        - button: debounce (~20-30 ms), long-press detection, ISR or timer based

    Touch (XPT2046)
        - shares the ILI9341 SPI bus with its own CS and a much slower clock, so
          wrap each read in an SPI transaction with its own settings
        - use the IRQ pin to know when a touch exists instead of polling the SPI
          bus constantly, and then sample a few times and median/average
        - map raw readings to screen pixels with calibration constants from
          config.h. tap = touch released within a short time and a small movement
        - lift/drop (no sliding or multi-touch needed)

    Public interface (sketch)
        - begin(): configure pins/PCNT/touch, create the queue and the input task
        - queue handle (or a receive(event, timeout) method) for DisplayUI
*/
