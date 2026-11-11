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
    l.outputPeak = FunctionGenerator::outputPeakMax();
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
    CHECK(strcmp(f.text, "1.0000") == 0 && strcmp(f.unit, "kHz") == 0);
    f = formatFrequency(1500.0f);
    CHECK(strcmp(f.text, "1.5000") == 0 && strcmp(f.unit, "kHz") == 0);
    f = formatFrequency(1000.1f); //the 0.1 Hz digit is shown in kHz too
    CHECK(strcmp(f.text, "1.0001") == 0 && strcmp(f.unit, "kHz") == 0);
    f = formatFrequency(999999.6f);
    CHECK(strcmp(f.text, "999.9996") == 0 && strcmp(f.unit, "kHz") == 0);
    f = formatFrequency(999999.97f); //would round to "1000.0000 kHz"
    CHECK(strcmp(f.text, "1.000000") == 0 && strcmp(f.unit, "MHz") == 0);
    f = formatFrequency(12500000.0f);
    CHECK(strcmp(f.text, "12.500000") == 0 && strcmp(f.unit, "MHz") == 0);
    CHECK_NEAR(f.unitScale, 1e6, 0);
}

TEST(longest_frequency_fits_its_field) {
    FrequencyText f = formatFrequency(FREQ_MAX_HZ);
    CHECK(freqTextStartX(f.text) >= 6);
    f = formatFrequency(999999.0f); //longest kHz text, "999.9990"
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
    FrequencyText f = formatFrequency(1500.0f); //"1.5000" kHz
    CHECK_EQ(stepDigitIndex(f, 1000), 0);
    CHECK_EQ(stepDigitIndex(f, 100), 2);
    CHECK_EQ(stepDigitIndex(f, 1), 4);
    CHECK_EQ(stepDigitIndex(f, 0.1f), 5);
    CHECK_EQ(stepDigitIndex(f, 10000), -1); //not on screen

    f = formatFrequency(5000000.0f); //"5.000000" MHz, every step down to 1 Hz is visible
    for (uint8_t s = 1; s < FREQ_STEP_COUNT; s++) CHECK(stepDigitIndex(f, freqStepHz(s)) >= 0);
    CHECK_EQ(stepDigitIndex(f, freqStepHz(0)), -1); //0.1 Hz isn't

    f = formatFrequency(0.5f); //"0.5" Hz
    CHECK_EQ(stepDigitIndex(f, 0.1f), 2);
}

TEST(freq_steps) {
    CHECK_NEAR(freqStepHz(0), 0.1, 1e-7);
    CHECK_NEAR(freqStepHz(1), 1, 0);
    CHECK_NEAR(freqStepHz(4), 1000, 0);
    CHECK_NEAR(freqStepHz(7), 1000000, 0);
    CHECK_NEAR(freqStepHz(UI_FREQ_STEP_DEFAULT), 1000, 0);
    char buf[16];
    formatFreqStep(0, buf, sizeof(buf));
    CHECK(strcmp(buf, "0.1 Hz") == 0);
    formatFreqStep(1, buf, sizeof(buf));
    CHECK(strcmp(buf, "1 Hz") == 0);
    formatFreqStep(6, buf, sizeof(buf));
    CHECK(strcmp(buf, "100 kHz") == 0);
}

TEST(freq_step_shown_follows_the_readout) {
    CHECK_NEAR(freqResolutionHz(500.0f), 0.1, 1e-7);
    CHECK_NEAR(freqResolutionHz(50000.0f), 0.1, 1e-7);
    CHECK_NEAR(freqResolutionHz(5e6f), 1, 0);
    CHECK_EQ(freqStepShown(500.0f, 0), 0);
    CHECK_EQ(freqStepShown(5e6f, 0), 1);    //0.1 Hz acts as 1 Hz in MHz
    CHECK_EQ(freqStepShown(5e6f, 3), 3);

    //next power of 10 up, wrapping to the smallest shown step
    CHECK_EQ(nextFreqStep(500.0f, 0), 1);
    CHECK_EQ(nextFreqStep(500.0f, FREQ_STEP_COUNT - 1), 0);
    CHECK_EQ(nextFreqStep(5e6f, FREQ_STEP_COUNT - 1), 1);
    CHECK_EQ(nextFreqStep(5e6f, 0), 2);     //from the 1 Hz it was acting as
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

    ed.handle(button(INPUT_EVENT::BUTTON_LONG), s, limits(), 0);
    CHECK(!ed.editing());
}

TEST(editor_press_moves_frequency_step_long_press_leaves) {
    DashboardEditor ed;
    FuncGenState s = state(500.0f);
    ed.handle(button(INPUT_EVENT::BUTTON_LONG), s, limits(), 0);
    CHECK_EQ(ed.freqStepIndex(), UI_FREQ_STEP_DEFAULT); //not editing: nothing happens
    CHECK(!ed.editing());

    ed.handle(button(INPUT_EVENT::BUTTON_SHORT), s, limits(), 0); //enter
    CHECK(ed.editing());
    CHECK_EQ(ed.freqStepIndex(), UI_FREQ_STEP_DEFAULT);
    for (uint8_t i = 1; i <= FREQ_STEP_COUNT; i++) {
        ed.handle(button(INPUT_EVENT::BUTTON_SHORT), s, limits(), 0);
        CHECK_EQ(ed.freqStepIndex(), (UI_FREQ_STEP_DEFAULT + i) % FREQ_STEP_COUNT);
        CHECK(ed.editing()); //presses never leave
    }

    ed.handle(button(INPUT_EVENT::BUTTON_LONG), s, limits(), 0);
    CHECK(!ed.editing());
    uint8_t kept = ed.freqStepIndex();
    ed.handle(button(INPUT_EVENT::BUTTON_SHORT), s, limits(), 0); //enter again
    CHECK_EQ(ed.freqStepIndex(), kept); //the step is remembered

    //on other fields a press still toggles, and a long press also leaves
    DashboardEditor ed2;
    ed2.handle(encoder(1, 1), s, limits(), 0); //amplitude
    ed2.handle(button(INPUT_EVENT::BUTTON_SHORT), s, limits(), 0);
    CHECK(ed2.editing());
    ed2.handle(button(INPUT_EVENT::BUTTON_SHORT), s, limits(), 0);
    CHECK(!ed2.editing());
    ed2.handle(button(INPUT_EVENT::BUTTON_SHORT), s, limits(), 0);
    ed2.handle(button(INPUT_EVENT::BUTTON_LONG), s, limits(), 0);
    CHECK(!ed2.editing());
    CHECK_EQ(ed2.freqStepIndex(), UI_FREQ_STEP_DEFAULT);
}

TEST(editor_frequency_tenths_can_be_removed) {
    //the old trap: 0.1 Hz minimum, 1 Hz smallest step, so 1.1, 2.1, ... forever
    DashboardEditor ed;
    FuncGenState s = state(1.1f);
    ed.handle(button(INPUT_EVENT::BUTTON_SHORT), s, limits(), 0);
    while (ed.freqStepIndex() != 0) ed.handle(button(INPUT_EVENT::BUTTON_SHORT), s, limits(), 0);
    EditCommand c = ed.handle(encoder(-1, -1), s, limits(), 0);
    CHECK(c.action == EDIT_ACTION::SET_FREQUENCY);
    CHECK_NEAR(c.value, 1.0, 1e-5);
}

TEST(editor_frequency_starts_from_the_value_on_screen) {
    DashboardEditor ed;
    ed.handle(button(INPUT_EVENT::BUTTON_SHORT), state(), limits(), 0);
    while (ed.freqStepIndex() != 1) ed.handle(button(INPUT_EVENT::BUTTON_SHORT), state(), limits(), 0); //1 Hz

    //"1.000000 MHz" on screen, the hidden 0.3 Hz is dropped
    EditCommand c = ed.handle(encoder(1, 1), state(1000000.3f), limits(), 0);
    CHECK_NEAR(c.value, 1000001.0, 0.07);   //float spacing near 1e6 is 0.0625

    //the AD9833 applies about 1000.0629 for 1000.1: "1.0001 kHz" on screen,
    //so -0.1 Hz steps from 1000.1, not from 1000.0629
    while (ed.freqStepIndex() != 0) ed.handle(button(INPUT_EVENT::BUTTON_SHORT), state(), limits(), 0);
    c = ed.handle(encoder(-1, -1), state(1000.0629f), limits(), 0);
    CHECK_NEAR(c.value, 1000.0, 1e-3);

    //0.1 Hz step in MHz acts as 1 Hz
    c = ed.handle(encoder(1, 1), state(5000000.0f), limits(), 0);
    CHECK_NEAR(c.value, 5000001.0, 0.5);
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

    ValueRange r = limits().amplitudeFor(s);
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
    CHECK_NEAR(c.value, limits().dcOffsetFor(s).min, 1e-6);
}

TEST(editor_knob_stops_at_the_peak_limit) {
    FuncGenLimits l = limits();
    l.outputPeak = 5.0f;
    l.amplitude[(uint8_t)WAVEFORM::SINE] = ValueRange{0.0f, 12.0f};
    l.dcOffset = ValueRange{-10.0f, 10.0f};

    //offset 2 V: amplitude stops at 6 Vpp
    DashboardEditor ed;
    FuncGenState s = state();
    s.dcOffset = 2.0f;
    ed.handle(encoder(1, 1), s, l, 0); //amplitude
    CHECK(ed.selected() == UI_FIELD::AMPLITUDE);
    ed.handle(button(INPUT_EVENT::BUTTON_SHORT), s, l, 0);
    EditCommand c = ed.handle(encoder(1, 1000), s, l, 0);
    CHECK(c.action == EDIT_ACTION::SET_AMPLITUDE);
    CHECK_NEAR(c.value, 6.0, 1e-5);

    //amplitude 4 Vpp: offset stops at +-3 V
    DashboardEditor ed2;
    s = state();
    s.amplitude = 4.0f;
    ed2.handle(encoder(2, 2), s, l, 0); //offset
    CHECK(ed2.selected() == UI_FIELD::OFFSET);
    ed2.handle(button(INPUT_EVENT::BUTTON_SHORT), s, l, 0);
    c = ed2.handle(encoder(1, 1000), s, l, 0);
    CHECK_NEAR(c.value, 3.0, 1e-5);
    c = ed2.handle(encoder(-1, -1000), s, l, 0);
    CHECK_NEAR(c.value, -3.0, 1e-5);
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
    FuncGenState s = state(1500.0f); //"1.5000" kHz
    FrequencyText f = formatFrequency(s.frequency);
    Rect r = fieldRect(UI_FIELD::FREQUENCY);
    int16_t start = r.x + freqTextStartX(f.text);
    int16_t y = r.y + FREQ_BASELINE - 10;

    ed.handle(tap(numCharX(f.text, start, 2) + 3, y), s, limits(), 0); //the "5" = 100 Hz
    CHECK_EQ(ed.freqStepIndex(), 3);
    CHECK(ed.selected() == UI_FIELD::FREQUENCY);
    CHECK(ed.editing());

    ed.handle(tap(numCharX(f.text, start, 4) + 3, y), s, limits(), 0); //1 Hz digit
    CHECK_EQ(ed.freqStepIndex(), 1);
    CHECK(ed.editing()); //stays in edit mode

    ed.handle(tap(numCharX(f.text, start, 5) + 3, y), s, limits(), 0); //last digit = 0.1 Hz
    CHECK_EQ(ed.freqStepIndex(), 0);

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

//info panel buttons

static const InfoButtons ALL_BUTTONS = {WIFI_BUTTON::CHANGE_NETWORK, true};

static InputEvent tapRect(Rect r) {
    return tap(r.x + r.w / 2, r.y + r.h / 2);
}

static DashboardEditor openInfo() {
    DashboardEditor ed;
    Rect sr = statusRect();
    ed.handle(tap(sr.x + 40, sr.y + 10), state(), limits(), 0, ALL_BUTTONS);
    return ed;
}

TEST(info_buttons_follow_wifi_and_ble) {
    NetStatus net = {};
    BleStatus ble = {};
    net.state = WIFI_STATE::CONNECTED;
    strcpy(net.savedSsid, "Lab");
    ble.state = BLE_STATE::ADVERTISING;
    InfoButtons b = infoButtons(net, ble);
    CHECK(b.wifi == WIFI_BUTTON::CHANGE_NETWORK);
    CHECK(b.phones);

    net.state = WIFI_STATE::AP_MODE;
    CHECK(infoButtons(net, ble).wifi == WIFI_BUTTON::BACK_TO_SAVED);
    net.savedSsid[0] = '\0';
    CHECK(infoButtons(net, ble).wifi == WIFI_BUTTON::NONE);    //already in setup, nothing to go back to

    net.state = WIFI_STATE::CONNECTING;
    CHECK(infoButtons(net, ble).wifi == WIFI_BUTTON::CHANGE_NETWORK);
    ble.state = BLE_STATE::OFF;
    CHECK(!infoButtons(net, ble).phones);
}

TEST(wifi_button_label) {
    NetStatus net = {};
    char label[48];
    formatWifiButton(net, WIFI_BUTTON::CHANGE_NETWORK, label, sizeof(label));
    CHECK(strcmp(label, "Change Wi-Fi") == 0);
    strcpy(net.savedSsid, "SSID-that-is-32-characters-long!");   //whole name, the view fits it
    formatWifiButton(net, WIFI_BUTTON::BACK_TO_SAVED, label, sizeof(label));
    CHECK(strcmp(label, "Back to SSID-that-is-32-characters-long!") == 0);
    formatWifiButton(net, WIFI_BUTTON::NONE, label, sizeof(label));
    CHECK(label[0] == '\0');
}

TEST(info_knob_moves_over_existing_items) {
    DashboardEditor ed = openInfo();
    FuncGenState s = state();
    CHECK(ed.infoFocus(ALL_BUTTONS) == INFO_ITEM::CLOSE);  //a press closes, like before the buttons
    ed.handle(encoder(1, 1), s, limits(), 0, ALL_BUTTONS);
    CHECK(ed.infoFocus(ALL_BUTTONS) == INFO_ITEM::WIFI);
    ed.handle(encoder(1, 1), s, limits(), 0, ALL_BUTTONS);
    CHECK(ed.infoFocus(ALL_BUTTONS) == INFO_ITEM::PHONES);
    ed.handle(encoder(-2, -2), s, limits(), 0, ALL_BUTTONS);
    CHECK(ed.infoFocus(ALL_BUTTONS) == INFO_ITEM::CLOSE);
    ed.handle(encoder(4, 4), s, limits(), 0, ALL_BUTTONS);     //wraps: wifi, phones, close, wifi
    CHECK(ed.infoFocus(ALL_BUTTONS) == INFO_ITEM::WIFI);
    CHECK(ed.infoOpen());
    CHECK(ed.selected() == UI_FIELD::FREQUENCY);    //the dashboard selection didn't move

    //missing buttons are skipped
    InfoButtons phonesOnly = {WIFI_BUTTON::NONE, true};
    DashboardEditor ed2 = openInfo();
    ed2.handle(encoder(1, 1), s, limits(), 0, phonesOnly);
    CHECK(ed2.infoFocus(phonesOnly) == INFO_ITEM::PHONES);
    ed2.handle(encoder(1, 1), s, limits(), 0, phonesOnly);
    CHECK(ed2.infoFocus(phonesOnly) == INFO_ITEM::CLOSE);
    ed2.handle(encoder(-1, -1), s, limits(), 0, phonesOnly);
    CHECK(ed2.infoFocus(phonesOnly) == INFO_ITEM::PHONES);
}

TEST(info_forget_phones_needs_a_confirm) {
    DashboardEditor ed = openInfo();
    FuncGenState s = state();
    ed.handle(encoder(2, 2), s, limits(), 0, ALL_BUTTONS);
    EditCommand c = ed.handle(button(INPUT_EVENT::BUTTON_SHORT), s, limits(), 0, ALL_BUTTONS);
    CHECK(c.action == EDIT_ACTION::NONE);
    CHECK(ed.infoConfirm() == INFO_CONFIRM::FORGET_PHONES);
    CHECK(!ed.confirmOk());     //starts on Cancel

    //turning right picks OK, left goes back to Cancel
    ed.handle(encoder(1, 1), s, limits(), 0, ALL_BUTTONS);
    CHECK(ed.confirmOk());
    ed.handle(encoder(-1, -1), s, limits(), 0, ALL_BUTTONS);
    CHECK(!ed.confirmOk());
    ed.handle(encoder(3, 3), s, limits(), 0, ALL_BUTTONS);
    CHECK(ed.confirmOk());

    c = ed.handle(button(INPUT_EVENT::BUTTON_SHORT), s, limits(), 0, ALL_BUTTONS);
    CHECK(c.action == EDIT_ACTION::DEVICE);
    CHECK(c.device == DEVICE_ACTION::FORGET_PHONES);
    CHECK(ed.infoConfirm() == INFO_CONFIRM::NONE);
    CHECK(ed.infoOpen());       //back on the panel, which then shows 0 paired
}

TEST(info_confirm_cancel_paths) {
    FuncGenState s = state();
    InputEvent cancels[] = {button(INPUT_EVENT::BUTTON_SHORT), button(INPUT_EVENT::BUTTON_LONG),
                            tapRect(infoButtonRect(0)), tap(160, 60)};
    for (const InputEvent& e : cancels) {
        DashboardEditor ed = openInfo();
        ed.handle(tapRect(infoButtonRect(1)), s, limits(), 0, ALL_BUTTONS);
        CHECK(ed.infoConfirm() == INFO_CONFIRM::FORGET_PHONES);
        EditCommand c = ed.handle(e, s, limits(), 0, ALL_BUTTONS);
        CHECK(c.action == EDIT_ACTION::NONE);
        CHECK(ed.infoConfirm() == INFO_CONFIRM::NONE);
        CHECK(ed.infoOpen());
    }

    //tapping OK confirms even with the knob on Cancel
    DashboardEditor ed = openInfo();
    ed.handle(tapRect(infoButtonRect(0)), s, limits(), 0, ALL_BUTTONS);
    CHECK(ed.infoConfirm() == INFO_CONFIRM::OPEN_WIFI_SETUP);
    EditCommand c = ed.handle(tapRect(infoButtonRect(1)), s, limits(), 0, ALL_BUTTONS);
    CHECK(c.action == EDIT_ACTION::DEVICE);
    CHECK(c.device == DEVICE_ACTION::OPEN_WIFI_SETUP);
}

TEST(info_back_to_saved_network_needs_no_confirm) {
    InfoButtons back = {WIFI_BUTTON::BACK_TO_SAVED, true};
    FuncGenState s = state();
    DashboardEditor ed = openInfo();
    ed.handle(encoder(1, 1), s, limits(), 0, back);
    EditCommand c = ed.handle(button(INPUT_EVENT::BUTTON_SHORT), s, limits(), 0, back);
    CHECK(c.action == EDIT_ACTION::DEVICE);
    CHECK(c.device == DEVICE_ACTION::LEAVE_WIFI_SETUP);
    CHECK(ed.infoConfirm() == INFO_CONFIRM::NONE);

    c = ed.handle(tapRect(infoButtonRect(0)), s, limits(), 0, back);
    CHECK(c.action == EDIT_ACTION::DEVICE);
    CHECK(c.device == DEVICE_ACTION::LEAVE_WIFI_SETUP);
}

TEST(info_taps_and_missing_buttons) {
    FuncGenState s = state();
    //tapping where a missing button would be just closes the panel
    InfoButtons none = {WIFI_BUTTON::NONE, false};
    DashboardEditor ed = openInfo();
    EditCommand c = ed.handle(tapRect(infoButtonRect(0)), s, limits(), 0, none);
    CHECK(c.action == EDIT_ACTION::NONE);
    CHECK(!ed.infoOpen());
    CHECK(ed.infoConfirm() == INFO_CONFIRM::NONE);

    ed = openInfo();
    ed.handle(tapRect(infoButtonRect(1)), s, limits(), 0, none);
    CHECK(!ed.infoOpen());

    //"close" closes
    ed = openInfo();
    ed.handle(tapRect(infoCloseRect()), s, limits(), 0, ALL_BUTTONS);
    CHECK(!ed.infoOpen());

    //a highlighted button that goes away (BLE stopped) falls back to close
    ed = openInfo();
    ed.handle(encoder(2, 2), s, limits(), 0, ALL_BUTTONS);
    CHECK(ed.infoFocus(ALL_BUTTONS) == INFO_ITEM::PHONES);
    InfoButtons wifiOnly = {WIFI_BUTTON::CHANGE_NETWORK, false};
    CHECK(ed.infoFocus(wifiOnly) == INFO_ITEM::CLOSE);
    c = ed.handle(button(INPUT_EVENT::BUTTON_SHORT), s, limits(), 0, wifiOnly);
    CHECK(c.action == EDIT_ACTION::NONE);
    CHECK(!ed.infoOpen());
    CHECK(ed.infoConfirm() == INFO_CONFIRM::NONE);

    //opening again starts on close, with no dialog left over
    ed = openInfo();
    ed.handle(tapRect(infoButtonRect(1)), s, limits(), 0, ALL_BUTTONS);
    ed.handle(button(INPUT_EVENT::BUTTON_LONG), s, limits(), 0, ALL_BUTTONS);   //cancels the dialog
    ed.handle(button(INPUT_EVENT::BUTTON_LONG), s, limits(), 0, ALL_BUTTONS);   //closes the panel
    CHECK(!ed.infoOpen());
    Rect sr = statusRect();
    ed.handle(tap(sr.x + 40, sr.y + 10), s, limits(), 0, ALL_BUTTONS);
    CHECK(ed.infoFocus(ALL_BUTTONS) == INFO_ITEM::CLOSE);
    CHECK(ed.infoConfirm() == INFO_CONFIRM::NONE);
}

TEST(info_buttons_are_inside_the_panel_and_apart) {
    Rect panel = infoRect();
    Rect rects[] = {infoButtonRect(0), infoButtonRect(1), infoCloseRect()};
    for (const Rect& r : rects) {
        CHECK(panel.contains(r.x, r.y) && panel.contains(r.x + r.w - 1, r.y + r.h - 1));
        CHECK(r.w >= 40 && r.h >= 18);  //big enough to hit with a finger
    }
    Rect a = infoButtonRect(0), b = infoButtonRect(1);
    CHECK(a.x + a.w < b.x);
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

    h.changed(fieldBit(UI_FIELD::FREQUENCY), 1000, CHANGE_SOURCE::MCP);
    CHECK_EQ(h.flashing(1000), fieldBit(UI_FIELD::FREQUENCY));
    CHECK_EQ(h.flashing(1000 + UI_REMOTE_FLASH_MS - 1), fieldBit(UI_FIELD::FREQUENCY));
    CHECK_EQ(h.flashing(1000 + UI_REMOTE_FLASH_MS), 0);
    CHECK(h.badge(1000 + UI_REMOTE_BADGE_MS - 1));
    CHECK(!h.badge(1000 + UI_REMOTE_BADGE_MS));

    //a second change flashes its own field and keeps the badge up
    h.changed(fieldBit(UI_FIELD::AMPLITUDE), 1500, CHANGE_SOURCE::MCP);
    CHECK_EQ(h.flashing(1600), fieldBit(UI_FIELD::FREQUENCY) | fieldBit(UI_FIELD::AMPLITUDE));
    CHECK_EQ(h.flashing(1000 + UI_REMOTE_FLASH_MS), fieldBit(UI_FIELD::AMPLITUDE));
    CHECK(h.badge(1000 + UI_REMOTE_BADGE_MS));

    //an agent call that changed nothing still shows the badge
    RemoteHighlight quiet;
    quiet.changed(0, 50, CHANGE_SOURCE::MCP);
    CHECK_EQ(quiet.flashing(50), 0);
    CHECK(quiet.badge(50));
}

TEST(remote_highlight_badge_names_the_latest_source) {
    RemoteHighlight h;
    h.changed(fieldBit(UI_FIELD::FREQUENCY), 0, CHANGE_SOURCE::MCP);
    CHECK(h.badgeSource() == CHANGE_SOURCE::MCP);
    h.changed(fieldBit(UI_FIELD::AMPLITUDE), 10, CHANGE_SOURCE::BLE);
    CHECK(h.badgeSource() == CHANGE_SOURCE::BLE);
    h.changed(0, 20, CHANGE_SOURCE::MCP);
    CHECK(h.badgeSource() == CHANGE_SOURCE::MCP);
}

static bool anyLineContains(char lines[][INFO_LINE_LEN], uint8_t n, const char* part) {
    for (uint8_t i = 0; i < n; i++) {
        if (strstr(lines[i], part)) return true;
    }
    return false;
}

static BleStatus bleOn() {
    BleStatus b = {};
    b.state = BLE_STATE::CONNECTED;
    strcpy(b.name, "FuncGen-A1B2");
    b.connected = 1;
    b.bonded = 2;
    return b;
}

TEST(info_lines) {
    char lines[INFO_MAX_LINES][INFO_LINE_LEN];
    BleStatus ble = bleOn();
    NetStatus n = {};
    n.state = WIFI_STATE::CONNECTED;
    strcpy(n.ssid, "Lab");
    strcpy(n.ip, "10.0.0.7");
    strcpy(n.hostname, "funcgen");
    uint8_t count = formatInfoLines(n, ble, lines, INFO_MAX_LINES);
    CHECK(count <= INFO_MAX_LINES);
    CHECK(anyLineContains(lines, count, "Lab"));
    CHECK(anyLineContains(lines, count, "http://funcgen.local/mcp"));
    CHECK(anyLineContains(lines, count, "http://10.0.0.7/mcp"));
    CHECK(anyLineContains(lines, count, "Bluetooth  FuncGen-A1B2"));
    CHECK(anyLineContains(lines, count, "1 connected, 2 paired"));
    CHECK(anyLineContains(lines, count, FIRMWARE_VERSION));   //last line, so nothing was cut off
    CHECK(!anyLineContains(lines, count, "knob"));            //the buttons took the boot-hold hint's place

    n.state = WIFI_STATE::AP_MODE;
    strcpy(n.ssid, "FuncGen-A1B2");
    strcpy(n.ip, "192.168.4.1");
    count = formatInfoLines(n, ble, lines, INFO_MAX_LINES);
    CHECK(anyLineContains(lines, count, "Join the Wi-Fi FuncGen-A1B2"));
    CHECK(anyLineContains(lines, count, "http://192.168.4.1"));
    CHECK(!anyLineContains(lines, count, "/mcp"));
    CHECK(anyLineContains(lines, count, FIRMWARE_VERSION));

    ble.state = BLE_STATE::OFF;
    count = formatInfoLines(n, ble, lines, INFO_MAX_LINES);
    CHECK(anyLineContains(lines, count, "Bluetooth off"));
    CHECK(!anyLineContains(lines, count, "paired"));

    //never writes more lines than asked for
    count = formatInfoLines(n, ble, lines, 2);
    CHECK_EQ(count, 2);
}

//pairing popup

static BleStatus pairingStatus(PAIRING pairing, uint16_t id, uint32_t passkey = 123456) {
    BleStatus b = bleOn();
    b.pairing = pairing;
    b.pairingId = id;
    b.passkey = passkey;
    return b;
}

static InputEvent event(INPUT_EVENT type) {
    InputEvent e = {};
    e.type = type;
    e.source = type == INPUT_EVENT::TOUCH_TAP ? INPUT_SOURCE::TOUCH : INPUT_SOURCE::MAIN_ENCODER;
    e.clicks = 1;
    return e;
}

TEST(pairing_prompt_shows_code_then_result) {
    PairingPrompt p;
    CHECK(p.shown() == PAIR_PROMPT::HIDDEN);
    CHECK(!p.handle(event(INPUT_EVENT::BUTTON_SHORT)));     //hidden: events go to the dashboard
    p.update(pairingStatus(PAIRING::NONE, 0), 0);
    CHECK(p.shown() == PAIR_PROMPT::HIDDEN);

    p.update(pairingStatus(PAIRING::SHOW_PASSKEY, 1, 42), 1000);
    CHECK(p.shown() == PAIR_PROMPT::CODE);
    CHECK_EQ(p.passkey(), 42);
    CHECK_EQ(p.secondsLeft(1000), BLE_PAIR_TIMEOUT_MS / 1000);

    //turning the knob is swallowed but doesn't cancel
    CHECK(p.handle(event(INPUT_EVENT::ENCODER_DELTA)));
    CHECK(p.shown() == PAIR_PROMPT::CODE);
    p.update(pairingStatus(PAIRING::SHOW_PASSKEY, 1, 42), 2000);
    CHECK(p.shown() == PAIR_PROMPT::CODE);

    p.update(pairingStatus(PAIRING::PAIRED, 1), 3000);
    CHECK(p.shown() == PAIR_PROMPT::PAIRED);
    p.update(pairingStatus(PAIRING::PAIRED, 1), 3000 + UI_PAIR_RESULT_MS - 1);
    CHECK(p.shown() == PAIR_PROMPT::PAIRED);
    p.update(pairingStatus(PAIRING::PAIRED, 1), 3000 + UI_PAIR_RESULT_MS);
    CHECK(p.shown() == PAIR_PROMPT::HIDDEN);
    CHECK(!p.takeCancel());

    //the phone failing shows FAILED
    p.update(pairingStatus(PAIRING::SHOW_PASSKEY, 2), 10000);
    CHECK(p.shown() == PAIR_PROMPT::CODE);
    p.update(pairingStatus(PAIRING::FAILED, 2), 11000);
    CHECK(p.shown() == PAIR_PROMPT::FAILED);
    CHECK(!p.takeCancel());
}

TEST(pairing_prompt_press_or_tap_cancels) {
    InputEvent cancels[] = {event(INPUT_EVENT::BUTTON_SHORT), event(INPUT_EVENT::BUTTON_LONG),
                            event(INPUT_EVENT::TOUCH_TAP)};
    for (const InputEvent& e : cancels) {
        PairingPrompt p;
        p.update(pairingStatus(PAIRING::SHOW_PASSKEY, 7), 0);
        CHECK(p.handle(e));
        CHECK(p.shown() == PAIR_PROMPT::HIDDEN);
        CHECK(p.takeCancel());
        CHECK(!p.takeCancel());     //only once

        //the cancelled attempt doesn't come back, nor does its result
        p.update(pairingStatus(PAIRING::SHOW_PASSKEY, 7), 100);
        CHECK(p.shown() == PAIR_PROMPT::HIDDEN);
        p.update(pairingStatus(PAIRING::FAILED, 7), 200);
        CHECK(p.shown() == PAIR_PROMPT::HIDDEN);

        //the next attempt does
        p.update(pairingStatus(PAIRING::SHOW_PASSKEY, 8), 300);
        CHECK(p.shown() == PAIR_PROMPT::CODE);
    }
}

TEST(pairing_prompt_times_out) {
    PairingPrompt p;
    p.update(pairingStatus(PAIRING::SHOW_PASSKEY, 1), 1000);
    p.update(pairingStatus(PAIRING::SHOW_PASSKEY, 1), 1000 + BLE_PAIR_TIMEOUT_MS - 1);
    CHECK(p.shown() == PAIR_PROMPT::CODE);
    CHECK_EQ(p.secondsLeft(1000 + BLE_PAIR_TIMEOUT_MS - 1), 1);
    CHECK(!p.takeCancel());

    p.update(pairingStatus(PAIRING::SHOW_PASSKEY, 1), 1000 + BLE_PAIR_TIMEOUT_MS);
    CHECK(p.shown() == PAIR_PROMPT::FAILED);
    CHECK(p.takeCancel());
    CHECK_EQ(p.secondsLeft(1000 + BLE_PAIR_TIMEOUT_MS), 0);

    //a press on the result just closes it
    CHECK(p.handle(event(INPUT_EVENT::BUTTON_SHORT)));
    CHECK(p.shown() == PAIR_PROMPT::HIDDEN);
    CHECK(!p.takeCancel());
}

TEST(pairing_prompt_timeout_survives_millis_wrap) {
    PairingPrompt p;
    uint32_t start = 0xFFFFFFFFu - 1000;
    p.update(pairingStatus(PAIRING::SHOW_PASSKEY, 1), start);
    p.update(pairingStatus(PAIRING::SHOW_PASSKEY, 1), start + 5000);    //wrapped
    CHECK(p.shown() == PAIR_PROMPT::CODE);
    CHECK_EQ(p.secondsLeft(start + 5000), (BLE_PAIR_TIMEOUT_MS - 5000) / 1000);
}
