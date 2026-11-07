#include "dashboard_view.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../libs/Adafruit_GFX/Fonts/DigitalWF24pt7b.h"
#include "../libs/Adafruit_GFX/Fonts/FreeSansBold9pt7b.h"
#include "../libs/Adafruit_GFX/Fonts/FreeSansBold12pt7b.h"
#include "../libs/Adafruit_GFX/Fonts/FreeSansBold18pt7b.h"

//RGB565
static const uint16_t COLOR_BG = DASHBOARD_BG;
static const uint16_t COLOR_TEXT = 0xFFFF;
static const uint16_t COLOR_LABEL = 0x8410;      //gray
static const uint16_t COLOR_FRAME = 0x31A6;      //dark gray
static const uint16_t COLOR_SELECTED = 0xFFE0;   //yellow
static const uint16_t COLOR_EDITING = 0x07FF;    //cyan
static const uint16_t COLOR_ON = 0x07E0;         //green
static const uint16_t COLOR_OFF = 0x8000;        //dark red
static const uint16_t COLOR_AXIS = 0x2104;
static const uint16_t COLOR_STATUS_BG = 0x18E3;
static const uint16_t COLOR_AP_BG = 0xFD20;      //orange, so setup mode stands out

static const int16_t VALUE_BASELINE = 52;        //amplitude/offset digits, from the field top
static const uint8_t NET_LINE_CHARS = 48;        //built-in font is 6 px wide, stops before the BLE slot

//waveform preview polylines: x 0..1, y -1..1, two periods
static const uint8_t SINE_POINTS = 33;
static const float SQUARE_POINTS[] = {0, 1, 0.25f, 1, 0.25f, -1, 0.5f, -1, 0.5f, 1, 0.75f, 1, 0.75f, -1, 1, -1};
static const float TRIANGLE_POINTS[] = {0, 0, 0.125f, 1, 0.375f, -1, 0.625f, 1, 0.875f, -1, 1, 0};

//filled on first use
static const float* sinePoints() {
    static float points[SINE_POINTS * 2];
    static bool built = false;
    if (!built) {
        for (uint8_t i = 0; i < SINE_POINTS; i++) {
            float x = (float)i / (SINE_POINTS - 1);
            points[i * 2] = x;
            points[i * 2 + 1] = sinf(x * 2.0f * 2.0f * (float)M_PI);
        }
        built = true;
    }
    return points;
}

//helpers

static void drawLabel(Adafruit_GFX& g, Rect r, const char* text, uint16_t color) {
    g.setFont(nullptr);
    g.setTextSize(1);
    g.setTextColor(color);
    g.setCursor(r.x + 6, r.y + 6);
    g.print(text);
}

static void drawFrame(Adafruit_GFX& g, Rect r, bool selected, bool editing) {
    uint16_t color = editing ? COLOR_EDITING : (selected ? COLOR_SELECTED : COLOR_FRAME);
    g.drawRoundRect(r.x, r.y, r.w, r.h, 4, color);
    if (selected) g.drawRoundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 3, color);
}

//bounding box of text in a GFX font
static void textBounds(Adafruit_GFX& g, const GFXfont* font, const char* text, int16_t& x1, int16_t& y1,
                       uint16_t& w, uint16_t& h) {
    g.setFont(font);
    g.setTextSize(1);
    g.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
}

//DigitalWF24 number with the tight spacing from dashboard_logic (numCharWidth + NUM_GAP).
//glyphs are shifted left so their ink starts at the cell's left edge. digits all
//shift by the "0" offset, so a "1" keeps its place at the right of the cell
static void drawNumber(Adafruit_GFX& g, int16_t x, int16_t baseline, const char* text, uint16_t color) {
    const GFXfont& font = DigitalWF24pt7b;
    const int8_t digitOffset = font.glyph['0' - font.first].xOffset;
    g.setFont(&font);
    g.setTextSize(1);
    for (const char* p = text; *p; p++) {
        uint8_t c = (uint8_t)*p;
        if (c >= font.first && c <= font.last) {
            bool digit = c >= '0' && c <= '9';
            int8_t offset = digit ? digitOffset : font.glyph[c - font.first].xOffset;
            g.drawChar(x - offset, baseline, c, color, color, 1);
        }
        x += numCharWidth(*p) + NUM_GAP;
    }
}

static void drawPreview(Adafruit_GFX& g, Rect box, WAVEFORM waveform, uint16_t color) {
    const float* points = sinePoints();
    uint8_t count = SINE_POINTS;
    if (waveform == WAVEFORM::SQUARE) {
        points = SQUARE_POINTS;
        count = sizeof(SQUARE_POINTS) / sizeof(SQUARE_POINTS[0]) / 2;
    } else if (waveform == WAVEFORM::TRIANGLE) {
        points = TRIANGLE_POINTS;
        count = sizeof(TRIANGLE_POINTS) / sizeof(TRIANGLE_POINTS[0]) / 2;
    }

    int16_t midY = box.y + box.h / 2;
    int16_t amp = box.h / 2 - 1;
    g.drawFastHLine(box.x, midY, box.w, COLOR_AXIS);

    int16_t prevX = 0, prevY = 0;
    for (uint8_t i = 0; i < count; i++) {
        int16_t x = box.x + (int16_t)lroundf(points[i * 2] * (box.w - 1));
        int16_t y = midY - (int16_t)lroundf(points[i * 2 + 1] * amp);
        if (i > 0) g.drawLine(prevX, prevY, x, y, color);
        prevX = x;
        prevY = y;
    }
}

//areas

static void renderStatus(Adafruit_GFX& g, Rect r, const DashboardView& v) {
    bool ap = v.net.state == WIFI_STATE::AP_MODE;
    g.fillRect(r.x, r.y, r.w, r.h, ap ? COLOR_AP_BG : COLOR_STATUS_BG);

    char line[NET_LINE_CHARS + 1];
    formatNetLine(v.net, line, sizeof(line));
    g.setFont(nullptr);
    g.setTextSize(1);
    g.setTextColor(ap ? COLOR_BG : COLOR_TEXT);
    g.setCursor(r.x + 4, r.y + 6);
    g.print(line);

    //bleSlotRect() is reserved for a BLE status icon, left blank until BLE exists
}

static void renderWaveform(Adafruit_GFX& g, Rect r, const DashboardView& v, bool selected, bool editing) {
    drawFrame(g, r, selected, editing);
    drawLabel(g, r, "WAVEFORM", editing ? COLOR_EDITING : COLOR_LABEL);

    const char* name = waveformName(v.state.waveform);
    int16_t x1, y1;
    uint16_t w, h;
    textBounds(g, &FreeSansBold9pt7b, name, x1, y1, w, h);
    g.setTextColor(editing ? COLOR_EDITING : COLOR_TEXT);
    g.setCursor(r.x + r.w - 8 - w - x1, r.y + 18);
    g.print(name);

    //preview in green while the output is on
    Rect box = {(int16_t)(r.x + 10), (int16_t)(r.y + 26), (int16_t)(r.w - 20), (int16_t)(r.h - 34)};
    drawPreview(g, box, v.state.waveform, v.state.outputEnabled ? COLOR_ON : COLOR_LABEL);
}

static void renderOutput(Adafruit_GFX& g, Rect r, const DashboardView& v, bool selected) {
    drawFrame(g, r, selected, false);
    drawLabel(g, r, "OUTPUT", COLOR_LABEL);

    bool on = v.state.outputEnabled;
    Rect badge = {(int16_t)(r.x + 28), (int16_t)(r.y + 20), (int16_t)(r.w - 56), (int16_t)(r.h - 30)};
    g.fillRoundRect(badge.x, badge.y, badge.w, badge.h, 6, on ? COLOR_ON : COLOR_OFF);

    const char* text = on ? "ON" : "OFF";
    int16_t x1, y1;
    uint16_t w, h;
    textBounds(g, &FreeSansBold18pt7b, text, x1, y1, w, h);
    g.setTextColor(on ? COLOR_BG : COLOR_TEXT);
    g.setCursor(badge.x + (badge.w - w) / 2 - x1, badge.y + (badge.h - h) / 2 - y1);
    g.print(text);
}

static void renderFrequency(Adafruit_GFX& g, Rect r, const DashboardView& v, bool selected, bool editing) {
    drawFrame(g, r, selected, editing);
    drawLabel(g, r, "FREQUENCY", editing ? COLOR_EDITING : COLOR_LABEL);

    //step, right aligned in the label row
    char step[12];
    char stepLine[20];
    formatFreqStep(v.freqStepIndex, step, sizeof(step));
    snprintf(stepLine, sizeof(stepLine), "STEP %s", step);
    g.setFont(nullptr);
    g.setTextColor(COLOR_LABEL);
    g.setCursor(r.x + r.w - 8 - (int16_t)strlen(stepLine) * 6, r.y + 6);
    g.print(stepLine);

    FrequencyText f = formatFrequency(v.state.frequency);
    int16_t startX = r.x + freqTextStartX(f.text);
    int16_t baseline = r.y + FREQ_BASELINE;
    drawNumber(g, startX, baseline, f.text, editing ? COLOR_EDITING : COLOR_TEXT);

    g.setFont(&FreeSansBold12pt7b);
    g.setTextColor(COLOR_TEXT);
    g.setCursor(r.x + freqUnitX(), baseline);
    g.print(f.unit);

    //underline the digit the encoder changes, if it is on screen
    int8_t digit = stepDigitIndex(f, freqStepHz(v.freqStepIndex));
    if (digit >= 0) {
        int16_t x = numCharX(f.text, startX, digit);
        g.fillRect(x, baseline + 12, numCharWidth(f.text[digit]), 3, editing ? COLOR_EDITING : COLOR_LABEL);
    }
}

static void renderValue(Adafruit_GFX& g, Rect r, bool selected, bool editing,
                        const char* label, const char* value, const char* unit) {
    drawFrame(g, r, selected, editing);
    drawLabel(g, r, label, editing ? COLOR_EDITING : COLOR_LABEL);

    int16_t x = r.x + 10;
    int16_t baseline = r.y + VALUE_BASELINE;
    drawNumber(g, x, baseline, value, editing ? COLOR_EDITING : COLOR_TEXT);

    g.setFont(&FreeSansBold9pt7b);
    g.setTextColor(COLOR_TEXT);
    g.setCursor(x + numTextWidth(value) + 6, baseline);
    g.print(unit);
}

//public

Rect areaRect(uint8_t area) {
    return (area == STATUS_AREA) ? statusRect() : fieldRect((UI_FIELD)area);
}

void areaKey(const DashboardView& v, uint8_t area, char* key, size_t len) {
    if (area == STATUS_AREA) {
        char line[NET_LINE_CHARS + 1];
        formatNetLine(v.net, line, sizeof(line));
        snprintf(key, len, "%d|%s", (int)v.net.state, line);
        return;
    }

    UI_FIELD field = (UI_FIELD)area;
    bool selected = v.selected == field;
    bool editing = selected && v.editing;
    char value[32] = "";

    switch (field) {
        case UI_FIELD::WAVEFORM:
            snprintf(value, sizeof(value), "%d|%d", (int)v.state.waveform, v.state.outputEnabled);
            break;
        case UI_FIELD::OUTPUT_ENABLE:
            snprintf(value, sizeof(value), "%d", v.state.outputEnabled);
            break;
        case UI_FIELD::FREQUENCY: {
            FrequencyText f = formatFrequency(v.state.frequency);
            snprintf(value, sizeof(value), "%s|%s|%u", f.text, f.unit, v.freqStepIndex);
            break;
        }
        case UI_FIELD::AMPLITUDE:
            formatAmplitude(v.state.amplitude, value, sizeof(value));
            break;
        case UI_FIELD::OFFSET:
            formatOffset(v.state.dcOffset, value, sizeof(value));
            break;
    }
    snprintf(key, len, "%d|%d|%s", selected, editing, value);
}

void renderArea(Adafruit_GFX& g, uint8_t area, Rect r, const DashboardView& v) {
    if (area == STATUS_AREA) {
        renderStatus(g, r, v);
        return;
    }

    UI_FIELD field = (UI_FIELD)area;
    bool selected = v.selected == field;
    bool editing = selected && v.editing;
    char value[16];

    switch (field) {
        case UI_FIELD::WAVEFORM:
            renderWaveform(g, r, v, selected, editing);
            break;
        case UI_FIELD::OUTPUT_ENABLE:
            renderOutput(g, r, v, selected);
            break;
        case UI_FIELD::FREQUENCY:
            renderFrequency(g, r, v, selected, editing);
            break;
        case UI_FIELD::AMPLITUDE:
            formatAmplitude(v.state.amplitude, value, sizeof(value));
            renderValue(g, r, selected, editing, "AMPLITUDE", value, "Vpp");
            break;
        case UI_FIELD::OFFSET:
            formatOffset(v.state.dcOffset, value, sizeof(value));
            renderValue(g, r, selected, editing, "OFFSET", value, "V");
            break;
    }
}
