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
        | network line                          [BLE] |  status bar
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
Rect bleSlotRect();
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

static const uint8_t FREQ_STEP_COUNT = 7;  //1 Hz .. 1 MHz
float freqStepHz(uint8_t index);
void formatFreqStep(uint8_t index, char* out, size_t len);   //"1 kHz"

void formatAmplitude(float vpp, char* out, size_t len);       //"2.50"
void formatOffset(float volts, char* out, size_t len);        //"+0.25"
const char* waveformName(WAVEFORM waveform);
void formatNetLine(const NetStatus& net, char* out, size_t len);

//what the editor wants the controller to do
enum class EDIT_ACTION : uint8_t {
    NONE,
    SET_WAVEFORM,
    SET_FREQUENCY,
    SET_AMPLITUDE,
    SET_DC_OFFSET,
    SET_OUTPUT
};

struct EditCommand {
    EDIT_ACTION action;
    WAVEFORM waveform;  //SET_WAVEFORM
    float value;        //SET_FREQUENCY / SET_AMPLITUDE / SET_DC_OFFSET
    bool enabled;       //SET_OUTPUT
};

/*
    Selection + edit mode.
        - not editing: encoder turns move the selection
        - short press: enter/leave edit mode (on OUTPUT it toggles the output)
        - editing: encoder turns change the value (frequency by the step,
          amplitude/offset by UI_*_STEP_V snapped to that grid, clamped to the
          limits, waveform cycles)
        - long press on frequency: next step
        - tap: selects a field, tapping the selected one toggles edit mode,
          tapping OUTPUT toggles the output, tapping a frequency digit makes
          that digit the step and starts editing
    It only returns commands. The values shown always come from the
    controller's state, so the screen shows what was actually applied.
*/
class DashboardEditor {
    public:
        DashboardEditor();

        EditCommand handle(const InputEvent& event, const FuncGenState& state, const FuncGenLimits& limits);

        UI_FIELD selected() const { return _selected; }
        bool editing() const { return _editing; }
        uint8_t freqStepIndex() const { return _stepIndex; }

    private:
        EditCommand onEncoder(const InputEvent& event, const FuncGenState& state, const FuncGenLimits& limits);
        EditCommand onShortPress(const FuncGenState& state);
        void onLongPress();
        EditCommand onTap(const InputEvent& event, const FuncGenState& state);

        UI_FIELD _selected;
        bool _editing;
        uint8_t _stepIndex;
};

#endif
