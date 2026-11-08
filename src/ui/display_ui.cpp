/**************************************************************************
*   Author: Parikshith Venkataraghavan
*   Purpose: dashboard drawn with Adafruit GFX; subscribes to FuncGenController
**************************************************************************/

#include "display_ui.h"

DisplayUI::DisplayUI(FuncGenController& controller, InputManager& input, SPIClass& bus)
    : _tft(&bus, PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_RST), _controller(controller), _input(input), _bus(bus),
      _netFn(nullptr), _netCtx(nullptr), _bleFn(nullptr), _bleCancelFn(nullptr), _bleCtx(nullptr),
      _actionFn(nullptr), _actionCtx(nullptr),
      _stateMux(portMUX_INITIALIZER_UNLOCKED), _stateDirty(false), _remoteFields(0), _remotePending(false),
      _remoteSource(CHANGE_SOURCE::MCP), _lastNetPoll(0), _lastDraw(0) {
    _infoKey[0] = '\0';
    _pairingKey[0] = '\0';
    memset(&_net, 0, sizeof(_net));
    _net.state = WIFI_STATE::IDLE;
    memset(&_ble, 0, sizeof(_ble));
    _ble.state = BLE_STATE::OFF;
    for (uint8_t i = 0; i < DASHBOARD_AREA_COUNT; i++) {
        _canvas[i] = nullptr;
        _drawnKey[i][0] = '\0';
    }
}

void DisplayUI::setNetStatusSource(NetStatusFn fn, void* ctx) {
    _netFn = fn;
    _netCtx = ctx;
}

void DisplayUI::setActionHandler(ActionFn fn, void* ctx) {
    _actionFn = fn;
    _actionCtx = ctx;
}

void DisplayUI::setBleSource(BleStatusFn status, BleCancelFn cancelPairing, void* ctx) {
    _bleFn = status;
    _bleCancelFn = cancelPairing;
    _bleCtx = ctx;
}

bool DisplayUI::begin() {
    //the display and touch libraries both call begin() without pins, see input.cpp
    _bus.begin(PIN_LCD_SCK, PIN_LCD_MISO, PIN_LCD_MOSI);

    //~145 KB in total, malloc puts blocks this big in PSRAM
    for (uint8_t i = 0; i < DASHBOARD_AREA_COUNT; i++) {
        Rect r = areaRect(i);
        _canvas[i] = new GFXcanvas16(r.w, r.h);
        if (!_canvas[i]->getBuffer()) {
            log_w("no memory for canvas %u, drawing that area directly", i);
            delete _canvas[i];
            _canvas[i] = nullptr;
        }
    }

    _state = _latest = _lastSeen = _controller.state();
    _limits = _controller.limits();
    if (!_controller.addListener(onStateChanged, this)) return false;

    BaseType_t ok = xTaskCreatePinnedToCore(taskEntry, "ui", UI_TASK_STACK, this, UI_TASK_PRIO,
                                            nullptr, UI_TASK_CORE);
    return ok == pdPASS;
}

void DisplayUI::onStateChanged(const FuncGenState& state, CHANGE_SOURCE source, void* ctx) {
    //runs in whatever task changed the generator: copy and flag only
    DisplayUI* ui = (DisplayUI*)ctx;
    portENTER_CRITICAL(&ui->_stateMux);
    if (source != CHANGE_SOURCE::LOCAL) {
        ui->_remoteFields |= changedFields(ui->_lastSeen, state);
        ui->_remotePending = true;
        ui->_remoteSource = source;
    }
    ui->_lastSeen = state;
    ui->_latest = state;
    ui->_stateDirty = true;
    portEXIT_CRITICAL(&ui->_stateMux);
}

void DisplayUI::taskEntry(void* arg) {
    ((DisplayUI*)arg)->run();
}

void DisplayUI::run() {
    pinMode(PIN_LCD_BL, OUTPUT);
    digitalWrite(PIN_LCD_BL, HIGH);
    _tft.begin(LCD_SPI_HZ);
    _tft.setRotation(LCD_ROTATION);
    _tft.fillScreen(DASHBOARD_BG);

    pollNetStatus();
    redraw(millis());

    for (;;) {
        InputEvent event;
        if (_input.receive(event, pdMS_TO_TICKS(UI_FRAME_MS))) {
            do {
                //the pairing popup takes everything while it is up
                if (_pairing.handle(event)) continue;
                //the previous command's listener call has updated _latest by now
                takeLatestState();
                apply(_editor.handle(event, _state, _limits, millis(), infoButtons(_net, _ble)));
            } while (_input.receive(event, 0));
        }

        takeLatestState();
        uint32_t now = millis();
        _editor.tick(now);
        if (now - _lastNetPoll >= UI_NET_POLL_MS) {
            _lastNetPoll = now;
            pollNetStatus();
        }
        pollBleStatus(now);
        if (now - _lastDraw >= UI_FRAME_MS) {
            _lastDraw = now;
            redraw(now);
        }
    }
}

void DisplayUI::takeLatestState() {
    uint8_t remoteFields = 0;
    bool remote = false;
    CHANGE_SOURCE source = CHANGE_SOURCE::MCP;
    portENTER_CRITICAL(&_stateMux);
    if (_stateDirty) {
        _state = _latest;
        _stateDirty = false;
    }
    if (_remotePending) {
        remoteFields = _remoteFields;
        remote = true;
        source = _remoteSource;
        _remoteFields = 0;
        _remotePending = false;
    }
    portEXIT_CRITICAL(&_stateMux);

    if (remote) {
        _highlight.changed(remoteFields, millis(), source);
        _editor.remoteChanged(remoteFields);
    }
}

void DisplayUI::apply(const EditCommand& command) {
    switch (command.action) {
        case EDIT_ACTION::SET_WAVEFORM: _controller.setWaveform(command.waveform); break;
        case EDIT_ACTION::SET_FREQUENCY: _controller.setFrequency(command.value); break;
        case EDIT_ACTION::SET_AMPLITUDE: _controller.setAmplitude(command.value); break;
        case EDIT_ACTION::SET_DC_OFFSET: _controller.setDCOffset(command.value); break;
        case EDIT_ACTION::SET_OUTPUT: _controller.setOutputEnabled(command.enabled); break;
        case EDIT_ACTION::DEVICE:
            if (_actionFn) _actionFn(command.device, _actionCtx);
            break;
        case EDIT_ACTION::NONE:
        default: break;
    }
}

void DisplayUI::pollNetStatus() {
    if (_netFn) _net = _netFn(_netCtx);
}

void DisplayUI::pollBleStatus(uint32_t nowMs) {
    //every frame, so the passkey shows up right away. it is only a copy
    if (_bleFn) _ble = _bleFn(_bleCtx);
    _pairing.update(_ble, nowMs);
    if (_pairing.takeCancel() && _bleCancelFn) _bleCancelFn(_bleCtx);
}

//drawing

void DisplayUI::redraw(uint32_t nowMs) {
    if (_pairing.shown() != PAIR_PROMPT::HIDDEN) {
        drawPairing(nowMs);
        return;
    }
    if (_pairingKey[0]) {
        //popup just closed: everything comes back
        _pairingKey[0] = '\0';
        _infoKey[0] = '\0';
        _tft.fillScreen(DASHBOARD_BG);
        for (uint8_t area = 0; area < DASHBOARD_AREA_COUNT; area++) _drawnKey[area][0] = '\0';
    }

    DashboardView view = {_state, _net, _editor.selected(), _editor.editing(), _editor.freqStepIndex(),
                          _highlight.flashing(nowMs), _highlight.badge(nowMs), _highlight.badgeSource(),
                          _ble.state == BLE_STATE::CONNECTED};
    char key[AREA_KEY_LEN];

    if (_editor.infoOpen()) {
        drawInfo();
    } else if (_infoKey[0]) {
        //panel just closed: everything under it has to come back
        _infoKey[0] = '\0';
        _tft.fillRect(infoRect().x, infoRect().y, infoRect().w, infoRect().h, DASHBOARD_BG);
        for (uint8_t area = 0; area < UI_FIELD_COUNT; area++) _drawnKey[area][0] = '\0';
    }

    for (uint8_t area = 0; area < DASHBOARD_AREA_COUNT; area++) {
        if (_editor.infoOpen() && area != STATUS_AREA) continue;
        areaKey(view, area, key, sizeof(key));
        if (strcmp(key, _drawnKey[area]) != 0) {
            drawArea(area, view);
            strncpy(_drawnKey[area], key, AREA_KEY_LEN);
        }
    }
}

void DisplayUI::drawInfo() {
    char key[AREA_KEY_LEN];
    InfoButtons buttons = infoButtons(_net, _ble);
    InfoView view = {buttons, _editor.infoFocus(buttons), _editor.infoConfirm(), _editor.confirmOk()};
    infoKey(_net, _ble, view, key, sizeof(key));
    if (strcmp(key, _infoKey) == 0) return;
    strncpy(_infoKey, key, AREA_KEY_LEN);
    Rect r = infoRect();
    _tft.fillRect(r.x, r.y, r.w, r.h, DASHBOARD_BG);
    renderInfo(_tft, r, _net, _ble, view);
}

void DisplayUI::drawPairing(uint32_t nowMs) {
    char key[AREA_KEY_LEN];
    uint8_t seconds = _pairing.secondsLeft(nowMs);
    pairingKey(_pairing.shown(), _pairing.passkey(), seconds, key, sizeof(key));
    if (strcmp(key, _pairingKey) == 0) return;
    strncpy(_pairingKey, key, AREA_KEY_LEN);
    renderPairing(_tft, _pairing.shown(), _pairing.passkey(), seconds);
}

void DisplayUI::drawArea(uint8_t area, const DashboardView& view) {
    Rect screen = areaRect(area);
    GFXcanvas16* canvas = _canvas[area];
    if (canvas) {
        canvas->fillScreen(DASHBOARD_BG);
        renderArea(*canvas, area, Rect{0, 0, screen.w, screen.h}, view);
        _tft.drawRGBBitmap(screen.x, screen.y, canvas->getBuffer(), screen.w, screen.h);
    } else {
        _tft.fillRect(screen.x, screen.y, screen.w, screen.h, DASHBOARD_BG);
        renderArea(_tft, area, screen, view);
    }
}
