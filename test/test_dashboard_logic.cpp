/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: host tests for the dashboard logic: layout, formatting and the
             editor that turns input events into controller commands
**************************************************************************/

#include "test.h"
#include <string.h>
#include "../src/ui/dashboard_logic.h"
#include "../src/core/func_gen.h"

static FuncGenLimits limits() {
    FuncGenLimits l;
    l.frequency = FunctionGenerator::frequencyRange();
    l.phase = FunctionGenerator::phaseRange();
    for (uint8_t i = 0; i < WAVEFORM_COUNT; i++) l.amplitude[i] = FunctionGenerator::amplitudeRange((WAVEFORM)i);
    l.dcOffset = FunctionGenerator::dcOffsetRange();
    return l;
}

static FuncGenState state(float hz = 1000.0f) {
    return FuncGenState{WAVEFORM::SINE, hz, 0.0f, 1.0f, 0.0f, false};
}

static InputEvent encoder(int16_t clicks, int16_t delta) {
    InputEvent e = {};
    e.type = INPUT_EVENT::ENCODER_DELTA;
    e.clicks = clicks;
    e.delta = delta;
    return e;
}

static InputEvent button(INPUT_EVENT type) {
    InputEvent e = {};
    e.type = type;
    return e;
}

static InputEvent tap(int16_t x, int16_t y) {
    InputEvent e = {};
    e.type = INPUT_EVENT::TOUCH_TAP;
    e.x = x;
    e.y = y;
    return e;
}

static InputEvent tapField(UI_FIELD f) {
    Rect r = fieldRect(f);
    return tap(r.x + 4, r.y + r.h - 4); //corner, away from any digit
}

//formatting

TEST(frequency_units_and_decimals) {
    FrequencyText f = formatFrequency(0.1f);
    CHECK(strcmp(f.text, "0.1") == 0 && strcmp(f.unit, "Hz") == 0);
    f = formatFrequency(999.9f);
    CHECK(strcmp(f.text, "999.9") == 0 && strcmp(f.unit, "Hz") == 0);
    f = formatFrequency(999.96f); //would round to "1000.0 Hz"
    CHECK(strcmp(f.text, "1.000") == 0 && strcmp(f.unit, "kHz") == 0);
    f = formatFrequency(1500.0f);
    CHECK(strcmp(f.text, "1.500") == 0 && strcmp(f.unit, "kHz") == 0);
    f = formatFrequency(999999.7f);
    CHECK(strcmp(f.text, "1.000000") == 0 && strcmp(f.unit, "MHz") == 0);
    f = formatFrequency(12500000.0f);
    CHECK(strcmp(f.text, "12.500000") == 0 && strcmp(f.unit, "MHz") == 0);
    CHECK_NEAR(f.unitScale, 1e6, 0);
}

TEST(longest_frequency_fits_its_field) {
    FrequencyText f = formatFrequency(FREQ_MAX_HZ);
    CHECK(freqTextStartX(f.text) >= 6);
    CHECK(freqUnitX() + 52 <= fieldRect(UI_FIELD::FREQUENCY).w);
}

TEST(number_layout_is_consistent) {
    const char* text = "12.345678";
    CHECK_EQ(numTextWidth(text), 8 * 25 + 4 + 8 * NUM_GAP);
    for (int8_t i = 0; text[i]; i++) {
        int16_t x = numCharX(text, 100, i);
        CHECK_EQ(numCharAt(text, 100, x + 1), i);
        CHECK_EQ(numCharAt(text, 100, x + numCharWidth(text[i]) - 1), i);
    }
    CHECK_EQ(numCharAt(text, 100, 50), -1);
    CHECK_EQ(numCharAt(text, 100, 100 + numTextWidth(text) + 10), -1);
}

TEST(digit_place_values) {
    CHECK_NEAR(digitPlace("12.345", 0), 10, 1e-6);
    CHECK_NEAR(digitPlace("12.345", 1), 1, 1e-6);
    CHECK_NEAR(digitPlace("12.345", 2), 0, 0); //the dot
    CHECK_NEAR(digitPlace("12.345", 3), 0.1, 1e-6);
    CHECK_NEAR(digitPlace("12.345", 5), 0.001, 1e-7);
    CHECK_NEAR(digitPlace("250", 0), 100, 1e-6);
    CHECK_NEAR(digitPlace("12.345", 9), 0, 0);
}

TEST(step_digit_index) {
    FrequencyText f = formatFrequency(1500.0f); //"1.500" kHz
    CHECK_EQ(stepDigitIndex(f, 1000), 0);
    CHECK_EQ(stepDigitIndex(f, 100), 2);
    CHECK_EQ(stepDigitIndex(f, 1), 4);
    CHECK_EQ(stepDigitIndex(f, 10000), -1); //not on screen

    f = formatFrequency(5000000.0f); //"5.000000" MHz, every step down to 1 Hz is visible
    for (uint8_t s = 0; s < FREQ_STEP_COUNT; s++) CHECK(stepDigitIndex(f, freqStepHz(s)) >= 0);
}

TEST(freq_steps) {
    CHECK_NEAR(freqStepHz(0), 1, 0);
    CHECK_NEAR(freqStepHz(3), 1000, 0);
    CHECK_NEAR(freqStepHz(6), 1000000, 0);
    char buf[16];
    formatFreqStep(0, buf, sizeof(buf));
    CHECK(strcmp(buf, "1 Hz") == 0);
    formatFreqStep(5, buf, sizeof(buf));
    CHECK(strcmp(buf, "100 kHz") == 0);
}

TEST(amplitude_and_offset_text) {
    char buf[16];
    formatAmplitude(2.5f, buf, sizeof(buf));
    CHECK(strcmp(buf, "2.50") == 0);
    formatOffset(0.25f, buf, sizeof(buf));
    CHECK(strcmp(buf, "+0.25") == 0);
    formatOffset(-1.25f, buf, sizeof(buf));
    CHECK(strcmp(buf, "-1.25") == 0);
    formatOffset(-0.001f, buf, sizeof(buf));
    CHECK(strcmp(buf, "+0.00") == 0); //no "-0.00"
}

TEST(net_line) {
    NetStatus n = {};
    char buf[64];
    n.state = WIFI_STATE::IDLE;
    formatNetLine(n, buf, sizeof(buf));
    CHECK(strcmp(buf, "Wi-Fi off") == 0);

    n.state = WIFI_STATE::CONNECTING;
    strcpy(n.ssid, "lab");
    formatNetLine(n, buf, sizeof(buf));
    CHECK(strcmp(buf, "Connecting to lab...") == 0);

    n.state = WIFI_STATE::CONNECTED;
    strcpy(n.hostname, "funcgen");
    strcpy(n.ip, "10.0.0.7");
    formatNetLine(n, buf, sizeof(buf));
    CHECK(strcmp(buf, "funcgen.local  10.0.0.7") == 0);

    n.state = WIFI_STATE::AP_MODE;
    strcpy(n.ssid, "FuncGen-A1B2");
    strcpy(n.ip, "192.168.4.1");
    formatNetLine(n, buf, sizeof(buf));
    CHECK(strstr(buf, "FuncGen-A1B2") && strstr(buf, "192.168.4.1"));
    CHECK(strlen(buf) <= 42); //leaves room for the MCP badge
}

//layout

TEST(fields_are_on_screen_and_dont_overlap) {
    for (uint8_t i = 0; i < UI_FIELD_COUNT; i++) {
        Rect a = fieldRect((UI_FIELD)i);
        CHECK(a.x >= 0 && a.y >= statusRect().h && a.x + a.w <= LCD_WIDTH && a.y + a.h <= LCD_HEIGHT);
        for (uint8_t j = i + 1; j < UI_FIELD_COUNT; j++) {
            Rect b = fieldRect((UI_FIELD)j);
            bool overlap = a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
            CHECK(!overlap);
        }
        UI_FIELD hit;
        CHECK(fieldAt(a.x + a.w / 2, a.y + a.h / 2, hit) && hit == (UI_FIELD)i);
    }
    UI_FIELD hit;
    CHECK(!fieldAt(10, 5, hit)); //status bar
}

//editor

TEST(editor_starts_on_frequency) {
    DashboardEditor ed;
    CHECK(ed.selected() == UI_FIELD::FREQUENCY);
    CHECK(!ed.editing());
    CHECK_EQ(ed.freqStepIndex(), UI_FREQ_STEP_DEFAULT);
}

TEST(editor_encoder_moves_selection_and_wraps) {
    DashboardEditor ed;
    FuncGenState s = state();
    EditCommand c = ed.handle(encoder(1, 5), s, limits(), 0);
    CHECK(c.action == EDIT_ACTION::NONE);
    CHECK(ed.selected() == UI_FIELD::AMPLITUDE); //clicks move it, not the accelerated delta
    ed.handle(encoder(2, 2), s, limits(), 0);
    CHECK(ed.selected() == UI_FIELD::WAVEFORM);  //wrapped
    ed.handle(encoder(-1, -1), s, limits(), 0);
    CHECK(ed.selected() == UI_FIELD::OFFSET);    //wrapped backwards
}

TEST(editor_frequency_steps_and_clamps) {
    DashboardEditor ed;
    FuncGenState s = state(1000.0f);
    ed.handle(button(INPUT_EVENT::BUTTON_SHORT), s, limits(), 0);
    CHECK(ed.editing());

    EditCommand c = ed.handle(encoder(1, 3), s, limits(), 0); //accelerated delta, 1 kHz step
    CHECK(c.action == EDIT_ACTION::SET_FREQUENCY);
    CHECK_NEAR(c.value, 4000.0, 1e-3);

    s.frequency = 1234.0f;
    c = ed.handle(encoder(-1, -1), s, limits(), 0);
    CHECK_NEAR(c.value, 234.0, 1e-3); //other digits kept, no snapping

    c = ed.handle(encoder(-1, -10), s, limits(), 0);
    CHECK_NEAR(c.value, FREQ_MIN_HZ, 1e-6);

    s.frequency = FREQ_MAX_HZ;
    c = ed.handle(encoder(1, 1), s, limits(), 0);
    CHECK(c.action == EDIT_ACTION::NONE); //already at the limit

    ed.handle(button(INPUT_EVENT::BUTTON_SHORT), s, limits(), 0);
    CHECK(!ed.editing());
}

TEST(editor_long_press_cycles_frequency_step) {
    DashboardEditor ed;
    FuncGenState s = state();
    for (uint8_t i = 1; i <= FREQ_STEP_COUNT; i++) {
        ed.handle(button(INPUT_EVENT::BUTTON_LONG), s, limits(), 0);
        CHECK_EQ(ed.freqStepIndex(), (UI_FREQ_STEP_DEFAULT + i) % FREQ_STEP_COUNT);
    }
    ed.handle(encoder(1, 1), s, limits(), 0); //amplitude
    uint8_t before = ed.freqStepIndex();
    ed.handle(button(INPUT_EVENT::BUTTON_LONG), s, limits(), 0);
    CHECK_EQ(ed.freqStepIndex(), before); //only on the frequency field
}

TEST(editor_amplitude_snaps_and_clamps) {
    DashboardEditor ed;
    FuncGenState s = state();
    ed.handle(encoder(1, 1), s, limits(), 0); //amplitude
    ed.handle(button(INPUT_EVENT::BUTTON_SHORT), s, limits(), 0);

    s.amplitude = 1.03f;
    EditCommand c = ed.handle(encoder(1, 1), s, limits(), 0);
    CHECK(c.action == EDIT_ACTION::SET_AMPLITUDE);
    CHECK_NEAR(c.value, 1.10, 1e-4); //1.08 snapped to the 0.05 grid

    ValueRange r = limits().amplitude[(uint8_t)WAVEFORM::SINE];
    c = ed.handle(encoder(-1, -100), s, limits(), 0);
    CHECK_NEAR(c.value, r.min, 1e-6);
    c = ed.handle(encoder(1, 1000), s, limits(), 0);
    CHECK_NEAR(c.value, r.max, 1e-6);
}

TEST(editor_offset_steps) {
    DashboardEditor ed;
    FuncGenState s = state();
    ed.handle(encoder(2, 2), s, limits(), 0); //offset
    CHECK(ed.selected() == UI_FIELD::OFFSET);
    ed.handle(button(INPUT_EVENT::BUTTON_SHORT), s, limits(), 0);
    EditCommand c = ed.handle(encoder(-1, -2), s, limits(), 0);
    CHECK(c.action == EDIT_ACTION::SET_DC_OFFSET);
    CHECK_NEAR(c.value, -2 * UI_OFFSET_STEP_V, 1e-5);
    c = ed.handle(encoder(-1, -1000), s, limits(), 0);
    CHECK_NEAR(c.value, DC_OFFSET_MIN_V, 1e-6);
}

TEST(editor_waveform_cycles) {
    DashboardEditor ed;
    FuncGenState s = state();
    ed.handle(encoder(-2, -2), s, limits(), 0); //waveform
    CHECK(ed.selected() == UI_FIELD::WAVEFORM);
    ed.handle(button(INPUT_EVENT::BUTTON_SHORT), s, limits(), 0);

    EditCommand c = ed.handle(encoder(1, 9), s, limits(), 0); //uses clicks, not the accelerated delta
    CHECK(c.action == EDIT_ACTION::SET_WAVEFORM && c.waveform == WAVEFORM::SQUARE);
    c = ed.handle(encoder(-1, -1), s, limits(), 0);
    CHECK(c.waveform == WAVEFORM::TRIANGLE); //wraps backwards
    c = ed.handle(encoder(3, 3), s, limits(), 0);
    CHECK(c.action == EDIT_ACTION::NONE); //full circle, nothing to change
}

TEST(editor_button_toggles_output) {
    DashboardEditor ed;
    FuncGenState s = state();
    ed.handle(encoder(-1, -1), s, limits(), 0); //output
    CHECK(ed.selected() == UI_FIELD::OUTPUT_ENABLE);
    EditCommand c = ed.handle(button(INPUT_EVENT::BUTTON_SHORT), s, limits(), 0);
    CHECK(c.action == EDIT_ACTION::SET_OUTPUT && c.enabled);
    CHECK(!ed.editing()); //no edit mode for a toggle
    s.outputEnabled = true;
    c = ed.handle(button(INPUT_EVENT::BUTTON_SHORT), s, limits(), 0);
    CHECK(c.action == EDIT_ACTION::SET_OUTPUT && !c.enabled);
}

TEST(editor_tap_selects_then_toggles_edit) {
    DashboardEditor ed;
    FuncGenState s = state();
    ed.handle(tapField(UI_FIELD::AMPLITUDE), s, limits(), 0);
    CHECK(ed.selected() == UI_FIELD::AMPLITUDE);
    CHECK(!ed.editing());
    ed.handle(tapField(UI_FIELD::AMPLITUDE), s, limits(), 0);
    CHECK(ed.editing());
    ed.handle(tapField(UI_FIELD::WAVEFORM), s, limits(), 0);
    CHECK(ed.selected() == UI_FIELD::WAVEFORM);
    CHECK(!ed.editing()); //moving away leaves edit mode

    EditCommand c = ed.handle(tap(5, 5), s, limits(), 0); //status bar
    CHECK(c.action == EDIT_ACTION::NONE);
    CHECK(ed.selected() == UI_FIELD::WAVEFORM);
}

TEST(editor_tap_output_toggles) {
    DashboardEditor ed;
    FuncGenState s = state();
    EditCommand c = ed.handle(tapField(UI_FIELD::OUTPUT_ENABLE), s, limits(), 0);
    CHECK(c.action == EDIT_ACTION::SET_OUTPUT && c.enabled);
    CHECK(ed.selected() == UI_FIELD::OUTPUT_ENABLE);
}

TEST(editor_tap_digit_sets_step) {
    DashboardEditor ed;
    FuncGenState s = state(1500.0f); //"1.500" kHz
    FrequencyText f = formatFrequency(s.frequency);
    Rect r = fieldRect(UI_FIELD::FREQUENCY);
    int16_t start = r.x + freqTextStartX(f.text);
    int16_t y = r.y + FREQ_BASELINE - 10;

    ed.handle(tap(numCharX(f.text, start, 2) + 3, y), s, limits(), 0); //the "5" = 100 Hz
    CHECK_EQ(ed.freqStepIndex(), 2);
    CHECK(ed.selected() == UI_FIELD::FREQUENCY);
    CHECK(ed.editing());

    ed.handle(tap(numCharX(f.text, start, 4) + 3, y), s, limits(), 0); //last digit = 1 Hz
    CHECK_EQ(ed.freqStepIndex(), 0);
    CHECK(ed.editing()); //stays in edit mode

    ed.handle(tap(numCharX(f.text, start, 1) + 1, y), s, limits(), 0); //the dot: plain tap
    CHECK_EQ(ed.freqStepIndex(), 0);
    CHECK(!ed.editing()); //tapping the selected field toggles edit mode
}

//step 7: timeout, remote changes, info panel

TEST(editor_edit_mode_times_out) {
    DashboardEditor ed;
    FuncGenState s = state();
    ed.handle(button(INPUT_EVENT::BUTTON_SHORT), s, limits(), 1000);
    CHECK(ed.editing());
    ed.tick(1000 + UI_EDIT_TIMEOUT_MS - 1);
    CHECK(ed.editing());
    ed.handle(encoder(1, 1), s, limits(), 5000); //input restarts the clock
    ed.tick(1000 + UI_EDIT_TIMEOUT_MS);
    CHECK(ed.editing());
    ed.tick(5000 + UI_EDIT_TIMEOUT_MS);
    CHECK(!ed.editing());
    CHECK(ed.selected() == UI_FIELD::FREQUENCY); //selection stays
}

TEST(editor_remote_change_ends_edit_of_that_field) {
    DashboardEditor ed;
    FuncGenState s = state();
    ed.handle(button(INPUT_EVENT::BUTTON_SHORT), s, limits(), 0); //editing frequency
    ed.remoteChanged(fieldBit(UI_FIELD::AMPLITUDE));
    CHECK(ed.editing()); //another field, keep going
    ed.remoteChanged(fieldBit(UI_FIELD::AMPLITUDE) | fieldBit(UI_FIELD::FREQUENCY));
    CHECK(!ed.editing());
    //the next turn moves the selection instead of changing the agent's value
    EditCommand c = ed.handle(encoder(1, 1), s, limits(), 0);
    CHECK(c.action == EDIT_ACTION::NONE);
    CHECK(ed.selected() == UI_FIELD::AMPLITUDE);
}

TEST(editor_info_panel) {
    DashboardEditor ed;
    FuncGenState s = state();
    ed.handle(button(INPUT_EVENT::BUTTON_SHORT), s, limits(), 0);
    Rect sr = statusRect();
    ed.handle(tap(sr.x + 40, sr.y + 10), s, limits(), 0);
    CHECK(ed.infoOpen());
    CHECK(!ed.editing());

    //the knob does nothing while the panel is open
    EditCommand c = ed.handle(encoder(3, 3), s, limits(), 0);
    CHECK(c.action == EDIT_ACTION::NONE);
    CHECK(ed.infoOpen());
    CHECK(ed.selected() == UI_FIELD::FREQUENCY);

    //a tap closes it without acting on what is under the finger
    c = ed.handle(tapField(UI_FIELD::OUTPUT_ENABLE), s, limits(), 0);
    CHECK(!ed.infoOpen());
    CHECK(c.action == EDIT_ACTION::NONE);

    //a press closes it too, also with OUTPUT selected
    ed.handle(tapField(UI_FIELD::OUTPUT_ENABLE), s, limits(), 0); //toggles output, selects it
    ed.handle(tap(sr.x + 40, sr.y + 10), s, limits(), 0);
    c = ed.handle(button(INPUT_EVENT::BUTTON_SHORT), s, limits(), 0);
    CHECK(!ed.infoOpen());
    CHECK(c.action == EDIT_ACTION::NONE);
}

TEST(changed_fields_mask) {
    FuncGenState a = state();
    FuncGenState b = a;
    CHECK_EQ(changedFields(a, b), 0);
    b.phase = 90; //not on screen
    CHECK_EQ(changedFields(a, b), 0);
    b = a;
    b.frequency = 2000;
    b.amplitude = 2;
    CHECK_EQ(changedFields(a, b), fieldBit(UI_FIELD::FREQUENCY) | fieldBit(UI_FIELD::AMPLITUDE));
    b = a;
    b.waveform = WAVEFORM::SQUARE;
    b.outputEnabled = true;
    b.dcOffset = 1;
    CHECK_EQ(changedFields(a, b), fieldBit(UI_FIELD::WAVEFORM) | fieldBit(UI_FIELD::OUTPUT_ENABLE) | fieldBit(UI_FIELD::OFFSET));
}

TEST(remote_highlight_timing) {
    RemoteHighlight h;
    CHECK_EQ(h.flashing(0), 0);
    CHECK(!h.badge(0));

    h.changed(fieldBit(UI_FIELD::FREQUENCY), 1000);
    CHECK_EQ(h.flashing(1000), fieldBit(UI_FIELD::FREQUENCY));
    CHECK_EQ(h.flashing(1000 + UI_REMOTE_FLASH_MS - 1), fieldBit(UI_FIELD::FREQUENCY));
    CHECK_EQ(h.flashing(1000 + UI_REMOTE_FLASH_MS), 0);
    CHECK(h.badge(1000 + UI_REMOTE_BADGE_MS - 1));
    CHECK(!h.badge(1000 + UI_REMOTE_BADGE_MS));

    //a second change flashes its own field and keeps the badge up
    h.changed(fieldBit(UI_FIELD::AMPLITUDE), 1500);
    CHECK_EQ(h.flashing(1600), fieldBit(UI_FIELD::FREQUENCY) | fieldBit(UI_FIELD::AMPLITUDE));
    CHECK_EQ(h.flashing(1000 + UI_REMOTE_FLASH_MS), fieldBit(UI_FIELD::AMPLITUDE));
    CHECK(h.badge(1000 + UI_REMOTE_BADGE_MS));

    //an agent call that changed nothing still shows the badge
    RemoteHighlight quiet;
    quiet.changed(0, 50);
    CHECK_EQ(quiet.flashing(50), 0);
    CHECK(quiet.badge(50));
}

static bool anyLineContains(char lines[][INFO_LINE_LEN], uint8_t n, const char* part) {
    for (uint8_t i = 0; i < n; i++) {
        if (strstr(lines[i], part)) return true;
    }
    return false;
}

TEST(info_lines) {
    char lines[INFO_MAX_LINES][INFO_LINE_LEN];
    NetStatus n = {};
    n.state = WIFI_STATE::CONNECTED;
    strcpy(n.ssid, "Lab");
    strcpy(n.ip, "10.0.0.7");
    strcpy(n.hostname, "funcgen");
    uint8_t count = formatInfoLines(n, lines, INFO_MAX_LINES);
    CHECK(count <= INFO_MAX_LINES);
    CHECK(anyLineContains(lines, count, "Lab"));
    CHECK(anyLineContains(lines, count, "http://funcgen.local/mcp"));
    CHECK(anyLineContains(lines, count, "http://10.0.0.7/mcp"));
    CHECK(anyLineContains(lines, count, "hold the knob"));
    CHECK(anyLineContains(lines, count, FIRMWARE_VERSION));

    n.state = WIFI_STATE::AP_MODE;
    strcpy(n.ssid, "FuncGen-A1B2");
    strcpy(n.ip, "192.168.4.1");
    count = formatInfoLines(n, lines, INFO_MAX_LINES);
    CHECK(anyLineContains(lines, count, "FuncGen-A1B2"));
    CHECK(anyLineContains(lines, count, "http://192.168.4.1"));
    CHECK(!anyLineContains(lines, count, "/mcp"));

    //never writes more lines than asked for
    count = formatInfoLines(n, lines, 2);
    CHECK_EQ(count, 2);
}
