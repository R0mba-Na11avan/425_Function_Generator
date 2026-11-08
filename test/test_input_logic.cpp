/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: host tests for the input logic: encoder acceleration, button
             debounce/long press, touch calibration and tap detection.
             samples are fed every INPUT_POLL_MS like the input task does
**************************************************************************/

#include "test.h"
#include "../src/ui/input_logic.h"

//button

//feeds the same level for durationMs, returns how many of each event came out
struct ButtonCount {
    int shorts = 0;
    int longs = 0;
};

static void hold(ButtonDebouncer& b, bool pressed, uint32_t& now, uint32_t durationMs, ButtonCount& c) {
    for (uint32_t end = now + durationMs; now < end; now += INPUT_POLL_MS) {
        BUTTON_EVENT e = b.update(pressed, now);
        if (e == BUTTON_EVENT::SHORT) c.shorts++;
        if (e == BUTTON_EVENT::LONG) c.longs++;
    }
}

TEST(button_short_press) {
    ButtonDebouncer b;
    ButtonCount c;
    uint32_t now = 1000;
    hold(b, false, now, 100, c);
    hold(b, true, now, 150, c);
    CHECK_EQ(c.shorts, 0); //nothing until release
    hold(b, false, now, 100, c);
    CHECK_EQ(c.shorts, 1);
    CHECK_EQ(c.longs, 0);
}

TEST(button_bounce_is_one_press) {
    ButtonDebouncer b;
    ButtonCount c;
    uint32_t now = 1000;
    for (int i = 0; i < 4; i++) { //contact bounce on press
        hold(b, true, now, INPUT_POLL_MS, c);
        hold(b, false, now, INPUT_POLL_MS, c);
    }
    hold(b, true, now, 200, c);
    for (int i = 0; i < 3; i++) { //and on release
        hold(b, false, now, INPUT_POLL_MS, c);
        hold(b, true, now, INPUT_POLL_MS, c);
    }
    hold(b, false, now, 100, c);
    CHECK_EQ(c.shorts, 1);
    CHECK_EQ(c.longs, 0);
}

TEST(button_glitch_is_ignored) {
    ButtonDebouncer b;
    ButtonCount c;
    uint32_t now = 1000;
    hold(b, true, now, BUTTON_DEBOUNCE_MS - INPUT_POLL_MS, c);
    hold(b, false, now, 200, c);
    CHECK_EQ(c.shorts, 0);
    CHECK_EQ(c.longs, 0);
}

TEST(button_long_press_fires_once_while_held) {
    ButtonDebouncer b;
    ButtonCount c;
    uint32_t now = 1000;
    hold(b, true, now, BUTTON_LONG_MS - 50, c);
    CHECK_EQ(c.longs, 0);
    hold(b, true, now, 100, c);
    CHECK_EQ(c.longs, 1); //fired while still held
    hold(b, true, now, 2000, c);
    CHECK_EQ(c.longs, 1); //only once
    hold(b, false, now, 100, c);
    CHECK_EQ(c.shorts, 0); //no SHORT after a LONG
}

TEST(button_two_presses) {
    ButtonDebouncer b;
    ButtonCount c;
    uint32_t now = 1000;
    hold(b, true, now, 100, c);
    hold(b, false, now, 100, c);
    hold(b, true, now, 100, c);
    hold(b, false, now, 100, c);
    CHECK_EQ(c.shorts, 2);
}

TEST(button_long_then_short) {
    ButtonDebouncer b;
    ButtonCount c;
    uint32_t now = 1000;
    hold(b, true, now, 800, c);
    hold(b, false, now, 100, c);
    hold(b, true, now, 100, c);
    hold(b, false, now, 100, c);
    CHECK_EQ(c.longs, 1);
    CHECK_EQ(c.shorts, 1);
}

//encoder acceleration

TEST(encoder_slow_turns_are_1x) {
    EncoderAccel a;
    uint32_t now = 1000;
    CHECK_EQ(a.apply(1, now), 1); //first move
    now += ENC_ACCEL_SLOW_MS;
    CHECK_EQ(a.apply(1, now), 1);
    now += 500;
    CHECK_EQ(a.apply(-1, now), -1);
    now += 500;
    CHECK_EQ(a.apply(-3, now), -3);
}

TEST(encoder_fast_turns_accelerate_up_to_max) {
    EncoderAccel a;
    uint32_t now = 1000;
    a.apply(1, now);
    now += ENC_ACCEL_SLOW_MS / 2;
    CHECK_EQ(a.apply(1, now), 2);
    now += 1;
    CHECK_EQ(a.apply(1, now), ENC_ACCEL_MAX);
    now += 1;
    CHECK_EQ(a.apply(-1, now), -1); //direction change starts over at 1x
    now += 1;
    CHECK_EQ(a.apply(-2, now), -2 * ENC_ACCEL_MAX);
}

TEST(encoder_zero_clicks) {
    EncoderAccel a;
    CHECK_EQ(a.apply(0, 1000), 0);
}

TEST(encoder_survives_millis_wrap) {
    EncoderAccel a;
    uint32_t now = 0xFFFFFFF0u;
    a.apply(1, now);
    now += 20; //wraps past 0
    int32_t steps = a.apply(1, now);
    CHECK(steps > 1 && steps <= ENC_ACCEL_MAX);
}

//touch calibration

static TouchCalibration testCal() {
    return TouchCalibration{200, 3900, 300, 3800, false, 320, 240};
}

TEST(touch_maps_edges_and_center) {
    TouchCalibration cal = testCal();
    TouchPoint p = mapTouch(200, 300, cal);
    CHECK_EQ(p.x, 0);
    CHECK_EQ(p.y, 0);
    p = mapTouch(3900, 3800, cal);
    CHECK_EQ(p.x, 319);
    CHECK_EQ(p.y, 239);
    p = mapTouch(2050, 2050, cal);
    CHECK_NEAR(p.x, 159.5, 1);
    CHECK_NEAR(p.y, 119.5, 1);
}

TEST(touch_clamps_outside_calibration) {
    TouchCalibration cal = testCal();
    TouchPoint p = mapTouch(0, 4095, cal);
    CHECK_EQ(p.x, 0);
    CHECK_EQ(p.y, 239);
}

TEST(touch_mirrored_axis) {
    TouchCalibration cal = testCal();
    cal.rawXMin = 3900;
    cal.rawXMax = 200;
    CHECK_EQ(mapTouch(3900, 300, cal).x, 0);
    CHECK_EQ(mapTouch(200, 300, cal).x, 319);
}

TEST(touch_swap_xy) {
    TouchCalibration cal = testCal();
    cal.swapXY = true;
    //after the swap the panel's Y reading drives the screen X
    TouchPoint p = mapTouch(300, 3900, cal);
    CHECK_EQ(p.x, 319);
    CHECK_EQ(p.y, 0);
}

TEST(touch_default_calibration_is_config) {
    TouchCalibration cal = defaultTouchCalibration();
    CHECK_EQ(cal.rawXMin, TOUCH_RAW_X_MIN);
    CHECK_EQ(cal.rawYMax, TOUCH_RAW_Y_MAX);
    CHECK_EQ(cal.width, LCD_WIDTH);
    CHECK_EQ(cal.height, LCD_HEIGHT);
}

//taps

struct TapCount {
    int taps = 0;
    TouchPoint last = {0, 0};
};

static void touchFor(TapDetector& d, bool touched, TouchPoint p, uint32_t& now, uint32_t durationMs, TapCount& c) {
    for (uint32_t end = now + durationMs; now < end; now += INPUT_POLL_MS) {
        TouchPoint tap;
        if (d.update(touched, p, now, tap)) {
            c.taps++;
            c.last = tap;
        }
    }
}

TEST(tap_quick_press) {
    TapDetector d;
    TapCount c;
    uint32_t now = 1000;
    touchFor(d, true, {100, 50}, now, 120, c);
    CHECK_EQ(c.taps, 0); //not until the finger is lifted
    touchFor(d, false, {0, 0}, now, 100, c);
    CHECK_EQ(c.taps, 1);
    CHECK_EQ(c.last.x, 100);
    CHECK_EQ(c.last.y, 50);
}

TEST(tap_survives_a_dropout) {
    TapDetector d;
    TapCount c;
    uint32_t now = 1000;
    touchFor(d, true, {100, 50}, now, 60, c);
    touchFor(d, false, {0, 0}, now, INPUT_POLL_MS, c); //one missed sample
    touchFor(d, true, {101, 51}, now, 60, c);
    touchFor(d, false, {0, 0}, now, 100, c);
    CHECK_EQ(c.taps, 1);
}

TEST(tap_small_jitter_is_still_a_tap) {
    TapDetector d;
    TapCount c;
    uint32_t now = 1000;
    touchFor(d, true, {100, 50}, now, 40, c);
    touchFor(d, true, {105, 46}, now, 40, c);
    touchFor(d, false, {0, 0}, now, 100, c);
    CHECK_EQ(c.taps, 1);
}

TEST(tap_hold_too_long_is_not_a_tap) {
    TapDetector d;
    TapCount c;
    uint32_t now = 1000;
    touchFor(d, true, {100, 50}, now, TOUCH_TAP_MAX_MS + 100, c);
    touchFor(d, false, {0, 0}, now, 100, c);
    CHECK_EQ(c.taps, 0);
}

TEST(tap_drag_is_not_a_tap) {
    TapDetector d;
    TapCount c;
    uint32_t now = 1000;
    touchFor(d, true, {100, 50}, now, 40, c);
    touchFor(d, true, {100 + TOUCH_TAP_MAX_MOVE_PX + 5, 50}, now, 40, c);
    touchFor(d, false, {0, 0}, now, 100, c);
    CHECK_EQ(c.taps, 0);
}

TEST(tap_two_taps) {
    TapDetector d;
    TapCount c;
    uint32_t now = 1000;
    touchFor(d, true, {10, 10}, now, 80, c);
    touchFor(d, false, {0, 0}, now, 100, c);
    touchFor(d, true, {300, 200}, now, 80, c);
    touchFor(d, false, {0, 0}, now, 100, c);
    CHECK_EQ(c.taps, 2);
    CHECK_EQ(c.last.x, 300);
}

//hold at boot

TEST(hold_timer_done_after_full_hold) {
    HoldTimer h(BOOT_HOLD_MS, 100);
    uint32_t now = 100;
    for (; now < 100 + BOOT_HOLD_MS; now += 10) {
        CHECK(h.update(true, now) == HOLD_STATE::HOLDING);
    }
    CHECK(h.update(true, now) == HOLD_STATE::DONE);
    CHECK_EQ(h.remainingMs(now), 0);
}

TEST(hold_timer_release_cancels) {
    HoldTimer h(BOOT_HOLD_MS, 0);
    CHECK(h.update(true, 500) == HOLD_STATE::HOLDING);
    CHECK_EQ(h.remainingMs(500), BOOT_HOLD_MS - 500);
    CHECK(h.update(false, 600) == HOLD_STATE::HOLDING); //could be a bounce
    CHECK(h.update(false, 600 + BUTTON_DEBOUNCE_MS - 1) == HOLD_STATE::HOLDING);
    CHECK(h.update(false, 600 + BUTTON_DEBOUNCE_MS) == HOLD_STATE::RELEASED);
}

TEST(hold_timer_ignores_bounces) {
    HoldTimer h(BOOT_HOLD_MS, 0);
    h.update(true, 0);
    h.update(false, 1000); //contact bounce
    h.update(false, 1005);
    CHECK(h.update(true, 1010) == HOLD_STATE::HOLDING);
    CHECK(h.update(false, BOOT_HOLD_MS) == HOLD_STATE::HOLDING); //up at the deadline: not done yet
    CHECK(h.update(true, BOOT_HOLD_MS + 5) == HOLD_STATE::DONE);
}
