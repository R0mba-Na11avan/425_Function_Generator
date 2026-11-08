/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: draws the dashboard on the ILI9341 using Adafruit GFX and turns
             input events into controller calls
**************************************************************************/

/*
    Rules
        - draws directly with Adafruit_GFX + Adafruit_ILI9341 (vendored in
          src/libs). no LVGL, no separate display driver
        - talks to the generator ONLY through FuncGenController
        - only the UI task touches the display. the XPT2046 shares the bus,
          both libraries wrap every access in an SPI transaction
        - network status comes from a function the sketch passes in
          (setNetStatusSource), because ui never includes comms

    Threading
        - the controller listener registered here only copies the state into
          a spinlock-protected slot and marks it dirty. it never draws
        - the UI task waits up to UI_FRAME_MS for input events, applies them,
          picks up the latest state and redraws what changed (at most every
          UI_FRAME_MS, so ~25 Hz)

    Redraw strategy
        - the screen is split into areas (status bar + one per field, see
          dashboard_logic.h). each area keeps a key string of what it last
          showed (value text, selection, ...). only areas whose key changed
          are redrawn, the whole screen is cleared only once at startup
        - the drawing itself is in dashboard_view (GFX only, host-testable)
        - an area is drawn into its own GFXcanvas16 in PSRAM and sent in one
          block, so nothing flickers. if a canvas can't be allocated that area
          falls back to clearing its box and drawing straight to the panel

    Editing: see DashboardEditor in dashboard_logic.h. every change goes through
    controller.setX(), and the screen only changes when the listener fires, so
    it always shows what was actually applied (also when an agent changed it)

    Remote changes: the listener gets the change source. for MCP/BLE changes it
    works out which fields changed (against the last state it saw) and the UI
    task flashes them, shows the MCP badge and ends edit mode if the field being
    edited was one of them (RemoteHighlight, DashboardEditor::remoteChanged)

    Info panel: while it is open (tap on the status bar) it is drawn instead of
    the fields, straight to the panel since it rarely changes. closing it
    redraws every field
*/

#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include <Arduino.h>
#include <SPI.h>
#include "freertos/FreeRTOS.h"

#include "../config.h"
#include "../core/funcgen_controller.h"
#include "../core/net_status.h"
#include "../libs/Adafruit_ILI9341/Adafruit_ILI9341.h"
#include "input.h"
#include "dashboard_logic.h"
#include "dashboard_view.h"

class DisplayUI {
    public:
        //returns the current network status. called from the UI task
        using NetStatusFn = NetStatus (*)(void* ctx);

        //bus is the display's SPI bus (HSPI), shared with the touch controller
        DisplayUI(FuncGenController& controller, InputManager& input, SPIClass& bus);

        //optional, call before begin(). without it the network line reads "Wi-Fi off"
        void setNetStatusSource(NetStatusFn fn, void* ctx = nullptr);

        //allocates the canvases, registers the listener and starts the UI task,
        //which initialises the panel and draws the first frame
        bool begin();

    private:
        static void onStateChanged(const FuncGenState& state, CHANGE_SOURCE source, void* ctx);
        static void taskEntry(void* arg);
        void run();

        void takeLatestState();
        void apply(const EditCommand& command);
        void pollNetStatus();

        void redraw(uint32_t nowMs);
        void drawArea(uint8_t area, const DashboardView& view);
        void drawInfo();

        Adafruit_ILI9341 _tft;
        FuncGenController& _controller;
        InputManager& _input;
        SPIClass& _bus;

        NetStatusFn _netFn;
        void* _netCtx;
        NetStatus _net;

        //written by the listener (any task), read by the UI task
        portMUX_TYPE _stateMux;
        FuncGenState _latest;
        bool _stateDirty;
        FuncGenState _lastSeen;     //state after the previous change, to find what a remote change touched
        uint8_t _remoteFields;      //fields changed remotely since the UI task last looked
        bool _remotePending;

        //UI task only
        FuncGenState _state;
        FuncGenLimits _limits;
        DashboardEditor _editor;
        RemoteHighlight _highlight;
        uint32_t _lastNetPoll;
        uint32_t _lastDraw;

        GFXcanvas16* _canvas[DASHBOARD_AREA_COUNT];
        char _drawnKey[DASHBOARD_AREA_COUNT][AREA_KEY_LEN];
        char _infoKey[AREA_KEY_LEN];  //empty while the panel isn't shown
};

#endif
