/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: rotary encoder (PCNT), button and XPT2046 touch -> InputEvent queue
**************************************************************************/

#include "input.h"
#include "driver/gpio.h"

//PCNT counter limits. accum_count keeps counting past them, so they only set
//how often the overflow interrupt fires
static const int PCNT_LIMIT = 10000;

static int16_t clamp16(int32_t v) {
    if (v > INT16_MAX) return INT16_MAX;
    if (v < INT16_MIN) return INT16_MIN;
    return (int16_t)v;
}

InputManager::InputManager(SPIClass& touchBus)
    : _bus(touchBus), _touch(PIN_TOUCH_CS, PIN_TOUCH_IRQ), _queue(nullptr), _pcnt(nullptr),
      _lastCount(0), _countRemainder(0), _pendingClicks(0), _pendingDelta(0),
      _cal(defaultTouchCalibration()), _lastCalPrintMs(0) {}

bool InputManager::begin() {
    _queue = xQueueCreate(INPUT_QUEUE_LEN, sizeof(InputEvent));
    if (!_queue) return false;

    pinMode(PIN_ENC_SW, INPUT_PULLUP);
    if (!setupEncoder()) return false;

    //both the touch and the display library call begin() without pins, and
    //SPIClass ignores pins once the bus runs, so whoever is first must pass them
    _bus.begin(PIN_LCD_SCK, PIN_LCD_MISO, PIN_LCD_MOSI);
    _touch.begin(_bus);
    //T_IRQ is open drain. the weak internal pull-up is harmless if the module
    //has its own. gpio_pullup_en leaves the library's interrupt alone (pinMode wouldn't)
    gpio_pullup_en((gpio_num_t)PIN_TOUCH_IRQ);

    BaseType_t ok = xTaskCreatePinnedToCore(taskEntry, "input", INPUT_TASK_STACK, this,
                                            INPUT_TASK_PRIO, nullptr, INPUT_TASK_CORE);
    return ok == pdPASS;
}

bool InputManager::receive(InputEvent& event, TickType_t timeout) {
    return _queue && xQueueReceive(_queue, &event, timeout) == pdTRUE;
}

bool InputManager::setupEncoder() {
    //x4 quadrature decoding, as in the ESP-IDF rotary encoder example: each
    //channel counts the edges of one pin, the level of the other sets direction
    pcnt_unit_config_t unitCfg = {};
    unitCfg.low_limit = -PCNT_LIMIT;
    unitCfg.high_limit = PCNT_LIMIT;
    unitCfg.flags.accum_count = 1;

    pcnt_glitch_filter_config_t filterCfg = {};
    filterCfg.max_glitch_ns = ENC_GLITCH_NS;

    pcnt_chan_config_t chanACfg = {};
    chanACfg.edge_gpio_num = PIN_ENC_A;
    chanACfg.level_gpio_num = PIN_ENC_B;

    pcnt_chan_config_t chanBCfg = {};
    chanBCfg.edge_gpio_num = PIN_ENC_B;
    chanBCfg.level_gpio_num = PIN_ENC_A;

    pcnt_channel_handle_t chanA = nullptr;
    pcnt_channel_handle_t chanB = nullptr;

    esp_err_t err = pcnt_new_unit(&unitCfg, &_pcnt);
    if (err == ESP_OK) err = pcnt_unit_set_glitch_filter(_pcnt, &filterCfg);
    if (err == ESP_OK) err = pcnt_new_channel(_pcnt, &chanACfg, &chanA);
    if (err == ESP_OK) err = pcnt_new_channel(_pcnt, &chanBCfg, &chanB);
    if (err == ESP_OK) err = pcnt_channel_set_edge_action(chanA, PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                                          PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    if (err == ESP_OK) err = pcnt_channel_set_level_action(chanA, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                                           PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    if (err == ESP_OK) err = pcnt_channel_set_edge_action(chanB, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                                          PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    if (err == ESP_OK) err = pcnt_channel_set_level_action(chanB, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                                           PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    //accum_count needs the limits as watch points to catch the overflows
    if (err == ESP_OK) err = pcnt_unit_add_watch_point(_pcnt, PCNT_LIMIT);
    if (err == ESP_OK) err = pcnt_unit_add_watch_point(_pcnt, -PCNT_LIMIT);
    if (err == ESP_OK) err = pcnt_unit_enable(_pcnt);
    if (err == ESP_OK) err = pcnt_unit_clear_count(_pcnt);
    if (err == ESP_OK) err = pcnt_unit_start(_pcnt);

    if (err != ESP_OK) {
        log_e("encoder setup failed: %s", esp_err_to_name(err));
        return false;
    }

    //most bare encoders have no pull-ups of their own
    gpio_pullup_en((gpio_num_t)PIN_ENC_A);
    gpio_pullup_en((gpio_num_t)PIN_ENC_B);
    return true;
}

void InputManager::taskEntry(void* arg) {
    ((InputManager*)arg)->run();
}

void InputManager::run() {
    TickType_t lastWake = xTaskGetTickCount();
    for (;;) {
        uint32_t now = millis();
        pollEncoder(now);
        pollButton(now);
        pollTouch(now);
        flushEncoder();
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(INPUT_POLL_MS));
    }
}

void InputManager::pollEncoder(uint32_t nowMs) {
    int count = 0;
    if (pcnt_unit_get_count(_pcnt, &count) != ESP_OK) return;

    _countRemainder += count - _lastCount;
    _lastCount = count;

    //C division truncates toward zero, so half a click either way stays in the remainder
    int32_t clicks = _countRemainder / ENC_COUNTS_PER_DETENT;
    _countRemainder -= clicks * ENC_COUNTS_PER_DETENT;

    if (clicks != 0) {
        _pendingClicks += clicks;
        _pendingDelta += _accel.apply(clicks, nowMs);
    }
}

void InputManager::pollButton(uint32_t nowMs) {
    BUTTON_EVENT e = _button.update(digitalRead(PIN_ENC_SW) == LOW, nowMs);
    if (e == BUTTON_EVENT::NONE) return;

    InputEvent event = {};
    event.type = (e == BUTTON_EVENT::LONG) ? INPUT_EVENT::BUTTON_LONG : INPUT_EVENT::BUTTON_SHORT;
    event.source = INPUT_SOURCE::MAIN_ENCODER;
    sendEvent(event);
}

void InputManager::pollTouch(uint32_t nowMs) {
    bool touched = false;
    TouchPoint point = {0, 0};

    //the IRQ flag stays set until the library sees the finger lifted, so
    //nothing touches the SPI bus while the screen is idle
    if (_touch.tirqTouched()) {
        TS_Point raw = _touch.getPoint();
        touched = raw.z > 0;
        if (touched) {
            point = mapTouch(raw.x, raw.y, _cal);
#if TOUCH_CALIBRATION_MODE
            if (nowMs - _lastCalPrintMs >= 100) {
                _lastCalPrintMs = nowMs;
                Serial.printf("touch raw x=%d y=%d z=%d -> screen x=%d y=%d\n",
                              raw.x, raw.y, raw.z, point.x, point.y);
            }
#endif
        }
    }

    TouchPoint tap = {0, 0};
    if (_tap.update(touched, point, nowMs, tap)) {
        InputEvent event = {};
        event.type = INPUT_EVENT::TOUCH_TAP;
        event.source = INPUT_SOURCE::TOUCH;
        event.x = tap.x;
        event.y = tap.y;
        sendEvent(event);
    }
}

void InputManager::flushEncoder() {
    if (_pendingClicks == 0 && _pendingDelta == 0) return;
    if (uxQueueSpacesAvailable(_queue) <= INPUT_QUEUE_RESERVED) return;

    InputEvent event = {};
    event.type = INPUT_EVENT::ENCODER_DELTA;
    event.source = INPUT_SOURCE::MAIN_ENCODER;
    event.clicks = clamp16(_pendingClicks);
    event.delta = clamp16(_pendingDelta);
    if (xQueueSend(_queue, &event, 0) == pdTRUE) {
        _pendingClicks -= event.clicks;
        _pendingDelta -= event.delta;
    }
}

void InputManager::sendEvent(const InputEvent& event) {
    //turns that happened before this press/tap go first, so the order is kept
    flushEncoder();
    //wait for room rather than drop it. the PCNT keeps counting meanwhile, so
    //even a stalled UI task only delays encoder clicks, never loses them
    xQueueSend(_queue, &event, portMAX_DELAY);
}
