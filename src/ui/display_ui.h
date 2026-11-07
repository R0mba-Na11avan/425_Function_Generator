/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: draws the dashboard on the ILI9341 using Adafruit GFX and turns
             input events into controller calls
**************************************************************************/

/*
    TO IMPLEMENT: class DisplayUI(FuncGenController&, WifiManager&, InputQueue&)
    (WifiManager is only read for status, see network line below)

    Rules
        - draws directly with Adafruit_GFX + Adafruit_ILI9341 (vendored in
          src/libs). no LVGL, no separate display driver
        - talks to the generator ONLY through FuncGenController (never
          FunctionGenerator)
        - the ILI9341 and the XPT2046 share one SPI bus. never touch the display
          from any task except the UI task, and wrap bus use in SPI transactions

    Threading
        - the controller calls listeners from whichever task made the change
          (HTTP, input, ...). the listener registered here must NOT draw. it just
          copies the FuncGenState and sets a "dirty" flag/notifies the UI task
        - the UI task wakes on that flag (or at ~20-30 Hz), reads the latest
          state and redraws

    Dashboard (single screen)
        - waveform name plus a small drawn preview (sine/square/triangle drawn
          with GFX lines from a precomputed point table, scaled to the box)
        - frequency, large (use one of the DigitalWF fonts), auto-scaled units
          Hz / kHz / MHz with sensible decimals
        - amplitude (Vpp) and DC offset (V) readouts
        - output ON/OFF indicator, colour coded
        - network line: Wi-Fi state (connecting / connected / AP mode), hostname
          or IP. a slot is reserved for a BLE status icon (BLE is not
          implemented yet, so leave the slot blank)
        - in AP-fallback mode, show the AP SSID and 192.168.4.1 prominently so the
          user knows where to connect to enter credentials

    Redraw strategy
        - keep a copy of the last drawn state. on each pass only erase and redraw
          the fields that changed (fillRect the field's box, then print). never
          clear the whole screen after the first draw. this avoids flicker and
          keeps SPI traffic low
        - for large text, use setTextColor(fg, bg) or redraw over a cleared box,
          since GFX fonts don't erase the old glyphs themselves

    Editing (driven by input events)
        - a "selected field" cursor moves over: waveform, frequency, amplitude,
          offset, output. highlight the selected one
        - encoder press: enter/leave edit mode on the selected field. in edit
          mode, encoder turns change the value by the current step
        - frequency has a step selector (1 Hz ... 1 MHz) cycled by a long press
          or by tapping the digit on the touchscreen
        - touch: tap a field to select it, tap the output indicator to toggle it
        - every change goes through controller.setX(); the display itself only
          updates when the listener fires, so the screen always shows what was
          actually applied after clamping (also when an agent changed it)

    Public interface (sketch)
        - begin(): init the panel, set rotation, draw the static parts (labels,
          frames), start the UI task
        - the rest is internal. everything else comes in via listener + input
*/
