/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: the hardware-free half of the dashboard: screen layout, text
             formatting and DashboardEditor, which turns input events into
             controller commands. DisplayUI draws with these and the host
             tests run them without an ESP32
**************************************************************************/

#ifndef DASHBOARD_LOGIC_H
#define DASHBOARD_LOGIC_H

#include <stdint.h>
#include <stddef.h>
#include "../config.h"
#include "../core/funcgen_state.h"
#include "../core/net_status.h"
#include "../core/ble_status.h"
#include "../core/device_actions.h"
#include "input_logic.h"

struct Rect {
    int16_t x, y, w, h;
    bool contains(int16_t px, int16_t py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

//editable fields, in the order the encoder moves through them (reading order on screen)
enum class UI_FIELD : uint8_t {
    WAVEFORM,
    OUTPUT_ENABLE,
    FREQUENCY,
    AMPLITUDE,
    OFFSET
};

static const uint8_t UI_FIELD_COUNT = 5;

/*
    Layout (320x240 landscape)

        +--------------------------------------------+
        | network line                    [MCP] [ble] |  status bar
        +---------------------+----------------------+
        | WAVEFORM      SINE  | OUTPUT               |
        |   ~~ preview ~~     |      [  ON  ]        |
        +---------------------+----------------------+
        | FREQUENCY                        STEP 1 kHz |
        |            12.345678 MHz                    |
        +---------------------+----------------------+
        | AMPLITUDE           | OFFSET               |
        |   2.50 Vpp          |  +0.00 V             |
        +---------------------+----------------------+
*/
Rect statusRect();
Rect bleSlotRect();     //status bar, right end: Bluetooth icon while a phone is connected
Rect mcpBadgeRect();    //status bar, left of the BLE slot: "MCP"/"BLE" after a remote change
Rect infoRect();        //the info panel, covers everything below the status bar
Rect infoCloseRect();   //info panel, top right: "close"
Rect infoButtonRect(uint8_t slot);  //info panel, bottom row: 0 left, 1 right (also the confirm buttons)
Rect fieldRect(UI_FIELD field);
//field under a screen point, false if none
bool fieldAt(int16_t x, int16_t y, UI_FIELD& field);

/*
    Numbers are drawn in DigitalWF24 one character at a time with a tight,
    fixed spacing (the font's own advance is too wide for 8 digits). These
    widths are the glyph widths of that font, so drawing and tap hit-testing
    share one layout.
*/
static const int16_t NUM_GAP = 4;          //between characters
static const int16_t FREQ_BASELINE = 56;   //frequency digits, from the top of the field
int16_t numCharWidth(char c);
int16_t numTextWidth(const char* text);
//x of the character at index when the text starts at startX
int16_t numCharX(const char* text, int16_t startX, int8_t index);
//index of the character under x when the text starts at startX, -1 if none
int8_t numCharAt(const char* text, int16_t startX, int16_t x);

//frequency readout, auto-scaled to Hz / kHz / MHz
struct FrequencyText {
    char text[16];      //e.g. "12.345678"
    const char* unit;   //"Hz", "kHz", "MHz"
    float unitScale;    //Hz per unit
};

FrequencyText formatFrequency(float hz);
//x of the first digit inside the frequency field (digits are right aligned before the unit)
int16_t freqTextStartX(const char* text);
int16_t freqUnitX();
//place value of the digit at index in units of the text (e.g. 0.01), 0 if not a digit
float digitPlace(const char* text, int8_t index);
//index of the digit whose place value is stepHz, -1 if it isn't shown
int8_t stepDigitIndex(const FrequencyText& f, float stepHz);

static const uint8_t FREQ_STEP_COUNT = 8;  //0.1 Hz .. 1 MHz, powers of 10
float freqStepHz(uint8_t index);
void formatFreqStep(uint8_t index, char* out, size_t len);   //"1 kHz"
//smallest digit the readout shows at hz: 0.1 Hz below 1 MHz, 1 Hz in MHz
float freqResolutionHz(float hz);
//the step really used at hz: index, or the smallest one the readout shows
uint8_t freqStepShown(float hz, uint8_t index);
//the step after index (next power of 10 up, wrapping to the smallest shown one)
uint8_t nextFreqStep(float hz, uint8_t index);

void formatAmplitude(float vpp, char* out, size_t len);       //"2.50"
void formatOffset(float volts, char* out, size_t len);        //"+0.25"
const char* waveformName(WAVEFORM waveform);
void formatNetLine(const NetStatus& net, char* out, size_t len);

//text of the info panel (opened by tapping the status bar), one line each
static const uint8_t INFO_MAX_LINES = 8;
static const uint8_t INFO_LINE_LEN = 64;   //fits the longest SSID and hostname (a long line is clipped on screen)
uint8_t formatInfoLines(const NetStatus& net, const BleStatus& ble, char lines[][INFO_LINE_LEN], uint8_t maxLines);

/*
    Info panel buttons (bottom row) and their confirm dialogs.
        - left: "Change Wi-Fi" (opens setup mode, after a confirm), or in setup
          mode with a saved network "Back to <network>" (no confirm). none in
          setup mode without a saved network
        - right: "Forget phones" (after a confirm), only while BLE is running
        - top right: "close"
    The knob moves a highlight over the buttons and "close" (starts on close,
    so a press closes the panel like before), a press activates. Tapping a
    button activates it, tapping anywhere else closes. In a confirm dialog the
    knob picks Cancel (left, the default) or OK (right), tapping elsewhere
    cancels.
*/
enum class WIFI_BUTTON : uint8_t {
    NONE,
    CHANGE_NETWORK, //DEVICE_ACTION::OPEN_WIFI_SETUP after a confirm
    BACK_TO_SAVED   //DEVICE_ACTION::LEAVE_WIFI_SETUP
};

//which buttons exist right now
struct InfoButtons {
    WIFI_BUTTON wifi;
    bool phones;
};

InfoButtons infoButtons(const NetStatus& net, const BleStatus& ble);
void formatWifiButton(const NetStatus& net, WIFI_BUTTON button, char* out, size_t len);  //"Back to Lab", needs 41 bytes

//what the knob highlights, in knob order
enum class INFO_ITEM : uint8_t {
    WIFI,
    PHONES,
    CLOSE
};

enum class INFO_CONFIRM : uint8_t {
    NONE,
    OPEN_WIFI_SETUP,
    FORGET_PHONES
};

//fields are passed around as bit masks: bit (uint8_t)field
inline uint8_t fieldBit(UI_FIELD field) { return (uint8_t)(1u << (uint8_t)field); }

//fields whose shown value differs between two states (phase isn't on screen)
uint8_t changedFields(const FuncGenState& before, const FuncGenState& after);

//remembers recent remote (MCP/BLE) changes: which fields flash and whether
//the status bar badge shows, and for whom
class RemoteHighlight {
    public:
        void changed(uint8_t fields, uint32_t nowMs, CHANGE_SOURCE source);
        uint8_t flashing(uint32_t nowMs) const;   //mask of fields still flashing
        bool badge(uint32_t nowMs) const;
        CHANGE_SOURCE badgeSource() const { return _badgeSource; }   //who made the latest change

    private:
        uint32_t _fieldAt[UI_FIELD_COUNT] = {};
        uint8_t _fields = 0;
        uint32_t _badgeAt = 0;
        bool _badge = false;
        CHANGE_SOURCE _badgeSource = CHANGE_SOURCE::MCP;
};

//what the pairing popup shows
enum class PAIR_PROMPT : uint8_t {
    HIDDEN,
    CODE,       //the passkey, until the phone finishes, a press/tap cancels or BLE_PAIR_TIMEOUT_MS
    PAIRED,     //result, for UI_PAIR_RESULT_MS or until a press/tap
    FAILED
};

/*
    The full-screen pairing popup. Fed with every BleStatus: a new pairing
    attempt (new pairingId showing a passkey) opens it. While it is up it
    takes every input event, so nothing underneath is edited. Cancelling
    (press, tap or timeout) closes it and asks BLE to drop the phone
    (takeCancel). A cancelled attempt is not shown again.
*/
class PairingPrompt {
    public:
        void update(const BleStatus& ble, uint32_t nowMs);

        //true if the popup took the event (always while it is shown)
        bool handle(const InputEvent& event);

        //true once after the popup cancelled a pairing
        bool takeCancel();

        PAIR_PROMPT shown() const { return _shown; }
        uint32_t passkey() const { return _passkey; }
        uint8_t secondsLeft(uint32_t nowMs) const;  //CODE: until it cancels itself

    private:
        PAIR_PROMPT _shown = PAIR_PROMPT::HIDDEN;
        uint16_t _id = 0;           //pairingId of the attempt last shown
        uint32_t _passkey = 0;
        uint32_t _sinceMs = 0;      //when the current screen appeared
        bool _cancel = false;
};

//what the editor wants the controller to do
enum class EDIT_ACTION : uint8_t {
    NONE,
    SET_WAVEFORM,
    SET_FREQUENCY,
    SET_AMPLITUDE,
    SET_DC_OFFSET,
    SET_OUTPUT,
    DEVICE          //an info panel button: pass device on to the radios
};

struct EditCommand {
    EDIT_ACTION action;
    WAVEFORM waveform;  //SET_WAVEFORM
    float value;        //SET_FREQUENCY / SET_AMPLITUDE / SET_DC_OFFSET
    bool enabled;       //SET_OUTPUT
    DEVICE_ACTION device;   //DEVICE
};

/*
    Selection + edit mode.
        - not editing: encoder turns move the selection
        - short press: enter edit mode (on OUTPUT it toggles the output). on
          amplitude/offset/waveform a second press leaves edit mode
        - editing frequency: short press moves the step to the next power of
          10 (0.1 Hz .. 1 MHz, then back to the smallest), turns change the
          value by the step, starting from the value on screen
        - editing: encoder turns change the value (amplitude/offset by
          UI_*_STEP_V snapped to that grid, clamped to the limits including
          the peak rule, waveform cycles)
        - long press: leave edit mode (any field)
        - tap: selects a field, tapping the selected one toggles edit mode,
          tapping OUTPUT toggles the output, tapping a frequency digit makes
          that digit the step and starts editing
        - tap on the status bar: opens the info panel (buttons: see above)
        - edit mode ends after UI_EDIT_TIMEOUT_MS without input (tick), and
          when an agent changes the field being edited (remoteChanged)
    It only returns commands. The values shown always come from the
    controller's state, so the screen shows what was actually applied.
*/
class DashboardEditor {
    public:
        DashboardEditor();

        //buttons: what the info panel offers right now (infoButtons())
        EditCommand handle(const InputEvent& event, const FuncGenState& state, const FuncGenLimits& limits,
                           uint32_t nowMs, InfoButtons buttons = InfoButtons{WIFI_BUTTON::NONE, false});

        //call regularly: ends edit mode after UI_EDIT_TIMEOUT_MS without input
        void tick(uint32_t nowMs);

        //an agent changed these fields (mask of fieldBit)
        void remoteChanged(uint8_t fields);

        UI_FIELD selected() const { return _selected; }
        bool editing() const { return _editing; }
        uint8_t freqStepIndex() const { return _stepIndex; }
        bool infoOpen() const { return _infoOpen; }
        //the highlighted item, CLOSE if the highlighted button no longer exists
        INFO_ITEM infoFocus(InfoButtons buttons) const;
        INFO_CONFIRM infoConfirm() const { return _confirm; }
        bool confirmOk() const { return _confirmOk; }   //knob is on OK rather than Cancel

    private:
        EditCommand onEncoder(const InputEvent& event, const FuncGenState& state, const FuncGenLimits& limits);
        EditCommand onShortPress(const FuncGenState& state);
        void onLongPress();
        EditCommand onTap(const InputEvent& event, const FuncGenState& state);
        EditCommand onInfo(const InputEvent& event, InfoButtons buttons);
        EditCommand onConfirm(const InputEvent& event);
        EditCommand activate(INFO_ITEM item, InfoButtons buttons);

        UI_FIELD _selected;
        bool _editing;
        uint8_t _stepIndex;
        bool _infoOpen;
        INFO_ITEM _infoFocus;
        INFO_CONFIRM _confirm;
        bool _confirmOk;
        uint32_t _lastInputMs;
};

#endif
