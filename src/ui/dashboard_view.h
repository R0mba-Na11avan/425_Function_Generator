/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: draws the dashboard areas onto any Adafruit_GFX surface (a
             GFXcanvas16 or the panel). no FreeRTOS, no ILI9341, so the host
             tests can render it to an image
**************************************************************************/

#ifndef DASHBOARD_VIEW_H
#define DASHBOARD_VIEW_H

#include <stdint.h>
#include <stddef.h>
#include "../libs/Adafruit_GFX/Adafruit_GFX.h"
#include "dashboard_logic.h"

//everything the screen shows
struct DashboardView {
    FuncGenState state;
    NetStatus net;
    UI_FIELD selected;
    bool editing;
    uint8_t freqStepIndex;
    uint8_t remoteFlash;    //fields an agent just changed (mask of fieldBit)
    bool remoteBadge;       //"MCP" or "BLE" in the status bar
    CHANGE_SOURCE badgeSource;
    bool bleConnected;      //Bluetooth icon in the status bar
};

//areas: one per UI_FIELD (same index), then the status bar
static const uint8_t STATUS_AREA = UI_FIELD_COUNT;
static const uint8_t DASHBOARD_AREA_COUNT = UI_FIELD_COUNT + 1;
static const uint8_t AREA_KEY_LEN = 128;    //fits the info panel key with the longest SSIDs

static const uint16_t DASHBOARD_BG = 0x0000;

Rect areaRect(uint8_t area);

//short text describing what an area shows. if it hasn't changed since the
//last draw, the area doesn't need redrawing
void areaKey(const DashboardView& view, uint8_t area, char* key, size_t len);

//draws one area. r is where it goes on g: (0, 0, w, h) for a canvas of the
//area's size, or areaRect(area) when drawing straight to the panel.
//the area must already be cleared to DASHBOARD_BG
void renderArea(Adafruit_GFX& g, uint8_t area, Rect r, const DashboardView& view);

//the info panel's buttons and dialog state (from DashboardEditor)
struct InfoView {
    InfoButtons buttons;
    INFO_ITEM focus;
    INFO_CONFIRM confirm;
    bool confirmOk;
};

//the info panel (infoRect()), drawn instead of the fields while it is open.
//same rules as areaKey/renderArea. r must be infoRect() (the buttons use
//screen coordinates)
void infoKey(const NetStatus& net, const BleStatus& ble, const InfoView& view, char* key, size_t len);
void renderInfo(Adafruit_GFX& g, Rect r, const NetStatus& net, const BleStatus& ble, const InfoView& view);

//text of the confirm dialogs, for drawing and for the tests
static const uint8_t CONFIRM_LINES = 3;
const char* confirmTitle(INFO_CONFIRM confirm);
const char* confirmLine(INFO_CONFIRM confirm, uint8_t line);
const char* confirmOkLabel(INFO_CONFIRM confirm);

//a button label cut with ".." until it fits an info panel button
void fitButtonLabel(Adafruit_GFX& g, const char* text, char* out, size_t len);

//the pairing popup (whole screen), drawn over everything while it is shown.
//same rules as areaKey/renderArea, but it clears the screen itself
void pairingKey(PAIR_PROMPT shown, uint32_t passkey, uint8_t secondsLeft, char* key, size_t len);
void renderPairing(Adafruit_GFX& g, PAIR_PROMPT shown, uint32_t passkey, uint8_t secondsLeft);

//boot screens (whole screen): the hold-to-forget-Wi-Fi countdown and a message
void renderBootHold(Adafruit_GFX& g, uint8_t secondsLeft);
void renderBootMessage(Adafruit_GFX& g, const char* title, const char* detail);

#endif
