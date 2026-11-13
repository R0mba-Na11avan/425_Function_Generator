#include "dashboard_view.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../libs/Adafruit_GFX/Fonts/DigitalWF24pt7b.h"
#include "../libs/Adafruit_GFX/Fonts/FreeSans9pt7b.h"
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
static const uint16_t COLOR_REMOTE = 0xF81F;     //magenta: changed by an agent
static const uint16_t COLOR_BLE = 0x3D9F;        //light blue: Bluetooth icon

static const int16_t VALUE_BASELINE = 52;        //amplitude/offset digits, from the field top
static const uint8_t NET_LINE_CHARS = 42;        //built-in font is 6 px wide, stops before the MCP badge

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

//a field an agent just changed gets a thick magenta frame, over any selection color
static void drawFrame(Adafruit_GFX& g, Rect r, bool selected, bool editing, bool remote) {
    uint16_t color = remote ? COLOR_REMOTE : (editing ? COLOR_EDITING : (selected ? COLOR_SELECTED : COLOR_FRAME));
    g.drawRoundRect(r.x, r.y, r.w, r.h, 4, color);
    if (selected || remote) g.drawRoundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 3, color);
    if (remote) g.drawRoundRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 2, color);
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

//the Bluetooth rune, 2 px wide, in a size x size box
static void drawBluetooth(Adafruit_GFX& g, int16_t x, int16_t y, int16_t size, uint16_t color) {
    int16_t cx = x + size / 2 - 1;
    int16_t top = y + 1;
    int16_t bottom = y + size - 2;
    int16_t arm = size / 4;
    int16_t q = (bottom - top) / 4;
    for (int16_t o = 0; o < 2; o++) {
        g.drawLine(cx + o, top, cx + o, bottom, color);
        g.drawLine(cx + o, top, cx + arm + o, top + q, color);                  //top-right bend
        g.drawLine(cx + arm + o, top + q, cx - arm + o, bottom - q, color);     //down to the left
        g.drawLine(cx + o, bottom, cx + arm + o, bottom - q, color);            //bottom-right bend
        g.drawLine(cx + arm + o, bottom - q, cx - arm + o, top + q, color);     //up to the left
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

    //both rects are in screen coordinates, r may be a canvas
    int16_t dx = r.x - statusRect().x;
    int16_t dy = r.y - statusRect().y;
    if (v.remoteBadge) {
        Rect b = mcpBadgeRect();
        b.x += dx;
        b.y += dy;
        g.fillRoundRect(b.x, b.y, b.w, b.h, 3, COLOR_REMOTE);
        g.setTextColor(COLOR_TEXT);
        g.setCursor(b.x + (b.w - 3 * 6) / 2, b.y + (b.h - 8) / 2);
        g.print(remoteBadgeText(v.badgeSource));
    }

    if (v.bleConnected) {
        Rect b = bleSlotRect();
        drawBluetooth(g, b.x + dx, b.y + dy, b.h, COLOR_BLE);
    }
}

static void renderWaveform(Adafruit_GFX& g, Rect r, const DashboardView& v, bool selected, bool editing, bool remote) {
    drawFrame(g, r, selected, editing, remote);
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

static void renderOutput(Adafruit_GFX& g, Rect r, const DashboardView& v, bool selected, bool remote) {
    drawFrame(g, r, selected, false, remote);
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

static void renderFrequency(Adafruit_GFX& g, Rect r, const DashboardView& v, bool selected, bool editing, bool remote) {
    drawFrame(g, r, selected, editing, remote);
    drawLabel(g, r, "FREQUENCY", editing ? COLOR_EDITING : COLOR_LABEL);

    //step, right aligned in the label row. the step the knob really uses at
    //this frequency (0.1 Hz isn't on screen in MHz, so it acts as 1 Hz there)
    uint8_t stepIndex = freqStepShown(v.state.frequency, v.freqStepIndex);
    char step[12];
    char stepLine[20];
    formatFreqStep(stepIndex, step, sizeof(step));
    snprintf(stepLine, sizeof(stepLine), "STEP %s", step);
    g.setFont(nullptr);
    g.setTextColor(editing ? COLOR_EDITING : COLOR_LABEL);
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
    int8_t digit = stepDigitIndex(f, freqStepHz(stepIndex));
    if (digit >= 0) {
        int16_t x = numCharX(f.text, startX, digit);
        g.fillRect(x, baseline + 12, numCharWidth(f.text[digit]), 3, editing ? COLOR_EDITING : COLOR_LABEL);
    }
}

static void renderValue(Adafruit_GFX& g, Rect r, bool selected, bool editing, bool remote,
                        const char* label, const char* value, const char* unit) {
    drawFrame(g, r, selected, editing, remote);
    drawLabel(g, r, label, editing ? COLOR_EDITING : COLOR_LABEL);

    //unit right aligned in the label row, so the number gets the whole width
    //(12.00 and -10.00 don't fit next to a unit)
    g.setFont(nullptr);
    g.setTextColor(COLOR_TEXT);
    g.setCursor(r.x + r.w - 8 - (int16_t)strlen(unit) * 6, r.y + 6);
    g.print(unit);

    int16_t x = r.x + 10;
    int16_t baseline = r.y + VALUE_BASELINE;
    drawNumber(g, x, baseline, value, editing ? COLOR_EDITING : COLOR_TEXT);
}

//public

Rect areaRect(uint8_t area) {
    return (area == STATUS_AREA) ? statusRect() : fieldRect((UI_FIELD)area);
}

void areaKey(const DashboardView& v, uint8_t area, char* key, size_t len) {
    if (area == STATUS_AREA) {
        char line[NET_LINE_CHARS + 1];
        formatNetLine(v.net, line, sizeof(line));
        snprintf(key, len, "%d|%d|%d|%d|%s", (int)v.net.state, v.remoteBadge, (int)v.badgeSource, v.bleConnected, line);
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
    bool remote = v.remoteFlash & fieldBit(field);
    snprintf(key, len, "%d|%d|%d|%s", selected, editing, remote, value);
}

void renderArea(Adafruit_GFX& g, uint8_t area, Rect r, const DashboardView& v) {
    if (area == STATUS_AREA) {
        renderStatus(g, r, v);
        return;
    }

    UI_FIELD field = (UI_FIELD)area;
    bool selected = v.selected == field;
    bool editing = selected && v.editing;
    bool remote = v.remoteFlash & fieldBit(field);
    char value[16];

    switch (field) {
        case UI_FIELD::WAVEFORM:
            renderWaveform(g, r, v, selected, editing, remote);
            break;
        case UI_FIELD::OUTPUT_ENABLE:
            renderOutput(g, r, v, selected, remote);
            break;
        case UI_FIELD::FREQUENCY:
            renderFrequency(g, r, v, selected, editing, remote);
            break;
        case UI_FIELD::AMPLITUDE:
            formatAmplitude(v.state.amplitude, value, sizeof(value));
            renderValue(g, r, selected, editing, remote, "AMPLITUDE", value, "Vpp");
            break;
        case UI_FIELD::OFFSET:
            formatOffset(v.state.dcOffset, value, sizeof(value));
            renderValue(g, r, selected, editing, remote, "OFFSET", value, "V");
            break;
    }
}

//info panel

static const int16_t INFO_LINE_H = 18;

void infoKey(const NetStatus& net, const BleStatus& ble, const InfoView& v, char* key, size_t len) {
    //short fields first, so a long SSID can never push them out of the key
    snprintf(key, len, "info|%d|%d|%u|%u|%d|%d|%d|%d|%d|%s|%s|%s", (int)net.state, (int)ble.state, ble.connected,
             ble.bonded, (int)v.buttons.wifi, v.buttons.phones, (int)v.focus, (int)v.confirm, v.confirmOk, net.ip,
             net.ssid, net.savedSsid);
}

//the confirm dialogs

const char* confirmTitle(INFO_CONFIRM confirm) {
    return confirm == INFO_CONFIRM::FORGET_PHONES ? "Forget all phones?" : "Change Wi-Fi?";
}

const char* confirmLine(INFO_CONFIRM confirm, uint8_t line) {
    static const char* const WIFI[CONFIRM_LINES] = {
        "Opens setup mode to pick a new",
        "network. MCP stops until then.",
        "The current network stays saved."
    };
    static const char* const PHONES[CONFIRM_LINES] = {
        "Every paired phone is disconnected",
        "and has to pair again. Also remove",
        "FuncGen in each phone's settings."
    };
    if (line >= CONFIRM_LINES) return "";
    return confirm == INFO_CONFIRM::FORGET_PHONES ? PHONES[line] : WIFI[line];
}

const char* confirmOkLabel(INFO_CONFIRM confirm) {
    return confirm == INFO_CONFIRM::FORGET_PHONES ? "Forget" : "Open setup";
}

static bool labelFits(Adafruit_GFX& g, const char* text) {
    int16_t x1, y1;
    uint16_t w, h;
    textBounds(g, &FreeSans9pt7b, text, x1, y1, w, h);
    return x1 + (int16_t)w <= infoButtonRect(0).w - 8;
}

void fitButtonLabel(Adafruit_GFX& g, const char* text, char* out, size_t len) {
    snprintf(out, len, "%s", text);
    for (int n = (int)strlen(out) - 1; n > 0 && !labelFits(g, out); n--) {
        snprintf(out, len, "%.*s..", n, text);
    }
}

//a rounded button with centered text, thick yellow border when the knob is on it
static void drawButton(Adafruit_GFX& g, Rect b, const char* label, bool focused, uint16_t fill) {
    char text[48];
    fitButtonLabel(g, label, text, sizeof(text));
    g.fillRoundRect(b.x, b.y, b.w, b.h, 6, fill);
    uint16_t border = focused ? COLOR_SELECTED : COLOR_LABEL;
    g.drawRoundRect(b.x, b.y, b.w, b.h, 6, border);
    if (focused) g.drawRoundRect(b.x + 1, b.y + 1, b.w - 2, b.h - 2, 5, border);

    int16_t x1, y1;
    uint16_t w, h;
    textBounds(g, &FreeSans9pt7b, text, x1, y1, w, h);
    g.setTextColor(COLOR_TEXT);
    g.setCursor(b.x + (b.w - w) / 2 - x1, b.y + (b.h - h) / 2 - y1);
    g.print(text);
}

static void renderConfirm(Adafruit_GFX& g, Rect r, const InfoView& v) {
    const char* title = confirmTitle(v.confirm);
    int16_t x1, y1;
    uint16_t w, h;
    textBounds(g, &FreeSansBold12pt7b, title, x1, y1, w, h);
    g.setTextColor(COLOR_TEXT);
    g.setCursor(r.x + (r.w - w) / 2 - x1, r.y + 44);
    g.print(title);

    g.setFont(&FreeSans9pt7b);
    g.setTextColor(COLOR_LABEL);
    for (uint8_t i = 0; i < CONFIRM_LINES; i++) {
        g.setCursor(r.x + 14, r.y + 88 + i * 22);
        g.print(confirmLine(v.confirm, i));
    }

    uint16_t okFill = v.confirm == INFO_CONFIRM::FORGET_PHONES ? COLOR_OFF : COLOR_STATUS_BG;
    drawButton(g, infoButtonRect(0), "Cancel", !v.confirmOk, COLOR_STATUS_BG);
    drawButton(g, infoButtonRect(1), confirmOkLabel(v.confirm), v.confirmOk, okFill);
}

void renderInfo(Adafruit_GFX& g, Rect r, const NetStatus& net, const BleStatus& ble, const InfoView& v) {
    //gray frame: yellow means "the knob is here"
    g.drawRoundRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 4, COLOR_LABEL);
    if (v.confirm != INFO_CONFIRM::NONE) {
        renderConfirm(g, r, v);
        return;
    }

    drawLabel(g, Rect{(int16_t)(r.x + 4), (int16_t)(r.y + 2), r.w, r.h}, "NETWORK", COLOR_LABEL);
    Rect c = infoCloseRect();
    g.setFont(nullptr);
    g.setTextColor(COLOR_TEXT);
    g.setCursor(c.x + (c.w - 5 * 6) / 2, c.y + (c.h - 8) / 2);
    g.print("close");
    if (v.focus == INFO_ITEM::CLOSE) {
        g.drawRoundRect(c.x, c.y, c.w, c.h, 4, COLOR_SELECTED);
        g.drawRoundRect(c.x + 1, c.y + 1, c.w - 2, c.h - 2, 3, COLOR_SELECTED);
    }

    char lines[INFO_MAX_LINES][INFO_LINE_LEN];
    uint8_t n = formatInfoLines(net, ble, lines, INFO_MAX_LINES);
    g.setFont(&FreeSans9pt7b);
    g.setTextColor(COLOR_TEXT);
    int16_t baseline = r.y + 36;
    for (uint8_t i = 0; i < n; i++, baseline += INFO_LINE_H) {
        g.setCursor(r.x + 12, baseline);
        g.print(lines[i]);
    }

    if (v.buttons.wifi != WIFI_BUTTON::NONE) {
        char label[48];
        formatWifiButton(net, v.buttons.wifi, label, sizeof(label));
        drawButton(g, infoButtonRect(0), label, v.focus == INFO_ITEM::WIFI, COLOR_STATUS_BG);
    }
    if (v.buttons.phones) drawButton(g, infoButtonRect(1), "Forget phones", v.focus == INFO_ITEM::PHONES, COLOR_STATUS_BG);
}

//boot screens

void renderBootHold(Adafruit_GFX& g, uint8_t secondsLeft) {
    g.fillScreen(COLOR_BG);
    g.setFont(&FreeSansBold12pt7b);
    g.setTextColor(COLOR_TEXT);
    const char* title = "Keep holding to forget";
    const char* title2 = "Wi-Fi and paired phones";
    int16_t x1, y1;
    uint16_t w, h;
    textBounds(g, &FreeSansBold12pt7b, title, x1, y1, w, h);
    g.setCursor((LCD_WIDTH - w) / 2 - x1, 60);
    g.print(title);
    textBounds(g, &FreeSansBold12pt7b, title2, x1, y1, w, h);
    g.setCursor((LCD_WIDTH - w) / 2 - x1, 90);
    g.print(title2);

    char count[4];
    snprintf(count, sizeof(count), "%u", secondsLeft);
    int16_t cw = numTextWidth(count);
    drawNumber(g, (LCD_WIDTH - cw) / 2, 160, count, COLOR_AP_BG);

    const char* hint = "Let go to start normally";
    textBounds(g, &FreeSans9pt7b, hint, x1, y1, w, h);
    g.setTextColor(COLOR_LABEL);
    g.setCursor((LCD_WIDTH - w) / 2 - x1, 210);
    g.print(hint);
}

void renderBootMessage(Adafruit_GFX& g, const char* title, const char* detail) {
    g.fillScreen(COLOR_BG);
    int16_t x1, y1;
    uint16_t w, h;
    textBounds(g, &FreeSansBold12pt7b, title, x1, y1, w, h);
    g.setTextColor(COLOR_TEXT);
    g.setCursor((LCD_WIDTH - w) / 2 - x1, 105);
    g.print(title);
    textBounds(g, &FreeSans9pt7b, detail, x1, y1, w, h);
    g.setTextColor(COLOR_LABEL);
    g.setCursor((LCD_WIDTH - w) / 2 - x1, 140);
    g.print(detail);
}

//pairing popup

static void printCentered(Adafruit_GFX& g, const GFXfont* font, const char* text, int16_t baseline, uint16_t color) {
    int16_t x1, y1;
    uint16_t w, h;
    textBounds(g, font, text, x1, y1, w, h);
    g.setTextColor(color);
    g.setCursor((LCD_WIDTH - w) / 2 - x1, baseline);
    g.print(text);
}

void pairingKey(PAIR_PROMPT shown, uint32_t passkey, uint8_t secondsLeft, char* key, size_t len) {
    snprintf(key, len, "pair|%d|%06lu|%u", (int)shown, (unsigned long)passkey, secondsLeft);
}

void renderPairing(Adafruit_GFX& g, PAIR_PROMPT shown, uint32_t passkey, uint8_t secondsLeft) {
    g.fillScreen(COLOR_BG);
    g.drawRoundRect(2, 2, LCD_WIDTH - 4, LCD_HEIGHT - 4, 6, COLOR_BLE);
    drawBluetooth(g, (LCD_WIDTH - 24) / 2, 16, 24, COLOR_BLE);

    switch (shown) {
        case PAIR_PROMPT::CODE: {
            printCentered(g, &FreeSansBold12pt7b, "Bluetooth pairing", 70, COLOR_TEXT);
            printCentered(g, &FreeSans9pt7b, "Enter this code on your phone", 98, COLOR_LABEL);

            //"123 456", the gap makes it easier to read off
            char code[8];
            snprintf(code, sizeof(code), "%03lu %03lu", (unsigned long)(passkey / 1000 % 1000),
                     (unsigned long)(passkey % 1000));
            drawNumber(g, (LCD_WIDTH - numTextWidth(code)) / 2, 160, code, COLOR_TEXT);

            char hint[48];
            snprintf(hint, sizeof(hint), "Press or tap to cancel (%u s)", secondsLeft);
            printCentered(g, &FreeSans9pt7b, hint, 212, COLOR_LABEL);
            break;
        }
        case PAIR_PROMPT::PAIRED:
            printCentered(g, &FreeSansBold18pt7b, "Paired", 130, COLOR_ON);
            printCentered(g, &FreeSans9pt7b, "The phone is remembered", 170, COLOR_LABEL);
            break;
        case PAIR_PROMPT::FAILED:
            printCentered(g, &FreeSansBold18pt7b, "Not paired", 130, COLOR_SELECTED);
            printCentered(g, &FreeSans9pt7b, "Cancelled, timed out or wrong code", 170, COLOR_LABEL);
            break;
        case PAIR_PROMPT::HIDDEN:
        default:
            break;
    }
}
