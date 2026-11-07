/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: LAST TO IMPLEMENT. BLE control of the function generator
**************************************************************************/

/*
    LEAVE AS A STUB until the display, input, Wi-Fi and MCP all work. Nothing
    else depends on this file. The notes below are the plan.

    Plan (NimBLE; the ESP32-S3 is BLE only, no Bluetooth Classic)
        - one custom GATT service, one characteristic per setting: waveform (u8),
          frequency, amplitude, phase, DC offset, output enable. all
          read + write + notify
        - choose the frequency encoding up front (u32 in mHz or Hz) so the whole
          range fits without floats
        - write handlers call FuncGenController setters. a controller listener
          sends notifications so every client sees changes from the LCD or MCP
        - clients: nRF Connect for testing, or a small Web Bluetooth page

    To consider when it's built
        - Wi-Fi and BLE share one radio, so check both stay reliable together
        - BLE could later also provision Wi-Fi credentials, replacing the AP
          setup page
        - reserve a BLE status slot on the dashboard (already left blank in
          display_ui)
*/
