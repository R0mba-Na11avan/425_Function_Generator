#include "dashboard_logic.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

//layout

static const Rect STATUS_RECT = {0, 0, LCD_WIDTH, 20};
static const Rect BLE_SLOT_RECT = {LCD_WIDTH - 20, 2, 16, 16};

static const Rect FIELD_RECTS[UI_FIELD_COUNT] = {
    {2, 22, 156, 70},       //WAVEFORM
    {162, 22, 156, 70},     //OUTPUT
    {2, 94, 316, 76},       //FREQUENCY
    {2, 172, 156, 66},      //AMPLITUDE
    {162, 172, 156, 66}     //OFFSET
};

//frequency field: unit column at the right, digits right aligned before it
static const int16_t FREQ_UNIT_W = 52;     //widest unit ("MHz") in FreeSansBold12
static const int16_t FREQ_MARGIN = 6;

Rect statusRect() {
    return STATUS_RECT;
}

Rect bleSlotRect() {
    return BLE_SLOT_RECT;
}

Rect fieldRect(UI_FIELD field) {
    return FIELD_RECTS[(uint8_t)field];
}

bool fieldAt(int16_t x, int16_t y, UI_FIELD& field) {
    for (uint8_t i = 0; i < UI_FIELD_COUNT; i++) {
        if (FIELD_RECTS[i].contains(x, y)) {
            field = (UI_FIELD)i;
            return true;
        }
    }
    return false;
}

//numbers

int16_t numCharWidth(char c) {
    //glyph widths of DigitalWF24pt7b (all digits share one cell, "1" sits at its right edge)
    if (c >= '0' && c <= '9') return 25;
    if (c == '.') return 4;
    if (c == '-') return 11;
    if (c == '+') return 12;
    return 25;
}

int16_t numTextWidth(const char* text) {
    int16_t w = 0;
    for (const char* p = text; *p; p++) {
        if (p != text) w += NUM_GAP;
        w += numCharWidth(*p);
    }
    return w;
}

int16_t numCharX(const char* text, int16_t startX, int8_t index) {
    int16_t cx = startX;
    for (int8_t i = 0; i < index && text[i]; i++) {
        cx += numCharWidth(text[i]) + NUM_GAP;
    }
    return cx;
}

int8_t numCharAt(const char* text, int16_t startX, int16_t x) {
    int16_t cx = startX;
    for (int8_t i = 0; text[i]; i++) {
        int16_t w = numCharWidth(text[i]);
        //each character owns half the gap on either side
        if (x >= cx - NUM_GAP / 2 && x < cx + w + NUM_GAP / 2) return i;
        cx += w + NUM_GAP;
    }
    return -1;
}

//frequency

FrequencyText formatFrequency(float hz) {
    FrequencyText f;
    //pick the unit from the rounded value so 999.96 Hz reads "1.000 kHz", not "1000.0 Hz"
    if (hz < 999.95f) {
        snprintf(f.text, sizeof(f.text), "%.1f", hz);
        f.unit = "Hz";
        f.unitScale = 1.0f;
    } else if (hz < 999999.5f) {
        snprintf(f.text, sizeof(f.text), "%.3f", hz / 1e3f);
        f.unit = "kHz";
        f.unitScale = 1e3f;
    } else {
        snprintf(f.text, sizeof(f.text), "%.6f", hz / 1e6);
        f.unit = "MHz";
        f.unitScale = 1e6f;
    }
    return f;
}

int16_t freqUnitX() {
    return FIELD_RECTS[(uint8_t)UI_FIELD::FREQUENCY].w - FREQ_MARGIN - FREQ_UNIT_W;
}

int16_t freqTextStartX(const char* text) {
    return freqUnitX() - FREQ_MARGIN - numTextWidth(text);
}

float digitPlace(const char* text, int8_t index) {
    int len = (int)strlen(text);
    if (index < 0 || index >= len) return 0;
    if (text[index] < '0' || text[index] > '9') return 0;

    const char* dot = strchr(text, '.');
    int dotIndex = dot ? (int)(dot - text) : len;
    int power = (index < dotIndex) ? dotIndex - index - 1 : -(index - dotIndex);
    return powf(10.0f, (float)power);
}

int8_t stepDigitIndex(const FrequencyText& f, float stepHz) {
    for (int8_t i = 0; f.text[i]; i++) {
        float place = digitPlace(f.text, i) * f.unitScale;
        if (place > 0 && fabsf(place - stepHz) < stepHz * 0.01f) return i;
    }
    return -1;
}

float freqStepHz(uint8_t index) {
    if (index >= FREQ_STEP_COUNT) index = FREQ_STEP_COUNT - 1;
    return powf(10.0f, (float)index);
}

void formatFreqStep(uint8_t index, char* out, size_t len) {
    static const char* const LABELS[FREQ_STEP_COUNT] = {
        "1 Hz", "10 Hz", "100 Hz", "1 kHz", "10 kHz", "100 kHz", "1 MHz"
    };
    if (index >= FREQ_STEP_COUNT) index = FREQ_STEP_COUNT - 1;
    snprintf(out, len, "%s", LABELS[index]);
}

//other readouts

void formatAmplitude(float vpp, char* out, size_t len) {
    snprintf(out, len, "%.2f", vpp);
}

void formatOffset(float volts, char* out, size_t len) {
    //avoid "-0.00" for tiny negative values
    if (fabsf(volts) < 0.005f) volts = 0.0f;
    snprintf(out, len, "%+.2f", volts);
}

const char* waveformName(WAVEFORM waveform) {
    switch (waveform) {
        case WAVEFORM::SINE: return "SINE";
        case WAVEFORM::SQUARE: return "SQUARE";
        case WAVEFORM::TRIANGLE: return "TRIANGLE";
        default: return "?";
    }
}

void formatNetLine(const NetStatus& net, char* out, size_t len) {
    switch (net.state) {
        case WIFI_STATE::CONNECTING:
            snprintf(out, len, "Connecting to %s...", net.ssid);
            break;
        case WIFI_STATE::CONNECTED:
            snprintf(out, len, "%s.local  %s", net.hostname, net.ip);
            break;
        case WIFI_STATE::AP_MODE:
            snprintf(out, len, "Setup: join %s  http://%s", net.ssid, net.ip);
            break;
        case WIFI_STATE::IDLE:
        default:
            snprintf(out, len, "Wi-Fi off");
            break;
    }
}

//editor

static float clampRange(float v, ValueRange r) {
    if (v < r.min) return r.min;
    if (v > r.max) return r.max;
    return v;
}

//steps v by delta * step, snapped to the step grid, clamped
static float stepSnapped(float v, int32_t delta, float step, ValueRange r) {
    float stepped = roundf((v + delta * step) / step) * step;
    return clampRange(stepped, r);
}

static EditCommand noCommand() {
    return EditCommand{EDIT_ACTION::NONE, WAVEFORM::SINE, 0.0f, false};
}

static EditCommand valueCommand(EDIT_ACTION action, float value, float current) {
    if (value == current) return noCommand();
    EditCommand c = noCommand();
    c.action = action;
    c.value = value;
    return c;
}

static EditCommand outputCommand(bool enabled) {
    EditCommand c = noCommand();
    c.action = EDIT_ACTION::SET_OUTPUT;
    c.enabled = enabled;
    return c;
}

DashboardEditor::DashboardEditor()
    : _selected(UI_FIELD::FREQUENCY), _editing(false), _stepIndex(UI_FREQ_STEP_DEFAULT) {}

EditCommand DashboardEditor::handle(const InputEvent& event, const FuncGenState& state, const FuncGenLimits& limits) {
    switch (event.type) {
        case INPUT_EVENT::ENCODER_DELTA: return onEncoder(event, state, limits);
        case INPUT_EVENT::BUTTON_SHORT: return onShortPress(state);
        case INPUT_EVENT::BUTTON_LONG: onLongPress(); return noCommand();
        case INPUT_EVENT::TOUCH_TAP: return onTap(event, state);
        default: return noCommand();
    }
}

EditCommand DashboardEditor::onEncoder(const InputEvent& event, const FuncGenState& state, const FuncGenLimits& limits) {
    if (!_editing) {
        int32_t i = ((int32_t)_selected + event.clicks) % UI_FIELD_COUNT;
        if (i < 0) i += UI_FIELD_COUNT;
        _selected = (UI_FIELD)i;
        return noCommand();
    }

    switch (_selected) {
        case UI_FIELD::WAVEFORM: {
            int32_t i = ((int32_t)state.waveform + event.clicks) % WAVEFORM_COUNT;
            if (i < 0) i += WAVEFORM_COUNT;
            if ((WAVEFORM)i == state.waveform) return noCommand();
            EditCommand c = noCommand();
            c.action = EDIT_ACTION::SET_WAVEFORM;
            c.waveform = (WAVEFORM)i;
            return c;
        }
        case UI_FIELD::FREQUENCY: {
            //no snapping, so the other digits stay as they are
            double hz = (double)state.frequency + (double)event.delta * freqStepHz(_stepIndex);
            return valueCommand(EDIT_ACTION::SET_FREQUENCY, clampRange((float)hz, limits.frequency), state.frequency);
        }
        case UI_FIELD::AMPLITUDE: {
            ValueRange r = limits.amplitude[(uint8_t)state.waveform];
            return valueCommand(EDIT_ACTION::SET_AMPLITUDE,
                                stepSnapped(state.amplitude, event.delta, UI_AMP_STEP_V, r), state.amplitude);
        }
        case UI_FIELD::OFFSET:
            return valueCommand(EDIT_ACTION::SET_DC_OFFSET,
                                stepSnapped(state.dcOffset, event.delta, UI_OFFSET_STEP_V, limits.dcOffset),
                                state.dcOffset);
        case UI_FIELD::OUTPUT_ENABLE:
        default:
            return noCommand();
    }
}

EditCommand DashboardEditor::onShortPress(const FuncGenState& state) {
    if (_selected == UI_FIELD::OUTPUT_ENABLE) {
        _editing = false;
        return outputCommand(!state.outputEnabled);
    }
    _editing = !_editing;
    return noCommand();
}

void DashboardEditor::onLongPress() {
    if (_selected == UI_FIELD::FREQUENCY) {
        _stepIndex = (_stepIndex + 1) % FREQ_STEP_COUNT;
    }
}

EditCommand DashboardEditor::onTap(const InputEvent& event, const FuncGenState& state) {
    UI_FIELD field;
    if (!fieldAt(event.x, event.y, field)) return noCommand();

    if (field == UI_FIELD::OUTPUT_ENABLE) {
        _selected = field;
        _editing = false;
        return outputCommand(!state.outputEnabled);
    }

    if (field == UI_FIELD::FREQUENCY) {
        FrequencyText f = formatFrequency(state.frequency);
        Rect r = fieldRect(field);
        int8_t i = numCharAt(f.text, r.x + freqTextStartX(f.text), event.x);
        float placeHz = digitPlace(f.text, i) * f.unitScale;
        for (uint8_t s = 0; s < FREQ_STEP_COUNT; s++) {
            if (placeHz > 0 && fabsf(placeHz - freqStepHz(s)) < freqStepHz(s) * 0.01f) {
                _stepIndex = s;
                _selected = field;
                _editing = true;
                return noCommand();
            }
        }
    }

    if (field == _selected) {
        _editing = !_editing;
    } else {
        _selected = field;
        _editing = false;
    }
    return noCommand();
}
