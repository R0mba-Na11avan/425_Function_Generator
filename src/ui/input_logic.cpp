#include "input_logic.h"

int32_t EncoderAccel::apply(int32_t clicks, uint32_t nowMs) {
    if (clicks == 0) return 0;

    int8_t dir = (clicks > 0) ? 1 : -1;
    uint32_t dt = nowMs - _lastMs;

    //the first move and any change of direction always start at 1x
    int32_t mult = 1;
    if (_lastDir == dir && dt < ENC_ACCEL_SLOW_MS) {
        mult = ENC_ACCEL_SLOW_MS / (dt ? dt : 1);
        if (mult > ENC_ACCEL_MAX) mult = ENC_ACCEL_MAX;
        if (mult < 1) mult = 1;
    }

    _lastMs = nowMs;
    _lastDir = dir;
    return clicks * mult;
}

BUTTON_EVENT ButtonDebouncer::update(bool pressed, uint32_t nowMs) {
    if (pressed != _raw) {
        _raw = pressed;
        _rawSince = nowMs;
    }

    //accept a level once it has been steady for the debounce time
    if (_raw != _stable && nowMs - _rawSince >= BUTTON_DEBOUNCE_MS) {
        _stable = _raw;
        if (_stable) {
            _pressedAt = _rawSince;
            _longSent = false;
        } else if (!_longSent) {
            return BUTTON_EVENT::SHORT;
        }
    }

    if (_stable && !_longSent && nowMs - _pressedAt >= BUTTON_LONG_MS) {
        _longSent = true;
        return BUTTON_EVENT::LONG;
    }
    return BUTTON_EVENT::NONE;
}

//HoldTimer

HoldTimer::HoldTimer(uint32_t holdMs, uint32_t startMs) : _holdMs(holdMs), _startMs(startMs) {}

HOLD_STATE HoldTimer::update(bool pressed, uint32_t nowMs) {
    if (pressed) {
        _up = false;
    } else if (!_up) {
        _up = true;
        _upSince = nowMs;
    } else if (nowMs - _upSince >= BUTTON_DEBOUNCE_MS) {
        return HOLD_STATE::RELEASED;
    }
    if (!_up && nowMs - _startMs >= _holdMs) return HOLD_STATE::DONE;
    return HOLD_STATE::HOLDING;
}

uint32_t HoldTimer::remainingMs(uint32_t nowMs) const {
    uint32_t held = nowMs - _startMs;
    return (held >= _holdMs) ? 0 : _holdMs - held;
}

TouchCalibration defaultTouchCalibration() {
    return TouchCalibration{
        TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX,
        TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX,
        TOUCH_SWAP_XY != 0,
        LCD_WIDTH, LCD_HEIGHT
    };
}

//maps raw from [rawMin, rawMax] onto [0, size - 1]. rawMin > rawMax mirrors the axis
static int16_t mapAxis(int32_t raw, int32_t rawMin, int32_t rawMax, int32_t size) {
    if (rawMax == rawMin) return 0;
    int32_t v = (raw - rawMin) * (size - 1) / (rawMax - rawMin);
    if (v < 0) v = 0;
    if (v > size - 1) v = size - 1;
    return (int16_t)v;
}

TouchPoint mapTouch(int16_t rawX, int16_t rawY, const TouchCalibration& cal) {
    if (cal.swapXY) {
        int16_t t = rawX;
        rawX = rawY;
        rawY = t;
    }
    return TouchPoint{
        mapAxis(rawX, cal.rawXMin, cal.rawXMax, cal.width),
        mapAxis(rawY, cal.rawYMin, cal.rawYMax, cal.height)
    };
}

bool TapDetector::update(bool touched, TouchPoint point, uint32_t nowMs, TouchPoint& tap) {
    if (touched) {
        if (!_down) {
            _down = true;
            _moved = false;
            _start = point;
            _downAt = nowMs;
        } else {
            int32_t dx = point.x - _start.x;
            int32_t dy = point.y - _start.y;
            if (dx * dx + dy * dy > TOUCH_TAP_MAX_MOVE_PX * TOUCH_TAP_MAX_MOVE_PX) _moved = true;
        }
        _lastTouchMs = nowMs;
        return false;
    }

    //resistive panels drop out for a sample now and then, so only count it as
    //lifted after TOUCH_RELEASE_MS without contact
    if (_down && nowMs - _lastTouchMs >= TOUCH_RELEASE_MS) {
        _down = false;
        if (!_moved && _lastTouchMs - _downAt <= TOUCH_TAP_MAX_MS) {
            tap = _start;
            return true;
        }
    }
    return false;
}
